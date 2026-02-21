#include "DangerManager.h"
#include "../BotUtils.h"
#include "../NavEngine/NavEngine.h"
#include <cmath>

void CDangerManager::Update(CTFPlayer* pLocal)
{
	if (!pLocal || !F::NavEngine.IsNavMeshLoaded())
		return;

	static Timer tUpdateTimer;
	if (!tUpdateTimer.Run(0.1f))
		return;

	m_iLastUpdateTick = I::GlobalVars->tickcount;
	
	std::vector<CNavArea*> vToRemove;
	for (auto& [pArea, data] : m_mDangerMap)
	{
		if (std::abs(m_iLastUpdateTick - data.m_iLastUpdateTick) > TIME_TO_TICKS(2.0f))
			vToRemove.push_back(pArea);
	}
	for (auto pArea : vToRemove)
		m_mDangerMap.erase(pArea);


	//change that, if we configure it correctly it will be so much better
	switch (pLocal->m_iClass())
	{
	case TF_CLASS_SCOUT:
	case TF_CLASS_HEAVY:
		m_flMinSlightDanger = 350.f;
		m_flMinFullDanger = 150.f;
		break;
	case TF_CLASS_ENGINEER:
		m_flMinSlightDanger = 350.f;
		m_flMinFullDanger = 150.f;
		break;
	case TF_CLASS_SNIPER:
		m_flMinSlightDanger = 600.f;
		m_flMinFullDanger = 300.f;
		break;
	default:
		m_flMinSlightDanger = 500.f;
		m_flMinFullDanger = 200.f;
		break;
	}

	UpdatePlayers(pLocal);
	UpdateBuildings(pLocal);
	UpdateProjectiles(pLocal);
	//UpdateStatic(pLocal);
}

void CDangerManager::Reset()
{
	m_mDangerMap.clear();
}

float CDangerManager::GetDanger(CNavArea* pArea)
{
	if (!pArea) return 0.f;

	if (auto it = m_mDangerMap.find(pArea); it != m_mDangerMap.end())
		return it->second.m_flScore;

	return 0.f;
}

float CDangerManager::GetCost(CNavArea* pArea)
{
	float flDanger = GetDanger(pArea);
	if (flDanger <= 0.f)
		return 0.f;

	if (flDanger >= DANGER_SCORE_SENTRY)
		return 10000.f; 
	
	return flDanger;
}

// legacy
bool CDangerManager::IsBlacklisted(CNavArea* pArea)
{
	return GetDanger(pArea) >= DANGER_SCORE_SENTRY * 0.9f;
}

BlacklistReason_t CDangerManager::GetBlacklistReason(CNavArea* pArea)
{
	if (auto it = m_mDangerMap.find(pArea); it != m_mDangerMap.end())
		return it->second.m_tLegacyReason;

	return {};
}

void CDangerManager::UpdatePlayers(CTFPlayer* pLocal)
{
	if (!(Vars::Misc::Movement::NavBot::Blacklist.Value & Vars::Misc::Movement::NavBot::BlacklistEnum::Players))
		return;

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		if (!pPlayer->IsAlive() || pPlayer == pLocal)
			continue;

		bool bDormant = pPlayer->IsDormant();
		bool bInvuln = pPlayer->InCond(TF_COND_INVULNERABLE) || pPlayer->InCond(TF_COND_PHASE);

		float flScore = bInvuln ? DANGER_SCORE_ENEMY_INVULN : 
						bDormant ? DANGER_SCORE_ENEMY_DORMANT : DANGER_SCORE_ENEMY_NORMAL;
		
		BlacklistReasonEnum::BlacklistReasonEnum eReason = bInvuln ? BlacklistReasonEnum::EnemyInvuln :
														   bDormant ? BlacklistReasonEnum::EnemyDormant : BlacklistReasonEnum::EnemyNormal;

		if (pPlayer->m_iClass() == TF_CLASS_SNIPER && !bDormant)
			flScore *= 2.0f;

		Vector vOrigin = {};
		if (bDormant)
		{
			if (!F::BotUtils.GetDormantOrigin(pPlayer->entindex(), &vOrigin))
				continue;
		}
		else
			vOrigin = pPlayer->GetAbsOrigin();
		
		if (vOrigin.IsZero())
			continue;
		
		std::vector<CNavArea*> vAreas;
		float flRadius = m_flMinSlightDanger;
		
		F::NavEngine.GetNavMap()->CollectAreasAround(vOrigin, flRadius, vAreas);

		for (auto& pArea : vAreas)
		{
			float flDist = pArea->m_vCenter.DistTo(vOrigin);
			
			float flDistFactor = 1.0f - (flDist / flRadius);
			float flFinalScore = flScore * (0.5f + (0.5f * flDistFactor));

			// Are you intentionally disabling vischeck for dormant players here?
			if (!bDormant && !F::NavEngine.IsVectorVisibleNavigation(vOrigin + Vector(0,0,60), pArea->m_vCenter + Vector(0,0,40)))
				continue;
			
			DangerData_t& tData = m_mDangerMap[pArea];
			if (flFinalScore > tData.m_flScore)
			{
				tData.m_flScore = flFinalScore;
				tData.m_iLastUpdateTick = m_iLastUpdateTick;
				tData.m_vOrigin = vOrigin;
				tData.m_eType = DangerType_t::Enemy;
				tData.m_tLegacyReason = { eReason, 0 };
			}
		}
	}
}

void CDangerManager::UpdateBuildings(CTFPlayer* pLocal)
{
	if (!(Vars::Misc::Movement::NavBot::Blacklist.Value & Vars::Misc::Movement::NavBot::BlacklistEnum::Sentries))
		return;

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::BuildingEnemy))
	{
		auto pBuilding = pEntity->As<CBaseObject>();
		if (!pBuilding || pBuilding->m_iHealth() <= 0)
			continue;

		if (pEntity->GetClassID() != ETFClassID::CObjectSentrygun)
			continue;

		auto pSentry = pEntity->As<CObjectSentrygun>();
		if (!pSentry || pSentry->m_iState() == SENTRY_STATE_INACTIVE)
			continue;

		bool bStrongClass = pLocal->m_iClass() == TF_CLASS_HEAVY || pLocal->m_iClass() == TF_CLASS_SOLDIER;
		if (bStrongClass && (pSentry->m_bMiniBuilding() || pSentry->m_iUpgradeLevel() == 1))
			continue;

		int iBullets = pSentry->m_iAmmoShells();
		int iRockets = pSentry->m_iAmmoRockets();
		if (iBullets == 0 && (pSentry->m_iUpgradeLevel() != 3 || iRockets == 0))
			continue;

		if ((!pSentry->m_bCarryDeploy() && pSentry->m_bBuilding()) || pSentry->m_bPlacing() || pSentry->m_bHasSapper())
			continue;

		float flScore = pSentry->m_bMiniBuilding() ? DANGER_SCORE_SENTRY * 0.8f : DANGER_SCORE_SENTRY;
		float flRadius = 1100.0f;

		Vector vOrigin = pSentry->GetAbsOrigin();
		Vector vEyePos = vOrigin + Vector(0, 0, 40.f);

		std::vector<CNavArea*> vAreas;
		F::NavEngine.GetNavMap()->CollectAreasAround(vOrigin, flRadius, vAreas);

		for (auto& pArea : vAreas)
		{
			if (pArea->m_vCenter.DistTo(vOrigin) > flRadius)
				continue;

			if (!F::NavEngine.IsVectorVisibleNavigation(vEyePos, pArea->m_vCenter + Vector(0, 0, 40), MASK_SHOT | CONTENTS_GRATE))
				continue;

			DangerData_t& tData = m_mDangerMap[pArea];
			if (flScore > tData.m_flScore)
			{
				tData.m_flScore = flScore;
				tData.m_iLastUpdateTick = m_iLastUpdateTick;
				tData.m_vOrigin = vOrigin;
				tData.m_eType = DangerType_t::Sentry;
				tData.m_tLegacyReason = { BlacklistReasonEnum::Sentry, 0 };
			}
		}
	}
}

void CDangerManager::UpdateProjectiles(CTFPlayer* pLocal)
{
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldProjectile))
	{
		if (pEntity->GetClassID() != ETFClassID::CTFGrenadePipebombProjectile)
			continue;

		if (pEntity->m_iTeamNum() == pLocal->m_iTeamNum())
			continue;

		auto pPipe = pEntity->As<CTFGrenadePipebombProjectile>();
		if (!pPipe || !pPipe->HasStickyEffects() || pPipe->IsDormant() || !pPipe->m_vecVelocity().IsZero(1.f))
			continue;

		float flRadius = 150.0f;
		float flScore = DANGER_SCORE_STICKY;

		std::vector<CNavArea*> vAreas;
		F::NavEngine.GetNavMap()->CollectAreasAround(pPipe->GetAbsOrigin(), flRadius, vAreas);

		for (auto& pArea : vAreas)
		{
			DangerData_t& tData = m_mDangerMap[pArea];
			if (flScore > tData.m_flScore)
			{
				tData.m_flScore = flScore;
				tData.m_iLastUpdateTick = m_iLastUpdateTick;
				tData.m_vOrigin = pPipe->GetAbsOrigin();
				tData.m_eType = DangerType_t::Trap;
				tData.m_tLegacyReason = { BlacklistReasonEnum::Sticky, 0 };
			}
		}
	}
}

//
// What was the idea behing the static danger?
// 
//void CDangerManager::UpdateStatic(CTFPlayer* pLocal)
//{
//	if (auto pLocalArea = F::NavEngine.GetLocalNavArea())
//	{
//		//uh
//	}
//}

void CDangerManager::Render()
{
	if (!F::NavEngine.IsReady())
		return;

	if (Vars::Misc::Movement::NavEngine::Draw.Value & Vars::Misc::Movement::NavEngine::DrawEnum::Blacklist)
	{
		for (auto& [pArea, tData] : m_mDangerMap)
		{
			if (tData.m_flScore <= 0.f)
				continue;

			Color_t tColor = { 255, 255, 255, 255 };
			
			switch (tData.m_eType)
			{
			case DangerType_t::Sentry:
				tColor = { 255, 0, 0, 255 }; // Red
				break;
			case DangerType_t::Enemy:
				tColor = { 255, 128, 0, 255 }; // Orange
				break;
			case DangerType_t::Trap:
				tColor = { 255, 255, 0, 255 }; // Yellow
				break;
			default:
				tColor = Vars::Colors::NavbotBlacklist.Value;
				break;
			}

			if (!F::NavEngine.GetNavMap() || !F::NavEngine.GetNavMap()->IsAreaValid(pArea))
				continue;

			H::Draw.RenderBox(pArea->m_vCenter, Vector(-6.0f, -6.0f, -6.0f), Vector(6.0f, 6.0f, 6.0f), Vector(0, 0, 0), tColor, false);

			if (tData.m_flScore >= DANGER_SCORE_SENTRY * 0.9f)
				H::Draw.RenderWireframeBox(pArea->m_vCenter, Vector(-6.0f, -6.0f, -6.0f), Vector(6.0f, 6.0f, 6.0f), Vector(0, 0, 0), tColor, false);
		}
	}
}
