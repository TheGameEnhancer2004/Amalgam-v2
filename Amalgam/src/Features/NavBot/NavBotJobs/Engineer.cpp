#include "Engineer.h"
#include "GetSupplies.h"
#include "../NavEngine/NavEngine.h"
#include "../NavEngine/Controllers/FlagController/FlagController.h"
#include "../NavEngine/Controllers/CPController/CPController.h"

bool CNavBotEngineer::BuildingNeedsToBeSmacked(CBaseObject* pBuilding)
{
	if (!pBuilding || pBuilding->m_bPlacing())
		return false;

	if (pBuilding->m_iUpgradeLevel() != 3 || pBuilding->m_iHealth() <= pBuilding->m_iMaxHealth() / 1.25f)
		return true;

	if (pBuilding->GetClassID() == ETFClassID::CObjectSentrygun)
		return pBuilding->As<CObjectSentrygun>()->m_iAmmoShells() <= pBuilding->As<CObjectSentrygun>()->MaxAmmoShells() / 2;

	return false;
}

bool CNavBotEngineer::BlacklistedFromBuilding(CNavArea* pArea)
{
	// FIXME: Better way of doing this ?
	if (auto pBlackList = F::NavEngine.GetFreeBlacklist())
	{
		for (auto [pBlacklistedArea, tReason] : *pBlackList)
		{
			if (pBlacklistedArea == pArea && tReason.m_eValue == BlacklistReasonEnum::BadBuildSpot)
				return true;
		}
	}
	return false;
}

bool CNavBotEngineer::NavToSentrySpot(Vector vLocalOrigin)
{
	static Timer tWaitUntilPathSentryTimer;

	// Wait a bit before pathing again
	if (!tWaitUntilPathSentryTimer.Run(0.3f))
		return F::NavEngine.m_eCurrentPriority == PriorityListEnum::Engineer;

	// Try to nav to our existing sentry spot
	if (m_pMySentryGun && !m_pMySentryGun->m_bPlacing())
	{
		if (m_flDistToSentry <= 100.f)
			return true;

		// Don't overwrite current nav
		if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::Engineer ||
			F::NavEngine.NavTo(m_pMySentryGun->GetAbsOrigin(), PriorityListEnum::Engineer))
			return true;
	}

	if (m_vBuildingSpots.empty())
		return false;

	if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::Engineer)
		return true;

	auto uSize = m_vBuildingSpots.size();

	// Max 10 attempts
	for (int iAttempts = 0; iAttempts < 10 && iAttempts < uSize; ++iAttempts)
	{
		// Get a semi-random building spot to still keep distance preferrance
		auto iRandomOffset = SDK::RandomInt(0, std::min(3, (int)uSize));

		BuildingSpot_t tRandomSpot;
		// Wrap around
		if (iAttempts - iRandomOffset < 0)
			tRandomSpot = m_vBuildingSpots[uSize + (iAttempts - iRandomOffset)];
		else
			tRandomSpot = m_vBuildingSpots[iAttempts - iRandomOffset];

	// Try to nav there
		bool bFailed = false;
		for (auto& vFailed : m_vFailedSpots)
		{
			if (vFailed.DistTo(tRandomSpot.m_vPos) < 1.f)
			{
				bFailed = true;
				break;
			}
		}
		if (bFailed)
			continue;

		if (F::NavEngine.NavTo(tRandomSpot.m_vPos, PriorityListEnum::Engineer))
		{
			m_tCurrentBuildingSpot = tRandomSpot;
			m_flBuildYaw = 0.0f;
			return true;
		}
	}
	return false;
}

bool CNavBotEngineer::BuildBuilding(CUserCmd* pCmd, CTFPlayer* pLocal, ClosestEnemy_t tClosestEnemy, bool bDispenser)
{
	m_eTaskStage = bDispenser ? EngineerTaskStageEnum::BuildDispenser : EngineerTaskStageEnum::BuildSentry;

	// If we've tried all rotations and still haven't built, mark this spot as failed
	if (m_flBuildYaw >= 360.0f)
	{
		m_vFailedSpots.push_back(m_tCurrentBuildingSpot.m_vPos);
		m_tCurrentBuildingSpot = {};
		m_iBuildAttempts = 0;
		m_flBuildYaw = 0.0f;
		return false;
	}

	// Make sure we have right amount of metal
	int iRequiredMetal = (bDispenser || G::SavedDefIndexes[SLOT_MELEE] == Engi_t_TheGunslinger) ? 100 : 130;
	if (pLocal->m_iMetalCount() < iRequiredMetal)
		return F::NavBotSupplies.Run(pCmd, pLocal, GetSupplyEnum::Ammo | GetSupplyEnum::Forced);
	
	// Try to build! we are close enough
	if (m_tCurrentBuildingSpot.m_flDistanceToTarget != FLT_MAX && m_tCurrentBuildingSpot.m_vPos.DistTo(pLocal->GetAbsOrigin()) <= (bDispenser ? 500.f : 200.f))
	{
		// Don't start building if an enemy is too close and we aren't already carrying the building
		if (tClosestEnemy.m_flDist < 500.f && tClosestEnemy.m_pPlayer && tClosestEnemy.m_pPlayer->IsAlive() && !pLocal->m_bCarryingObject())
			return false;

		// Try current angle for 0.3 seconds, then rotate 15 degrees
		static Timer tRotationTimer;
		pCmd->viewangles.x = 20.0f;
		pCmd->viewangles.y = m_flBuildYaw;
		I::EngineClient->SetViewAngles(pCmd->viewangles);

		if (tRotationTimer.Run(0.3f))
			m_flBuildYaw += 15.0f;

		// Gives us some time to build
		static Timer tAttemptTimer;
		if (tAttemptTimer.Run(0.3f))
			m_iBuildAttempts++;

		if (!pLocal->m_bCarryingObject())
		{
			static Timer command_timer;
			if (command_timer.Run(0.5f))
				I::EngineClient->ClientCmd_Unrestricted(std::format("build {}", bDispenser ? 0 : 2).c_str());
		}

		pCmd->buttons |= IN_ATTACK;
		pCmd->forwardmove = 20.0f;
		if (pCmd->sidemove == 0.0f)
			pCmd->sidemove = 1.0f;
		return true;
	}
	else
	{
		m_flBuildYaw = 0.0f;
		return NavToSentrySpot(pLocal->GetAbsOrigin());
	}

	return false;
}

bool CNavBotEngineer::SmackBuilding(CUserCmd* pCmd, CTFPlayer* pLocal, CBaseObject* pBuilding)
{
	m_iBuildAttempts = 0;
	if (!pLocal->m_iMetalCount())
		return F::NavBotSupplies.Run(pCmd, pLocal, GetSupplyEnum::Ammo | GetSupplyEnum::Forced);

	m_eTaskStage = pBuilding->GetClassID() == ETFClassID::CObjectDispenser ? EngineerTaskStageEnum::SmackDispenser : EngineerTaskStageEnum::SmackSentry;

	if (pBuilding->GetAbsOrigin().DistTo(pLocal->GetAbsOrigin()) <= 100.f && F::BotUtils.m_iCurrentSlot == SLOT_MELEE)
	{
		if (G::Attacking == 1)
		{
			pCmd->viewangles = Math::CalcAngle(pLocal->GetEyePosition(), pBuilding->GetCenter());
			I::EngineClient->SetViewAngles(pCmd->viewangles);
		}
		else
			pCmd->buttons |= IN_ATTACK;
	}
	else if (F::NavEngine.m_eCurrentPriority != PriorityListEnum::Engineer)
		return F::NavEngine.NavTo(pBuilding->GetAbsOrigin(), PriorityListEnum::Engineer);

	return true;
}

void CNavBotEngineer::RefreshBuildingSpots(CTFPlayer* pLocal, ClosestEnemy_t tClosestEnemy, bool bForce)
{
	if (!IsEngieMode(pLocal))
		return;

	bool bHasGunslinger = G::SavedDefIndexes[SLOT_MELEE] == Engi_t_TheGunslinger;
	static Timer tRefreshBuildingSpotsTimer;
	if (bForce || tRefreshBuildingSpotsTimer.Run(bHasGunslinger ? 1.f : 5.f))
	{
		m_vBuildingSpots.clear();

		int iLocalTeam = pLocal->m_iTeamNum();
		int iEnemyTeam = (iLocalTeam == TF_TEAM_RED) ? TF_TEAM_BLUE : TF_TEAM_RED;

		// Determine the "front line" or enemy focus point
		Vector vEnemyOrigin;
		bool bFoundEnemy = false;

		// 1. Try dormant enemy origin (last known position of the closest enemy)
		if (F::BotUtils.GetDormantOrigin(tClosestEnemy.m_iEntIdx, vEnemyOrigin))
			bFoundEnemy = true;
		// 2. Try enemy objective (if we are attacking)
		else if (F::FlagController.GetPosition(iEnemyTeam, vEnemyOrigin))
			bFoundEnemy = true;
		else if (F::CPController.GetClosestControlPoint(pLocal->GetAbsOrigin(), iEnemyTeam, vEnemyOrigin))
			bFoundEnemy = true;
		// 3. Fallback to our own objective (defending)
		else if (F::FlagController.GetSpawnPosition(iLocalTeam, vEnemyOrigin))
			bFoundEnemy = true;
		// 4. Fallback to enemy spawn
		else if (F::FlagController.GetSpawnPosition(iEnemyTeam, vEnemyOrigin))
			bFoundEnemy = true;

		if (!bFoundEnemy)
			return;

		// Check for all threats that could rape us
		std::vector<CTFPlayer*> vEnemies;
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerTeam))
		{
			auto pPlayer = pEntity->As<CTFPlayer>();
			if (pPlayer->IsDormant() || !pPlayer->IsAlive() || pPlayer->m_iTeamNum() == iLocalTeam)
				continue;
			vEnemies.push_back(pPlayer);
		}

		for (auto& tArea : F::NavEngine.GetNavFile()->m_vAreas)
		{
			if (BlacklistedFromBuilding(&tArea))
				continue;

			if (tArea.m_iTFAttributeFlags & (TF_NAV_SPAWN_ROOM_RED | TF_NAV_SPAWN_ROOM_BLUE | TF_NAV_SPAWN_ROOM_EXIT))
				continue;

			float flDistToEnemyOrigin = tArea.m_vCenter.DistTo(vEnemyOrigin);
			if (flDistToEnemyOrigin >= 4000.f)
				continue;

			auto AddSpot = [&](const Vector& vPos)
			{
				for (auto& vFailed : m_vFailedSpots)
				{
					if (vFailed.DistTo(vPos) < 1.f)
						return;
				}

				// Check if we can actually build here, sentry size is roughly 40x40x66.
				CGameTrace trace;
				CTraceFilterNavigation filter;
				Vector vMins(-30.f, -30.f, 0.f);
				Vector vMaxs(30.f, 30.f, 66.f);
				SDK::TraceHull(vPos + Vector(0, 0, 5), vPos + Vector(0, 0, 5), vMins, vMaxs, MASK_PLAYERSOLID, &filter, &trace);
				if (trace.DidHit())
					return;

				SDK::Trace(vPos + Vector(0, 0, 10), vPos - Vector(0, 0, 10), MASK_PLAYERSOLID, &filter, &trace);
				if (!trace.DidHit())
					return;

				float flDistToEnemy = vPos.DistTo(vEnemyOrigin);
				float flScore = flDistToEnemy;

				// too close to enemy
				float flMinDist = bHasGunslinger ? 400.f : 800.f;
				if (flDistToEnemy < flMinDist)
					flScore += (flMinDist - flDistToEnemy) * 10.f;

				// too far
				if (flDistToEnemy > 2500.f)
					flScore += (flDistToEnemy - 2500.f) * 2.f;

				for (auto pEnemy : vEnemies)
				{
					if (pEnemy->GetAbsOrigin().DistTo(vPos) < 600.f)
					{
						flScore += 2000.f;
						break;
					}
				}

				if (tArea.m_iTFAttributeFlags & TF_NAV_SENTRY_SPOT)
					flScore -= 200.f;

				m_vBuildingSpots.emplace_back(flScore, vPos);
			};

			if (tArea.m_iTFAttributeFlags & TF_NAV_SENTRY_SPOT)
				AddSpot(tArea.m_vCenter);
			else
			{
				for (auto& tHidingSpot : tArea.m_vHidingSpots)
				{
					if (tHidingSpot.HasGoodCover())
						AddSpot(tHidingSpot.m_vPos);
				}
			}
		}

		std::sort(m_vBuildingSpots.begin(), m_vBuildingSpots.end(),
				  [](const BuildingSpot_t& a, const BuildingSpot_t& b) -> bool
				  {
					  return a.m_flDistanceToTarget < b.m_flDistanceToTarget;
				  });
	}
}

void CNavBotEngineer::Render()
{
	if (!Vars::Misc::Movement::NavEngine::Draw.Value)
		return;

	auto pLocal = H::Entities.GetLocal();
	if (!pLocal || !pLocal->IsAlive() || pLocal->m_iClass() != TF_CLASS_ENGINEER)
		return;

	for (auto& tSpot : m_vBuildingSpots)
	{
		bool bIsCurrent = (tSpot.m_vPos == m_tCurrentBuildingSpot.m_vPos);
		Color_t color = bIsCurrent ? Color_t(0, 255, 0, 255) : Color_t(255, 255, 255, 100);
		
		H::Draw.RenderWireframeBox(tSpot.m_vPos, Vector(-30, -30, 0), Vector(30, 30, 66), Vector(0, 0, 0), color, false);
		if (bIsCurrent)
			H::Draw.RenderBox(tSpot.m_vPos, Vector(-30, -30, 0), Vector(30, 30, 66), Vector(0, 0, 0), Color_t(0, 255, 0, 50), false);
	}
}

void CNavBotEngineer::RefreshLocalBuildings(CTFPlayer* pLocal)
{
	if (IsEngieMode(pLocal))
	{
		m_pMySentryGun = pLocal->GetObjectOfType(OBJ_SENTRYGUN)->As<CObjectSentrygun>();
		m_pMyDispenser = pLocal->GetObjectOfType(OBJ_DISPENSER)->As<CObjectDispenser>();
		m_flDistToSentry = m_pMySentryGun ? m_pMySentryGun->GetAbsOrigin().DistTo(pLocal->GetAbsOrigin()) : FLT_MAX;
		m_flDistToDispenser = m_pMyDispenser ? m_pMyDispenser->GetAbsOrigin().DistTo(pLocal->GetAbsOrigin()) : FLT_MAX;
	}
}

void CNavBotEngineer::Reset()
{
	m_pMySentryGun = nullptr;
	m_pMyDispenser = nullptr;
	m_flDistToSentry = FLT_MAX;
	m_flDistToDispenser = FLT_MAX;
	m_iBuildAttempts = 0;
	m_flBuildYaw = 0.0f;
	m_vBuildingSpots.clear();
	m_vFailedSpots.clear();
	m_tCurrentBuildingSpot = {};
	m_eTaskStage = EngineerTaskStageEnum::None;
}

bool CNavBotEngineer::IsEngieMode(CTFPlayer* pLocal)
{
	return Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::AutoEngie &&
		(Vars::Aimbot::AutoEngie::AutoRepair.Value || Vars::Aimbot::AutoEngie::AutoUpgrade.Value) &&
		pLocal && pLocal->IsAlive() && pLocal->m_iClass() == TF_CLASS_ENGINEER;
}

bool CNavBotEngineer::Run(CUserCmd* pCmd, CTFPlayer* pLocal, ClosestEnemy_t tClosestEnemy)
{
	if (!IsEngieMode(pLocal))
	{
		m_eTaskStage = EngineerTaskStageEnum::None;
		return false;
	}

	static Timer tBuildingCheckTimer;
	if (tBuildingCheckTimer.Run(10.f))
	{
		if (!m_pMySentryGun || !m_pMyDispenser)
			RefreshBuildingSpots(pLocal, tClosestEnemy, true);
	}

	// Already have a sentry
	if (m_pMySentryGun && !m_pMySentryGun->m_bPlacing())
	{
		if (G::SavedDefIndexes[SLOT_MELEE] == Engi_t_TheGunslinger)
		{
			// Too far away, destroy it
			if (m_pMySentryGun->GetAbsOrigin().DistTo(pLocal->GetAbsOrigin()) >= 1800.f)
				I::EngineClient->ClientCmd_Unrestricted("destroy 2");

			// Return false so we run another task
			m_eTaskStage = EngineerTaskStageEnum::None;
			return false;
		}
		else
		{
			// Try to smack sentry first
			if (BuildingNeedsToBeSmacked(m_pMySentryGun))
				return SmackBuilding(pCmd, pLocal, m_pMySentryGun);
			else
			{
				if (m_pMyDispenser && !m_pMyDispenser->m_bPlacing())
				{
					// We already have a dispenser, see if it needs to be smacked
					if (BuildingNeedsToBeSmacked(m_pMyDispenser))
						return SmackBuilding(pCmd, pLocal, m_pMyDispenser);

					// Return false so we run another task
					m_eTaskStage = EngineerTaskStageEnum::None;
					return false;
				}
				else
				{
					// We put dispenser by sentry
					return BuildBuilding(pCmd, pLocal, tClosestEnemy, true);
				}
			}
		}
	}
	// Try to build a sentry
	return BuildBuilding(pCmd, pLocal, tClosestEnemy, false);
}
