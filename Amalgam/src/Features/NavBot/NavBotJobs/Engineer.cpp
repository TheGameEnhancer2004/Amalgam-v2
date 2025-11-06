#include "Engineer.h"
#include "GetSupplies.h"
#include "../NavEngine/NavEngine.h"
#include "../NavEngine/Controllers/FlagController/FlagController.h"

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

	// Don't overwrite current nav
	if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::Engineer)
		return false;

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
		if (F::NavEngine.NavTo(tRandomSpot.m_vPos, PriorityListEnum::Engineer))
		{
			m_tCurrentBuildingSpot = tRandomSpot;
			return true;
		}
	}
	return false;
}

bool CNavBotEngineer::BuildBuilding(CUserCmd* pCmd, CTFPlayer* pLocal, ClosestEnemy_t tClosestEnemy, bool bDispenser)
{
	m_eTaskStage = bDispenser ? EngineerTaskStageEnum::BuildDispenser : EngineerTaskStageEnum::BuildSentry;

	// Blacklist this spot and refresh the building spots
	if (m_iBuildAttempts >= 15)
	{
		(*F::NavEngine.GetFreeBlacklist())[F::NavEngine.GetLocalNavArea()] = BlacklistReasonEnum::BadBuildSpot;
		RefreshBuildingSpots(pLocal, tClosestEnemy, true);
		m_tCurrentBuildingSpot = {};
		m_iBuildAttempts = 0;
		return false;
	}

	// Make sure we have right amount of metal
	int iRequiredMetal = (bDispenser || G::SavedDefIndexes[SLOT_MELEE] == Engi_t_TheGunslinger) ? 100 : 130;
	if (pLocal->m_iMetalCount() < iRequiredMetal)
		return F::NavBotSupplies.Run(pCmd, pLocal, GetSupplyEnum::Ammo | GetSupplyEnum::Forced);
	
	static float flPrevYaw = 0.0f;
	// Try to build! we are close enough
	if (m_tCurrentBuildingSpot.m_flDistanceToTarget != FLT_MAX && m_tCurrentBuildingSpot.m_vPos.DistTo(pLocal->GetAbsOrigin()) <= (bDispenser ? 500.f : 200.f))
	{
		// TODO: Rotate our angle to a valid building spot ? also rotate building itself to face enemies ?
		pCmd->viewangles.x = 20.0f;
		pCmd->viewangles.y = flPrevYaw += 2.0f;

		// Gives us 4 1/2 seconds to build
		static Timer tAttemptTimer;
		if (tAttemptTimer.Run(0.3f))
			m_iBuildAttempts++;

		if (!pLocal->m_bCarryingObject())
		{
			static Timer command_timer;
			if (command_timer.Run(0.1f))
				I::EngineClient->ClientCmd_Unrestricted(std::format("build {}", bDispenser ? 0 : 2).c_str());
		}

		pCmd->buttons |= IN_ATTACK;
		return true;
	}
	else
	{
		flPrevYaw = 0.0f;
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
			pCmd->viewangles = Math::CalcAngle(pLocal->GetEyePosition(), pBuilding->GetCenter());
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
		Vector vTarget;
		if (F::FlagController.GetSpawnPosition(pLocal->m_iTeamNum(), vTarget) ||
			F::BotUtils.GetDormantOrigin(tClosestEnemy.m_iEntIdx, vTarget))
		{
			// Search all nav areas for valid spots
			for (auto& tArea : F::NavEngine.GetNavFile()->m_vAreas)
			{
				if (BlacklistedFromBuilding(&tArea))
					continue;

				if (tArea.m_iTFAttributeFlags & (TF_NAV_SPAWN_ROOM_RED | TF_NAV_SPAWN_ROOM_BLUE | TF_NAV_SPAWN_ROOM_EXIT))
					continue;

				float flDistToTarget = tArea.m_vCenter.DistTo(vTarget);

				// Skip far away spots
				if (flDistToTarget >= 4000.f)
					continue;

				if (tArea.m_iTFAttributeFlags & TF_NAV_SENTRY_SPOT)
					m_vBuildingSpots.emplace_back(flDistToTarget, tArea.m_vCenter);
				else
				{
					for (auto tHidingSpot : tArea.m_vHidingSpots)
					{
						if (tHidingSpot.HasGoodCover())
							m_vBuildingSpots.emplace_back(flDistToTarget, tHidingSpot.m_vPos);
					}
				}
			}
			// Sort by distance to nearest, lower is better
			// TODO: This isn't really optimal, need a dif way to where it is a good distance from enemies but also bots dont build in the same spot
			std::sort(m_vBuildingSpots.begin(), m_vBuildingSpots.end(),
					  [&](BuildingSpot_t& a, BuildingSpot_t& b) -> bool
					  {
						  if (bHasGunslinger)
						  {
							  auto flADist = a.m_flDistanceToTarget;
							  auto flBDist = b.m_flDistanceToTarget;

							  // Penalty for being in danger ranges
							  if (flADist + 100.0f < 300.0f)
								  flADist += 4000.0f;
							  if (flBDist + 100.0f < 300.0f)
								  flBDist += 4000.0f;

							  if (flADist + 1000.0f < 500.0f)
								  flADist += 1500.0f;
							  if (flBDist + 1000.0f < 500.0f)
								  flBDist += 1500.0f;

							  return flADist < flBDist;
						  }
						  else
							  return a.m_flDistanceToTarget < b.m_flDistanceToTarget;
					  });
		}
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
	m_vBuildingSpots.clear();
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
