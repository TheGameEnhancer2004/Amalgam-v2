#include "Roam.h"
#include "Capture.h"
#include "../NavEngine/NavEngine.h"
#include "../NavEngine/Controllers/Controller.h"

bool CNavBotRoam::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	static Timer tRoamTimer;
	static std::vector<CNavArea*> vVisitedAreas;
	static Timer tVisitedAreasClearTimer;
	static CNavArea* pCurrentTargetArea = nullptr;
	static int iConsecutiveFails = 0;

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
		return true;

	// Reset current target if we are not pathing or it's invalid
	pCurrentTargetArea = nullptr;

	std::vector<CNavArea*> vValidAreas;
	const Vector vLocalOrigin = pLocal->GetAbsOrigin();

	// Get all nav areas
	for (auto& tArea : F::NavEngine.GetNavFile()->m_vAreas)
	{
		// Skip if area is invalid
		if (!&tArea)
			continue;
		// Skip if area is blacklisted
		if (F::NavEngine.GetFreeBlacklist()->find(&tArea) != F::NavEngine.GetFreeBlacklist()->end())
			continue;

		// Dont run in spawn bitch
		if (tArea.m_iTFAttributeFlags & (TF_NAV_SPAWN_ROOM_BLUE | TF_NAV_SPAWN_ROOM_RED))
			continue;

		// Skip if we recently visited this area or something near it
		bool bTooCloseToVisited = false;
		for (auto pVisited : vVisitedAreas)
		{
			if (pVisited && tArea.m_vCenter.DistTo(pVisited->m_vCenter) < 750.f)
			{
				bTooCloseToVisited = true;
				break;
			}
		}
		if (bTooCloseToVisited)
			continue;

		// Skip areas that are too close to us
		float flDist = tArea.m_vCenter.DistTo(vLocalOrigin);
		if (flDist < 500.f)
			continue;

		vValidAreas.push_back(&tArea);
	}

	// No valid areas found
	if (vValidAreas.empty())
	{
		// If we failed too many times in a row, clear visited areas
		if (++iConsecutiveFails >= 3)
		{
			vVisitedAreas.clear();
			iConsecutiveFails = 0;
		}
		return false;
	}

	// Reset fail counter since we found valid areas
	iConsecutiveFails = 0;

	// Sort by distance first (farthest first)
	std::sort(vValidAreas.begin(), vValidAreas.end(), [&](CNavArea* a, CNavArea* b) {
		if (!a || !b) return false;
		return a->m_vCenter.DistToSqr(vLocalOrigin) > b->m_vCenter.DistToSqr(vLocalOrigin);
	});

	for (size_t i = 0; i < vValidAreas.size(); ++i)
	{
		if (SDK::RandomFloat(0.f, 1.f) < 0.2f) 
		{
			size_t j = (i + 1 < vValidAreas.size()) ? i + 1 : (i > 0 ? i - 1 : i);
			if (i != j)
				std::swap(vValidAreas[i], vValidAreas[j]);
		}
	}

	for (auto pArea : vValidAreas)
	{
		if (!pArea)
			continue;
		if (F::NavEngine.NavTo(pArea->m_vCenter, PriorityListEnum::Patrol))
		{
			pCurrentTargetArea = pArea;
			vVisitedAreas.push_back(pArea);
			return true;
		}
	}

	return false;
}
