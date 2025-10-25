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

bool CNavBotEngineer::NavToSentrySpot()
{
	static Timer tWaitUntilPathSentryTimer;

	// Wait a bit before pathing again
	if (!tWaitUntilPathSentryTimer.Run(0.3f))
		return false;

	// Try to nav to our existing sentry spot
	if (m_pMySentryGun && !m_pMySentryGun->m_bPlacing())
	{
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

	// Max 10 attempts
	for (int iAttempts = 0; iAttempts < 10 && iAttempts < m_vBuildingSpots.size(); ++iAttempts)
	{
		// Get a semi-random building spot to still keep distance preferrance
		auto iRandomOffset = SDK::RandomInt(0, std::min(3, (int)m_vBuildingSpots.size()));

		Vector vRandom;
		// Wrap around
		if (iAttempts - iRandomOffset < 0)
			vRandom = m_vBuildingSpots[m_vBuildingSpots.size() + (iAttempts - iRandomOffset)];
		else
			vRandom = m_vBuildingSpots[iAttempts - iRandomOffset];

		// Try to nav there
		if (F::NavEngine.NavTo(vRandom, PriorityListEnum::Engineer))
		{
			m_vCurrentBuildingSpot = vRandom;
			return true;
		}
	}
	return false;
}

bool CNavBotEngineer::BuildBuilding(CUserCmd* pCmd, CTFPlayer* pLocal, ClosestEnemy_t tClosestEnemy, bool bDispenser)
{
	// Blacklist this spot and refresh the building spots
	if (m_iBuildAttempts >= 15)
	{
		(*F::NavEngine.GetFreeBlacklist())[F::NavEngine.GetLocalNavArea()] = BlacklistReasonEnum::BadBuildSpot;
		RefreshBuildingSpots(pLocal, tClosestEnemy, true);
		m_vCurrentBuildingSpot.Zero();
		m_iBuildAttempts = 0;
		return false;
	}

	// Make sure we have right amount of metal
	int iRequiredMetal = (bDispenser || G::SavedDefIndexes[SLOT_MELEE] == Engi_t_TheGunslinger) ? 100 : 130;
	if (pLocal->m_iMetalCount() < iRequiredMetal)
		return F::NavBotSupplies.GetAmmo(pCmd, pLocal, true);

	m_sEngineerTask = std::format(L"Build {}", bDispenser ? L"dispenser" : L"sentry");
	static float flPrevYaw = 0.0f;
	// Try to build! we are close enough
	if (!m_vCurrentBuildingSpot.IsZero() && m_vCurrentBuildingSpot.DistTo(pLocal->GetAbsOrigin()) <= (bDispenser ? 500.f : 200.f))
	{
		// TODO: Rotate our angle to a valid building spot ? also rotate building itself to face enemies ?
		pCmd->viewangles.x = 20.0f;
		pCmd->viewangles.y = flPrevYaw += 2.0f;

		// Gives us 4 1/2 seconds to build
		static Timer tAttemptTimer;
		if (tAttemptTimer.Run(0.3f))
			m_iBuildAttempts++;

		auto pBuilding = bDispenser ? m_pMyDispenser->As<CBaseObject>() : m_pMySentryGun;
		if (!pLocal->m_bCarryingObject())
		{
			static Timer command_timer;
			if (command_timer.Run(0.1f))
				I::EngineClient->ClientCmd_Unrestricted(std::format("build {}", bDispenser ? 0 : 2).c_str());
		}
		//else if (pBuilding->m_bServerOverridePlacement()) // Can place
		pCmd->buttons |= IN_ATTACK;
		return true;
	}
	else
	{
		flPrevYaw = 0.0f;
		return NavToSentrySpot();
	}

	return false;
}

bool CNavBotEngineer::SmackBuilding(CUserCmd* pCmd, CTFPlayer* pLocal, CBaseObject* pBuilding)
{
	if (!pBuilding)
		return false;

	if (!pLocal->m_iMetalCount())
		return F::NavBotSupplies.GetAmmo(pCmd, pLocal, true);

	m_sEngineerTask = std::format(L"Smack {}", pBuilding->GetClassID() == ETFClassID::CObjectDispenser ? L"dispenser" : L"sentry");

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

				if (tArea.m_iTFAttributeFlags & TF_NAV_SENTRY_SPOT)
					m_vBuildingSpots.emplace_back(tArea.m_vCenter);
				else
				{
					for (auto tHidingSpot : tArea.m_vHidingSpots)
					{
						if (tHidingSpot.HasGoodCover())
							m_vBuildingSpots.emplace_back(tHidingSpot.m_vPos);
					}
				}
			}
			// Sort by distance to nearest, lower is better
			// TODO: This isn't really optimal, need a dif way to where it is a good distance from enemies but also bots dont build in the same spot
			std::sort(m_vBuildingSpots.begin(), m_vBuildingSpots.end(),
					  [&](Vector a, Vector b) -> bool
					  {
						  if (bHasGunslinger)
						  {
							  auto flADist = a.DistTo(vTarget);
							  auto flBDist = b.DistTo(vTarget);

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
							  return a.DistTo(vTarget) < b.DistTo(vTarget);
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
	}
}

void CNavBotEngineer::Reset()
{
	m_pMySentryGun = nullptr;
	m_pMyDispenser = nullptr;
	m_iBuildAttempts = 0;
	m_vBuildingSpots.clear();
	m_vCurrentBuildingSpot.Zero();
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
		m_sEngineerTask.clear();
		return false;
	}

	// Already have a sentry
	if (m_pMySentryGun && !m_pMySentryGun->m_bPlacing())
	{
		if (G::SavedDefIndexes[SLOT_MELEE] == Engi_t_TheGunslinger)
		{

			// Too far away, destroy it
			// BUG Ahead, building isnt destroyed lol
			if (m_pMySentryGun->GetAbsOrigin().DistTo(pLocal->GetAbsOrigin()) >= 1800.f)
			{
				// If we have a valid building
				I::EngineClient->ClientCmd_Unrestricted("destroy 2");
			}
			// Return false so we run another task
			return false;
		}
		else
		{
			// Try to smack sentry first
			if (BuildingNeedsToBeSmacked(m_pMySentryGun))
				return SmackBuilding(pCmd, pLocal, m_pMySentryGun);
			else
			{
				// We put dispenser by sentry
				if (!m_pMyDispenser)
					return BuildBuilding(pCmd, pLocal, tClosestEnemy, true);
				else
				{
					// We already have a dispenser, see if it needs to be smacked
					if (BuildingNeedsToBeSmacked(m_pMyDispenser))
						return SmackBuilding(pCmd, pLocal, m_pMyDispenser);

					// Return false so we run another task
					return false;
				}
			}

		}
	}
	// Try to build a sentry
	return BuildBuilding(pCmd, pLocal, tClosestEnemy, false);
}
