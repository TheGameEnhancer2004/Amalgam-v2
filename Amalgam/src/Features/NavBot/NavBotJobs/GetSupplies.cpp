#include "GetSupplies.h"
#include "../NavEngine/NavEngine.h"

bool CNavBotSupplies::GetSuppliesData(CTFPlayer* pLocal, bool& bClosestTaken, bool bIsAmmo)
{
	if (bIsAmmo)
	{
		// We dont cache ammopacks with physics
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::PickupAmmo))
		{
			if (pEntity->IsDormant())
				continue;

			SupplyData_t tData;
			tData.m_vOrigin = pEntity->GetAbsOrigin();
			m_vTempMain.emplace_back(tData);
		}
		m_vTempMain.reserve(m_vTempMain.size() + m_vCachedAmmoOrigins.size());
		m_vTempMain.insert(m_vTempMain.end(), m_vCachedAmmoOrigins.begin(), m_vCachedAmmoOrigins.end());
	}
	else
		m_vTempMain = m_vCachedHealthOrigins;

	if (m_vTempMain.size())
	{
		// Sort by distance, closer is better
		auto vLocalOrigin = pLocal->GetAbsOrigin();
		std::sort(m_vTempMain.begin(), m_vTempMain.end(), [&](SupplyData_t& a, SupplyData_t& b) -> bool
			{
				return a.m_vOrigin.DistTo(vLocalOrigin) < b.m_vOrigin.DistTo(vLocalOrigin);
			});


		bClosestTaken = m_vTempMain.front().m_flRespawnTime;
		return true;
	}
	return false;
}

bool CNavBotSupplies::GetDispensersData(CTFPlayer* pLocal)
{
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::BuildingTeam))
	{
		if (pEntity->GetClassID() != ETFClassID::CObjectDispenser)
			continue;

		auto pDispenser = pEntity->As<CObjectDispenser>();
		if (pDispenser->m_bCarried() || pDispenser->m_bHasSapper() || pDispenser->m_bBuilding())
			continue;

		Vector vOrigin;
		if (!F::BotUtils.GetDormantOrigin(pDispenser->entindex(), vOrigin))
			continue;

		Vec2 vOrigin2D = Vec2(vOrigin.x, vOrigin.y);
		auto pClosestArea = F::NavEngine.FindClosestNavArea(vOrigin);
		if (!pClosestArea)
			continue;

		// This fixes the fact that players can just place dispensers in unreachable locations
		Vector vNearestPoint = pClosestArea->GetNearestPoint(vOrigin2D);
		if (vNearestPoint.DistTo(vOrigin) > 300.f ||
			vOrigin.z - vNearestPoint.z > PLAYER_CROUCHED_JUMP_HEIGHT)
			continue;

		SupplyData_t tData;
		tData.m_bDispenser = true;
		tData.m_vOrigin = vOrigin;
		m_vTempDispensers.emplace_back(tData);
	}
	if (m_vTempDispensers.size())
	{
		// Sort by distance, closer is better
		auto vLocalOrigin = pLocal->GetAbsOrigin();
		std::sort(m_vTempDispensers.begin(), m_vTempDispensers.end(), [&](SupplyData_t& a, SupplyData_t& b) -> bool
			{
				return a.m_vOrigin.DistTo(vLocalOrigin) < b.m_vOrigin.DistTo(vLocalOrigin);
			});
		return true;
	}
	return false;
}

bool CNavBotSupplies::ShouldSearchHealth(CTFPlayer* pLocal, bool bLowPrio)
{
	if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::SearchHealth))
		return false;

	// Priority too high
	if (F::NavEngine.m_eCurrentPriority > PriorityListEnum::GetHealth)
		return false;

	// Check if being gradually healed in any way
	if (pLocal->m_nPlayerCond() & (1 << 21)/*TFCond_Healing*/)
		return false;

	// Get health when below 65%, or below 80% and just patrolling
	float flHealthPercent = static_cast<float>(pLocal->m_iHealth()) / pLocal->GetMaxHealth();
	return flHealthPercent < 0.64f || bLowPrio && (F::NavEngine.m_eCurrentPriority <= PriorityListEnum::Patrol || F::NavEngine.m_eCurrentPriority == PriorityListEnum::LowPrioGetHealth) && flHealthPercent <= 0.80f;
}

bool CNavBotSupplies::ShouldSearchAmmo(CTFPlayer* pLocal)
{
	if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::SearchAmmo))
		return false;

	// Priority too high
	if (F::NavEngine.m_eCurrentPriority > PriorityListEnum::GetAmmo)
		return false;

	for (int i = 0; i <= SLOT_MELEE; i++)
	{
		int iActualSlot = G::SavedWepSlots[i];
		if (iActualSlot == SLOT_MELEE || !G::AmmoInSlot[iActualSlot].m_bUsesAmmo)
			continue;

		int iWeaponID = G::SavedWepIds[iActualSlot];
		int iReserveAmmo = G::AmmoInSlot[iActualSlot].m_iReserve;
		if (iReserveAmmo <= 5 &&
			(iWeaponID == TF_WEAPON_SNIPERRIFLE ||
			iWeaponID == TF_WEAPON_SNIPERRIFLE_CLASSIC ||
			iWeaponID == TF_WEAPON_SNIPERRIFLE_DECAP))
			return true;

		int iClip = G::AmmoInSlot[iActualSlot].m_iClip;
		int iMaxClip = G::AmmoInSlot[iActualSlot].m_iMaxClip;
		int iMaxReserveAmmo = G::AmmoInSlot[iActualSlot].m_iMaxReserve;

		// If clip and reserve are both very low, definitely get ammo
		if (iMaxClip > 0 && iClip <= iMaxClip * 0.25f && iReserveAmmo <= iMaxReserveAmmo * 0.25f)
			return true;

		// Don't search for ammo if we have more than 60% of max reserve
		if (iReserveAmmo >= iMaxReserveAmmo * 0.6f)
			continue;

		// Search for ammo if we're below 33% of capacity
		if (iReserveAmmo <= iMaxReserveAmmo / 3)
			return true;
	}

	return false;
}

bool CNavBotSupplies::GetSupply(CUserCmd* pCmd, CTFPlayer* pLocal, Vector vLocalOrigin, SupplyData_t* pSupplyData, const int iPriority)
{
	float flDist = pSupplyData->m_vOrigin.DistTo(vLocalOrigin);
	if (!pSupplyData->m_bDispenser)
	{
		// Check if we are close enough to the pack to pick it up
		if (flDist < 75.0f)
		{
			Vector2D vTo = { pSupplyData->m_vOrigin.x, pSupplyData->m_vOrigin.y };
			Vector vPathPoint = F::NavEngine.GetLocalNavArea()->GetNearestPoint(vTo);
			vPathPoint.z = pSupplyData->m_vOrigin.z;

			// We are close enough to take the pack. Mark as taken
			if (pSupplyData->m_pOriginalSelfPtr && !pSupplyData->m_flRespawnTime && flDist <= 20.f)
				pSupplyData->m_pOriginalSelfPtr->m_flRespawnTime = I::GlobalVars->curtime + 10.f;

			// Try to touch the pack
			SDK::WalkTo(pCmd, pLocal, vPathPoint);
			return true;
		}
	}
	// Stand still if we are close to a dispenser 
	else if (flDist <= 150.f)
		return true;

	return F::NavEngine.NavTo(pSupplyData->m_vOrigin, PriorityListEnum::PriorityListEnum(iPriority), true, flDist > 200.f);
}

void CNavBotSupplies::UpdateTakenState()
{
	float flCurTime = I::GlobalVars->curtime;
	for (auto& pHealthData : m_vCachedHealthOrigins)
	{
		if (pHealthData.m_flRespawnTime < flCurTime)
			pHealthData.m_flRespawnTime = 0.f;
	}
	for (auto& pAmmoData : m_vCachedAmmoOrigins)
	{
		if (pAmmoData.m_flRespawnTime < flCurTime)
			pAmmoData.m_flRespawnTime = 0.f;
	}
}

bool CNavBotSupplies::Run(CUserCmd* pCmd, CTFPlayer* pLocal, int iFlags)
{
	m_vTempMain.clear();
	m_vTempDispensers.clear();
	bool bLowPrio = iFlags & GetSupplyEnum::LowPrio;
	const PriorityListEnum::PriorityListEnum ePriority = iFlags & GetSupplyEnum::Health ?
		bLowPrio ? PriorityListEnum::LowPrioGetHealth : PriorityListEnum::GetHealth :
		PriorityListEnum::GetAmmo;

	static bool bWasForce = false;
	bool bShouldForce = iFlags & GetSupplyEnum::Forced;
	bool bIsAmmo = ePriority == PriorityListEnum::GetAmmo;
	if (!bShouldForce && !(bIsAmmo ? ShouldSearchAmmo(pLocal) : ShouldSearchHealth(pLocal, bLowPrio)))
	{
		// Cancel pathing if we no longer need to get anything
		if (F::NavEngine.m_eCurrentPriority == ePriority && (!bIsAmmo || !bWasForce))
			F::NavEngine.CancelPath();
		return false;
	}

	static Timer tCooldownTimer{};
	if (!bShouldForce && !tCooldownTimer.Check(1.f))
		return F::NavEngine.m_eCurrentPriority == ePriority;

	// Already pathing, only try to repath every 2s
	static Timer tRepathCooldownTimer{};
	if (F::NavEngine.m_eCurrentPriority == ePriority && !tRepathCooldownTimer.Run(2.f))
		return true;

	UpdateTakenState();
	bWasForce = false;
	bool bClosestSupplyWasTaken = false;
	bool bGotSupplies = GetSuppliesData(pLocal, bClosestSupplyWasTaken, bIsAmmo);
	bool bGotDispensers = GetDispensersData(pLocal);
	if (!bGotSupplies && !bGotDispensers)
	{
		tCooldownTimer.Update();
		return false;
	}

	const auto vLocalOrigin = pLocal->GetAbsOrigin();
	bool bHasCloseDispenser = false;
	if (bGotDispensers)
	{
		bHasCloseDispenser = m_vTempDispensers.front().m_vOrigin.DistTo(vLocalOrigin) <= 1600.f;
		m_vTempMain.reserve(m_vTempMain.size() + m_vTempDispensers.size());
		m_vTempMain.insert(m_vTempMain.end(), m_vTempDispensers.begin(), m_vTempDispensers.end());
		std::sort(m_vTempMain.begin(), m_vTempMain.end(), [&](SupplyData_t& a, SupplyData_t& b) -> bool
			{
				return a.m_vOrigin.DistTo(vLocalOrigin) < b.m_vOrigin.DistTo(vLocalOrigin);
			});
	}

	bool bWaitForRespawn = false;
	SupplyData_t* pBest = nullptr, * pSecondBest = nullptr;
	if (bClosestSupplyWasTaken)
	{
		for (auto& pSupplyData : m_vTempMain)
		{
			if (pSupplyData.m_flRespawnTime)
				continue;

			// Too far
			if (pSupplyData.m_vOrigin.DistTo(vLocalOrigin) > 800.f)
			{
				// If no other target was found wait for this entity to respawn
				bWaitForRespawn = !pBest;
				break;
			}

			if (pBest)
			{
				pSecondBest = &pSupplyData;
				break;
			}
			else
				pBest = &pSupplyData;
		}
		// Check again if its worth going for this entity
		if (!bWaitForRespawn && pBest && F::NavEngine.GetPathCost(vLocalOrigin, pBest->m_vOrigin) >= 2000.f)
			pBest = pSecondBest = nullptr;
	}

	if (!pBest)
	{
		pBest = &m_vTempMain.front();
		if (bHasCloseDispenser)
		{
			if (bClosestSupplyWasTaken)
				pBest = &m_vTempDispensers.front();
		}
		else if (m_vTempMain.size() > 1)
			pSecondBest = &m_vTempMain.at(1);
	}

	// Check if going for the other one will take less time
	if (pSecondBest)
	{
		float flFirstTargetCost = F::NavEngine.GetPathCost(vLocalOrigin, pBest->m_vOrigin);
		float flSecondTargetCost = F::NavEngine.GetPathCost(vLocalOrigin, pSecondBest->m_vOrigin);
		if (flSecondTargetCost < flFirstTargetCost)
			pBest = pSecondBest;
	}

	if (pBest && GetSupply(pCmd, pLocal, vLocalOrigin, pBest, ePriority))
	{
		bWasForce = bShouldForce;
		return true;
	}

	tCooldownTimer.Update();
	return false;
}

void CNavBotSupplies::AddCachedSupplyOrigin(Vector vOrigin, bool bHealth)
{
	SupplyData_t tData;
	tData.m_vOrigin = vOrigin;
	if (bHealth)
	{
		m_vCachedHealthOrigins.push_back(tData);
		m_vCachedHealthOrigins.back().m_pOriginalSelfPtr = &m_vCachedHealthOrigins.back();
	}
	else
	{
		m_vCachedAmmoOrigins.push_back(tData);
		m_vCachedAmmoOrigins.back().m_pOriginalSelfPtr = &m_vCachedAmmoOrigins.back();
	}
}

void CNavBotSupplies::ResetCachedOrigins()
{
	m_vCachedHealthOrigins.clear();
	m_vCachedAmmoOrigins.clear();
}

void CNavBotSupplies::ResetTemp()
{
	m_vTempMain.clear();
	m_vTempDispensers.clear();
}
