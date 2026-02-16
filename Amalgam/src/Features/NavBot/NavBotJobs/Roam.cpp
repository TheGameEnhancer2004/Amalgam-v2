#include "Roam.h"
#include "Capture.h"
#include "../DangerManager/DangerManager.h"
#include "../NavEngine/NavEngine.h"
#include "../NavEngine/Controllers/Controller.h"
#include <unordered_set>
#include <algorithm>
#include <cmath>

bool CNavBotRoam::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	static Timer tRoamTimer;
	static std::vector<CNavArea*> vVisitedAreas; // Should be cleared when nav engine is off, currently it is not
	static Timer tVisitedAreasClearTimer;
	static CNavArea* pCurrentTargetArea = nullptr;
	static int iConsecutiveFails = 0;
	static CMap* pLastMap = nullptr;
	static CNavArea* pLastConnectedSeed = nullptr;
	static std::unordered_set<CNavArea*> sConnectedAreas;
	static Timer tConnectedAreasRefreshTimer;

	auto pMap = F::NavEngine.GetNavMap();
	if (!pMap)
	{
		vVisitedAreas.clear();
		pCurrentTargetArea = nullptr;
		iConsecutiveFails = 0;
		sConnectedAreas.clear();
		pLastConnectedSeed = nullptr;
		pLastMap = nullptr;
		return false;
	}

	if (pLastMap != pMap)
	{
		vVisitedAreas.clear();
		pCurrentTargetArea = nullptr;
		iConsecutiveFails = 0;
		sConnectedAreas.clear();
		pLastConnectedSeed = nullptr;
		tConnectedAreasRefreshTimer.Update();
		pLastMap = pMap;
	}

	// Clear visited areas if they get too large or after some time
	if (tVisitedAreasClearTimer.Run(60.f) || vVisitedAreas.size() > 40)
	{
		vVisitedAreas.clear();
		iConsecutiveFails = 0;
	}

	// Don't path constantly
	if (!tRoamTimer.Run(0.5f))
		return false;

	if (F::NavEngine.m_eCurrentPriority > PriorityListEnum::Patrol)
		return false;

	const Vector vLocalOrigin = pLocal->GetAbsOrigin();
	Vector vObjectiveAnchor = {};
	bool bHasObjectiveAnchor = false;

	// Defend our objective if possible
	if (Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::DefendObjectives)
	{
		int iEnemyTeam = pLocal->m_iTeamNum() == TF_TEAM_BLUE ? TF_TEAM_RED : TF_TEAM_BLUE;

		Vector vTarget;
		const auto vLocalOrigin = pLocal->GetAbsOrigin();
		bool bGotTarget = false;

		switch (F::GameObjectiveController.m_eGameMode)
		{
		case TF_GAMETYPE_CP:
			bGotTarget = F::NavBotCapture.GetControlPointGoal(vLocalOrigin, iEnemyTeam, vTarget);
			break;
		case TF_GAMETYPE_ESCORT:
			bGotTarget = F::NavBotCapture.GetPayloadGoal(vLocalOrigin, iEnemyTeam, vTarget);
			break;
		case TF_GAMETYPE_CTF:
			if (F::GameObjectiveController.m_bHaarp)
				bGotTarget = F::NavBotCapture.GetCtfGoal(pLocal, pLocal->m_iTeamNum(), iEnemyTeam, vTarget);
			break;
		default:
			break;
		}
		if (bGotTarget)
		{
			vObjectiveAnchor = vTarget;
			bHasObjectiveAnchor = true;
		}

		if (bGotTarget || F::NavBotCapture.m_bOverwriteCapture)
		{
			if (F::NavBotCapture.m_bOverwriteCapture)
			{
				F::NavEngine.CancelPath();
				m_bDefending = true;
				return true;
			}

			if (auto pClosestNav = F::NavEngine.FindClosestNavArea(vTarget))
			{
				// Get closest enemy to vicheck
				CBaseEntity* pClosestEnemy = nullptr;
				float flBestDist = FLT_MAX;
				for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
				{
					if (!F::BotUtils.ShouldTarget(pLocal, pWeapon, pEntity->entindex()))
						continue;

					float flDist = pEntity->GetAbsOrigin().DistTo(pClosestNav->m_vCenter);
					if (flDist > flBestDist)
						continue;

					flBestDist = flDist;
					pClosestEnemy = pEntity;
				}

				Vector vVischeckPoint;
				bool bVischeck = pClosestEnemy && flBestDist <= 1000.f;
				if (bVischeck)
				{
					vVischeckPoint = pClosestEnemy->GetAbsOrigin();
					vVischeckPoint.z += PLAYER_CROUCHED_JUMP_HEIGHT;
				}

				std::pair<CNavArea*, int> tHidingSpot;
				if (F::NavBotCore.FindClosestHidingSpot(pClosestNav, vVischeckPoint, 5, tHidingSpot, bVischeck))
				{
					if (tHidingSpot.first && tHidingSpot.first->m_vCenter.DistTo(vLocalOrigin) <= 250.f)
					{
						F::NavEngine.CancelPath();
						m_bDefending = true;
						return true;
					}
					if (F::NavEngine.NavTo(tHidingSpot.first->m_vCenter, PriorityListEnum::Patrol, true, !F::NavEngine.IsPathing()))
					{
						m_bDefending = true;
						return true;
					}
				}
			}
		}
	}
	m_bDefending = false;
	if (pCurrentTargetArea && F::NavEngine.m_eCurrentPriority == PriorityListEnum::Patrol)
	{
		bool bCanKeepTarget = F::NavEngine.IsPathing() && pCurrentTargetArea->m_vCenter.DistTo(vLocalOrigin) <= 4200.f;
		if (pMap)
		{
			auto tAreaKey = std::pair<CNavArea*, CNavArea*>(pCurrentTargetArea, pCurrentTargetArea);
			auto it = pMap->m_mVischeckCache.find(tAreaKey);
			if (it != pMap->m_mVischeckCache.end() && !it->second.m_bPassable && (it->second.m_iExpireTick == 0 || it->second.m_iExpireTick > I::GlobalVars->tickcount) && it->second.m_bStuckBlacklist)
				bCanKeepTarget = false;
		}

		if (bCanKeepTarget)
			return true;

		pCurrentTargetArea = nullptr;
	}

	// Reset current target if we are not pathing or it's invalid
	pCurrentTargetArea = nullptr;

	struct RoamCandidate_t
	{
		CNavArea* m_pArea = nullptr;
		float m_flBlacklistPenalty = 0.f;
		float m_flDangerCost = 0.f;
		bool m_bSoftBlocked = false;
	};

	std::vector<RoamCandidate_t> vCandidates;
	auto pLocalArea = F::NavEngine.GetLocalNavArea(vLocalOrigin);
	if (!pLocalArea)
		return false;

	if (pLastConnectedSeed != pLocalArea || sConnectedAreas.empty() || tConnectedAreasRefreshTimer.Run(2.f))
	{
		std::vector<CNavArea*> vConnectedAreas;
		if (pMap)
			pMap->CollectAreasAround(vLocalOrigin, 100000.f, vConnectedAreas);

		sConnectedAreas.clear();
		for (auto pArea : vConnectedAreas)
			if (pArea)
				sConnectedAreas.insert(pArea);

		if (sConnectedAreas.empty())
			sConnectedAreas.insert(pLocalArea);

		pLastConnectedSeed = pLocalArea;
	}

	// Get all nav areas
	for (auto& tArea : F::NavEngine.GetNavFile()->m_vAreas)
	{
		if (!sConnectedAreas.contains(&tArea))
			continue;

		float flBlacklistPenalty = 0.f;
		if (pMap)
		{
			auto itBlacklist = F::NavEngine.GetFreeBlacklist()->find(&tArea);
			if (itBlacklist != F::NavEngine.GetFreeBlacklist()->end())
			{
				flBlacklistPenalty = pMap->GetBlacklistPenalty(itBlacklist->second);
				if (!std::isfinite(flBlacklistPenalty) || flBlacklistPenalty >= 4000.f)
					continue;
			}
		}

		bool bSoftBlocked = false;
		if (pMap)
		{
			auto tAreaKey = std::pair<CNavArea*, CNavArea*>(&tArea, &tArea);
			auto it = pMap->m_mVischeckCache.find(tAreaKey);
			if (it != pMap->m_mVischeckCache.end() && !it->second.m_bPassable && (it->second.m_iExpireTick == 0 || it->second.m_iExpireTick > I::GlobalVars->tickcount))
			{
				if (it->second.m_bStuckBlacklist)
					continue;

				bSoftBlocked = true;
			}
		}

		// Dont run in spawn bitch
		if (tArea.m_iTFAttributeFlags & (TF_NAV_SPAWN_ROOM_BLUE | TF_NAV_SPAWN_ROOM_RED))
			continue;

		RoamCandidate_t tCandidate{};
		tCandidate.m_pArea = &tArea;
		tCandidate.m_flBlacklistPenalty = flBlacklistPenalty;
		tCandidate.m_flDangerCost = F::DangerManager.GetCost(&tArea);
		tCandidate.m_bSoftBlocked = bSoftBlocked;
		vCandidates.push_back(tCandidate);
	}

	// No valid areas found
	if (vCandidates.empty())
		return false;

	std::vector<std::pair<CNavArea*, float>> vScoredAreas;
	vScoredAreas.reserve(vCandidates.size());
	const float flLocalToObjective = bHasObjectiveAnchor ? vLocalOrigin.DistTo(vObjectiveAnchor) : 0.f;

	for (const auto& tCandidate : vCandidates)
	{
		auto pArea = tCandidate.m_pArea;
		if (!pArea)
			continue;

		const float flDist = pArea->m_vCenter.DistTo(vLocalOrigin);

		float flObjectiveScore = 0.f;
		if (bHasObjectiveAnchor)
		{
			const float flAreaToObjective = pArea->m_vCenter.DistTo(vObjectiveAnchor);
			const float flProgress = std::clamp((flLocalToObjective - flAreaToObjective) / 1200.f, -1.f, 1.f);
			flObjectiveScore = flProgress * 900.f;
		}

		float flSafetyPenalty = std::clamp(tCandidate.m_flDangerCost, 0.f, 8000.f) * 0.08f;
		flSafetyPenalty += std::min(tCandidate.m_flBlacklistPenalty, 2500.f) * 0.45f;
		if (tCandidate.m_bSoftBlocked)
			flSafetyPenalty += 450.f;

		float flSpeedScore = -flDist * 0.35f;
		if (flDist < 350.f)
			flSpeedScore -= 250.f;
		if (flDist > 4200.f)
			flSpeedScore -= 280.f;

		float flVisitedPenalty = 0.f;
		for (auto pVisited : vVisitedAreas)
		{
			if (pVisited && pArea->m_vCenter.DistTo(pVisited->m_vCenter) < 750.f)
			{
				flVisitedPenalty += 500.f;
				break;
			}
		}

		float flScore = flObjectiveScore - flSafetyPenalty + flSpeedScore - flVisitedPenalty;

		vScoredAreas.emplace_back(pArea, flScore);
	}

	std::sort(vScoredAreas.begin(), vScoredAreas.end(), [](const auto& a, const auto& b)
		{
			return a.second > b.second;
		});

	const size_t uPathCostCandidates = std::min<size_t>(vScoredAreas.size(), 16);
	for (size_t i = 0; i < uPathCostCandidates; i++)
	{
		auto& tScoredArea = vScoredAreas[i];
		const float flPathCost = F::NavEngine.GetPathCost(vLocalOrigin, tScoredArea.first->m_vCenter);
		if (std::isfinite(flPathCost) && flPathCost < FLT_MAX)
			tScoredArea.second -= flPathCost * 0.12f;
		else
			tScoredArea.second -= 1200.f;
	}

	if (uPathCostCandidates > 0)
	{
		std::sort(vScoredAreas.begin(), vScoredAreas.end(), [](const auto& a, const auto& b)
			{
				return a.second > b.second;
			});
	}

	int iAttempts = 0;
	for (auto& [pArea, _] : vScoredAreas)
	{
		if (!pArea)
			continue;

		if (iAttempts++ > 40)
			break;
		if (F::NavEngine.NavTo(pArea->m_vCenter, PriorityListEnum::Patrol))
		{
			pCurrentTargetArea = pArea;
			vVisitedAreas.push_back(pArea);
			iConsecutiveFails = 0;
			return true;
		}
	}

	if (++iConsecutiveFails >= 3)
	{
		vVisitedAreas.clear();
		iConsecutiveFails = 0;
	}

	return false;
}
