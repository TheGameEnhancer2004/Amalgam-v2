#include "EscapeDanger.h"
#include "../NavEngine/NavEngine.h"
#include "../NavEngine/Controllers/FlagController/FlagController.h"
#include "../NavEngine/Controllers/Controller.h"

bool CNavBotDanger::EscapeDanger(CTFPlayer* pLocal)
{
    if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::EscapeDanger))
		return false;

	// Don't escape while we have the intel
	if (Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::DontEscapeDangerIntel && F::GameObjectiveController.m_eGameMode == TF_GAMETYPE_CTF)
	{
		auto iFlagCarrierIdx = F::FlagController.GetCarrier(pLocal->m_iTeamNum());
		if (iFlagCarrierIdx == pLocal->entindex())
			return false;
	}

	// Priority too high
	if (F::NavEngine.m_eCurrentPriority > PriorityListEnum::EscapeDanger ||
		F::NavEngine.m_eCurrentPriority == PriorityListEnum::MeleeAttack ||
		F::NavEngine.m_eCurrentPriority == PriorityListEnum::RunSafeReload)
		return false;

	
	// Check if we're in spawn - if so, ignore danger and focus on getting out
	auto pLocalArea = F::NavEngine.GetLocalNavArea();
	if (pLocalArea->m_iTFAttributeFlags & TF_NAV_SPAWN_ROOM_RED ||
		pLocalArea->m_iTFAttributeFlags & TF_NAV_SPAWN_ROOM_BLUE)
		return false;

	auto pBlacklist = F::NavEngine.GetFreeBlacklist();
	
	// Check if we're in any danger
	bool bInHighDanger = false;
	bool bInMediumDanger = false;
	bool bInLowDanger = false;
	
	if (pBlacklist && pBlacklist->contains(pLocalArea))
	{
		// Check building spot - don't run away from that
		if ((*pBlacklist)[pLocalArea].m_eValue == BlacklistReasonEnum::BadBuildSpot)
			return false;
			
		// Determine danger level
		switch ((*pBlacklist)[pLocalArea].m_eValue)
		{
		case BlacklistReasonEnum::Sentry:
		case BlacklistReasonEnum::Sticky:
		case BlacklistReasonEnum::EnemyInvuln:
			bInHighDanger = true;
			break;
		case BlacklistReasonEnum::SentryMedium:
		case BlacklistReasonEnum::EnemyNormal:
			bInMediumDanger = true;
			break;
		case BlacklistReasonEnum::SentryLow:
		case BlacklistReasonEnum::EnemyDormant:
			bInLowDanger = true;
			break;
		}
		
		// Only escape from high danger by default
		// Also escape from medium danger if health is low
		bool bShouldEscape = bInHighDanger || 
		                    (bInMediumDanger && pLocal->m_iHealth() < pLocal->GetMaxHealth() * 0.5f);
		
		// If we're not in high danger and on an important task, we might not need to escape
		bool bImportantTask = (F::NavEngine.m_eCurrentPriority == PriorityListEnum::Capture || 
		                      F::NavEngine.m_eCurrentPriority == PriorityListEnum::GetHealth ||
		                      F::NavEngine.m_eCurrentPriority == PriorityListEnum::Engineer);
		
		if (!bShouldEscape && bImportantTask)
			return false;
		
		// If we're in low danger only and on any task, don't escape
		if (bInLowDanger && !bInMediumDanger && !bInHighDanger && F::NavEngine.m_eCurrentPriority != 0)
			return false;

		static CNavArea* pTargetArea = nullptr;
		// Already running and our target is still valid
		if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::EscapeDanger && !pBlacklist->contains(pTargetArea))
			return true;

		// Determine the reference position to stay close to
		Vector vReferencePosition;
		bool bHasTarget = false;

		// If we were pursuing a specific objective or following a target, try to stay close to it
		if (F::NavEngine.m_eCurrentPriority != 0 && F::NavEngine.m_eCurrentPriority != PriorityListEnum::EscapeDanger && F::NavEngine.IsPathing())
		{
			// Use the last crumb in our path as the reference position
			vReferencePosition = F::NavEngine.GetCrumbs()->back().m_vPos;
			bHasTarget = true;
		}
		else
		{
			// Use current position if we don't have a target
			vReferencePosition = pLocal->GetAbsOrigin();
		}

		std::vector<std::pair<CNavArea*, float>> vSafeAreas;
		std::vector<CNavArea*> vAreaPointers;

		// Copy areas and calculate distances
		for (auto& tArea : F::NavEngine.GetNavFile()->m_vAreas)
		{
			// Get all areas
			vAreaPointers.push_back(&tArea);

			// Skip if area is blacklisted with high danger
			auto it = pBlacklist->find(&tArea);
			if (it != pBlacklist->end())
			{
				// Check danger level - allow pathing through medium or low danger if we have a target
				bool bContinue = false;
				switch (it->second.m_eValue)
				{
				case BlacklistReasonEnum::Sentry:
				case BlacklistReasonEnum::Sticky:
				case BlacklistReasonEnum::EnemyInvuln:
					// Skip high danger areas
					bContinue = true;
					break;
				case BlacklistReasonEnum::SentryMedium:
				case BlacklistReasonEnum::EnemyNormal:
					// Skip medium danger areas if we don't have a target or have low health
					bContinue = !bHasTarget || pLocal->m_iHealth() < pLocal->GetMaxHealth() * 0.5f;
					break;
				default:
					break;
				}
				if (bContinue)
					continue;
			}

			float flDistToReference = tArea.m_vCenter.DistTo(vReferencePosition);
			float flDistToCurrent = tArea.m_vCenter.DistTo(pLocal->GetAbsOrigin());
			
			// Only consider areas that are not too far away and reachable
			if (flDistToCurrent < 2000.f)
			{
				// If we have a target, prioritize staying near it
				float flScore = bHasTarget ? flDistToReference : flDistToCurrent;
				vSafeAreas.push_back({ &tArea, flScore });
			}
		}

		// Sort by score (closer to reference position is better)
		std::sort(vSafeAreas.begin(), vSafeAreas.end(), [](const std::pair<CNavArea*, float>& a, const std::pair<CNavArea*, float>& b) -> bool
		{
			return a.second < b.second;
		});

		int iCalls = 0;
		// Try to path to safe areas
		for (auto& pArea : vSafeAreas)
		{
			// Try the 10 closest areas (increased from 5 to give more options)
			iCalls++;
			if (iCalls > 10)
				break;

			// Check if this area is safe (not near enemy)
			bool bIsSafe = true;
			for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
			{
				if (!F::BotUtils.ShouldTarget(pLocal, pLocal->m_hActiveWeapon().Get()->As<CTFWeaponBase>(), pEntity->entindex()))
					continue;

				// If enemy is too close to this area, mark it as unsafe
				float flDist = pEntity->GetAbsOrigin().DistTo(pArea.first->m_vCenter);
				if (flDist < F::NavBotCore.m_tSelectedConfig.m_flMinFullDanger * 1.2f)
				{
					bIsSafe = false;
					break;
				}
			}

			// Skip unsafe areas
			if (!bIsSafe)
				continue;

			if (F::NavEngine.NavTo(pArea.first->m_vCenter, PriorityListEnum::EscapeDanger))
			{
				pTargetArea = pArea.first;
				return true;
			}
		}

		// If we couldn't find a safe area close to the target, fall back to any safe area
		if (iCalls <= 0 || (bInHighDanger && iCalls < 10))
		{
			// Sort by distance to player
			std::sort(vAreaPointers.begin(), vAreaPointers.end(), [&](CNavArea* a, CNavArea* b) -> bool
			{
				return a->m_vCenter.DistTo(pLocal->GetAbsOrigin()) < b->m_vCenter.DistTo(pLocal->GetAbsOrigin());
			});

			// Try to path to any non-blacklisted area
			for (auto& pArea : vAreaPointers)
			{
				auto it = pBlacklist->find(pArea);
				if (it == pBlacklist->end() || 
				   (bInHighDanger && (it->second.m_eValue == BlacklistReasonEnum::SentryLow || it->second.m_eValue == BlacklistReasonEnum::EnemyDormant)))
				{
					iCalls++;
					if (iCalls > 5)
						break;
					if (F::NavEngine.NavTo(pArea->m_vCenter, PriorityListEnum::EscapeDanger))
					{
						pTargetArea = pArea;
						return true;
					}
				}
			}
		}
	}
	// No longer in danger
	else if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::EscapeDanger)
		F::NavEngine.CancelPath();

	return false;
}

// Check if a position is safe from stickies and projectiles
static bool IsPositionSafe(Vector vPos, int iLocalTeam)
{
	if (!(Vars::Misc::Movement::NavBot::Blacklist.Value & Vars::Misc::Movement::NavBot::BlacklistEnum::Stickies) &&
		!(Vars::Misc::Movement::NavBot::Blacklist.Value & Vars::Misc::Movement::NavBot::BlacklistEnum::Projectiles))
		return true;

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldProjectile))
	{
		if (pEntity->m_iTeamNum() == iLocalTeam)
			continue;

		auto iClassId = pEntity->GetClassID();
		// Check for stickies
		if (Vars::Misc::Movement::NavBot::Blacklist.Value & Vars::Misc::Movement::NavBot::BlacklistEnum::Stickies && iClassId == ETFClassID::CTFGrenadePipebombProjectile)
		{
			// Skip non-sticky projectiles
			if (pEntity->As<CTFGrenadePipebombProjectile>()->m_iType() != TF_GL_MODE_REMOTE_DETONATE)
				continue;

			float flDist = pEntity->m_vecOrigin().DistTo(vPos);
			if (flDist < Vars::Misc::Movement::NavBot::StickyDangerRange.Value)
				return false;
		}

		// Check for rockets and pipes
		if (Vars::Misc::Movement::NavBot::Blacklist.Value & Vars::Misc::Movement::NavBot::BlacklistEnum::Projectiles)
		{
			if (iClassId == ETFClassID::CTFProjectile_Rocket ||
				(iClassId == ETFClassID::CTFGrenadePipebombProjectile && pEntity->As<CTFGrenadePipebombProjectile>()->m_iType() == TF_GL_MODE_REGULAR))
			{
				float flDist = pEntity->m_vecOrigin().DistTo(vPos);
				if (flDist < Vars::Misc::Movement::NavBot::ProjectileDangerRange.Value)
					return false;
			}
		}
	}
	return true;
}

bool CNavBotDanger::EscapeProjectiles(CTFPlayer* pLocal)
{
	if (!(Vars::Misc::Movement::NavBot::Blacklist.Value & Vars::Misc::Movement::NavBot::BlacklistEnum::Stickies) &&
		!(Vars::Misc::Movement::NavBot::Blacklist.Value & Vars::Misc::Movement::NavBot::BlacklistEnum::Projectiles))
		return false;

	// Don't interrupt higher priority tasks
	if (F::NavEngine.m_eCurrentPriority > PriorityListEnum::EscapeDanger)
		return false;

	// Check if current position is unsafe
	if (IsPositionSafe(pLocal->GetAbsOrigin(), pLocal->m_iTeamNum()))
	{
		if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::EscapeDanger)
			F::NavEngine.CancelPath();
		return false;
	}

	auto pLocalArea = F::NavEngine.GetLocalNavArea();

	// Find safe nav areas sorted by distance
	std::vector<std::pair<CNavArea*, float>> vSafeAreas;
	for (auto& tArea : F::NavEngine.GetNavFile()->m_vAreas)
	{
		// Skip current area
		if (&tArea == pLocalArea)
			continue;

		// Skip if area is blacklisted
		if (F::NavEngine.GetFreeBlacklist()->find(&tArea) != F::NavEngine.GetFreeBlacklist()->end())
			continue;

		if (IsPositionSafe(tArea.m_vCenter, pLocal->m_iTeamNum()))
		{
			float flDist = tArea.m_vCenter.DistTo(pLocal->GetAbsOrigin());
			vSafeAreas.push_back({ &tArea, flDist });
		}
	}

	// Sort by distance
	std::sort(vSafeAreas.begin(), vSafeAreas.end(),
			  [](const std::pair<CNavArea*, float>& a, const std::pair<CNavArea*, float>& b)
			  {
				  return a.second < b.second;
			  });

	// Try to path to closest safe area
	for (auto& pArea : vSafeAreas)
	{
		if (F::NavEngine.NavTo(pArea.first->m_vCenter, PriorityListEnum::EscapeDanger))
			return true;
	}

	return false;
}

bool CNavBotDanger::EscapeSpawn(CTFPlayer* pLocal)
{
	static Timer tSpawnEscapeCooldown{};

	// Don't try too often
	if (!tSpawnEscapeCooldown.Run(2.f))
		return F::NavEngine.m_eCurrentPriority == PriorityListEnum::EscapeSpawn;

	// Cancel if we're not in spawn and this is running
	if (!(F::NavEngine.GetLocalNavArea()->m_iTFAttributeFlags & (TF_NAV_SPAWN_ROOM_RED | TF_NAV_SPAWN_ROOM_BLUE | TF_NAV_SPAWN_ROOM_EXIT)))
	{
		if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::EscapeSpawn)
			F::NavEngine.CancelPath();
		return false;
	}

	// Try to find an exit
	for (auto tArea : F::NavEngine.GetNavFile()->m_vAreas)
	{
		// Skip spawn areas
		if (tArea.m_iTFAttributeFlags & (TF_NAV_SPAWN_ROOM_RED | TF_NAV_SPAWN_ROOM_BLUE | TF_NAV_SPAWN_ROOM_EXIT))
			continue;

		// Try to get there
		if (F::NavEngine.NavTo(tArea.m_vCenter, PriorityListEnum::EscapeSpawn))
			return true;
	}

	return false;
}
