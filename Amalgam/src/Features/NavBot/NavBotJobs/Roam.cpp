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

	// Clear visited areas every 60 seconds to allow revisiting
	if (tVisitedAreasClearTimer.Run(60.f))
	{
		vVisitedAreas.clear();
		iConsecutiveFails = 0;
	}

	// Don't path constantly
	if (!tRoamTimer.Run(2.f))
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
		default:
			break;
		}
		if (bGotTarget)
		{
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
					if (tHidingSpot.first->m_vCenter.DistTo(vLocalOrigin) <= 250.f)
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

	// If we have a current target and are pathing, continue
	if (pCurrentTargetArea && F::NavEngine.m_eCurrentPriority == PriorityListEnum::Patrol)
		return true;

	// Reset current target
	pCurrentTargetArea = nullptr;

	std::vector<CNavArea*> vValidAreas;

	// Get all nav areas
	for (auto& tArea : F::NavEngine.GetNavFile()->m_vAreas)
	{
		// Skip if area is blacklisted
		if (F::NavEngine.GetFreeBlacklist()->find(&tArea) != F::NavEngine.GetFreeBlacklist()->end())
			continue;

		// Dont run in spawn bitch
		if (tArea.m_iTFAttributeFlags & (TF_NAV_SPAWN_ROOM_BLUE | TF_NAV_SPAWN_ROOM_RED))
			continue;

		// Skip if we recently visited this area
		if (std::find(vVisitedAreas.begin(), vVisitedAreas.end(), &tArea) != vVisitedAreas.end())
			continue;

		// Skip areas that are too close
		float flDist = tArea.m_vCenter.DistTo(pLocal->GetAbsOrigin());
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

	// Different strategies for area selection
	std::vector<CNavArea*> vPotentialTargets;

	// Strategy 1: Try to find areas that are far from current position
	for (auto pArea : vValidAreas)
	{
		float flDist = pArea->m_vCenter.DistTo(pLocal->GetAbsOrigin());
		if (flDist > 2000.f)
			vPotentialTargets.push_back(pArea);
	}

	// Strategy 2: If no far areas found, try areas that are at medium distance
	if (vPotentialTargets.empty())
	{
		for (auto pArea : vValidAreas)
		{
			float flDist = pArea->m_vCenter.DistTo(pLocal->GetAbsOrigin());
			if (flDist > 1000.f && flDist <= 2000.f)
				vPotentialTargets.push_back(pArea);
		}
	}

	// Strategy 3: If still no areas found, use any valid area
	if (vPotentialTargets.empty())
		vPotentialTargets = vValidAreas;

	// Shuffle the potential targets to add randomness
	for (size_t i = vPotentialTargets.size() - 1; i > 0; i--)
	{
		int j = rand() % (i + 1);
		std::swap(vPotentialTargets[i], vPotentialTargets[j]);
	}

	// Try to path to potential targets
	for (auto pArea : vPotentialTargets)
	{
		if (F::NavEngine.NavTo(pArea->m_vCenter, PriorityListEnum::Patrol))
		{
			pCurrentTargetArea = pArea;
			vVisitedAreas.push_back(pArea);
			return true;
		}
	}

	return false;
}
