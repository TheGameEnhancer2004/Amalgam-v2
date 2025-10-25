#include "GetSupplies.h"
#include "../NavEngine/NavEngine.h"

std::vector<std::pair<bool, Vector>> CNavBotSupplies::GetEntities(CTFPlayer* pLocal, bool bHealth)
{
	auto eGroupType = bHealth ? EntityEnum::PickupHealth : EntityEnum::PickupAmmo;

	auto GetEnts = [&](std::vector<std::pair<bool, Vector>>* pOut) -> void
		{
			for (auto pEntity : H::Entities.GetGroup(eGroupType))
			{
				if (!pEntity->IsDormant())
				{
					int iEntIndex = pEntity->entindex();
					if (bHealth)
					{
						// Already cached
						if (m_sCachedHealthIndexes.count(iEntIndex))
							continue;

						m_vCachedHealthOrigins.emplace_back(false, pEntity->GetAbsOrigin());
						m_sCachedHealthIndexes.insert(iEntIndex);
					}
					// We dont cache ammopacks with physics
					else if (pEntity->GetClassID() == ETFClassID::CTFAmmoPack)
						pOut->emplace_back(false, pEntity->GetAbsOrigin());
					else
					{
						// Already cached
						if (m_sCachedAmmoIndexes.count(iEntIndex))
							continue;

						m_vCachedAmmoOrigins.emplace_back(false, pEntity->GetAbsOrigin());
						m_sCachedAmmoIndexes.insert(iEntIndex);
					}
				}
			}
		};

	std::vector<std::pair<bool, Vector>> vOrigins;
	if (bHealth)
	{
		GetEnts(nullptr);
		vOrigins = m_vCachedHealthOrigins;
	}
	else
	{
		GetEnts(&vOrigins);
		vOrigins.reserve(vOrigins.size() + m_vCachedAmmoOrigins.size());
		vOrigins.insert(vOrigins.end(), m_vCachedAmmoOrigins.begin(), m_vCachedAmmoOrigins.end());
	}

	// Sort by distance, closer is better
	auto vLocalOrigin = pLocal->GetAbsOrigin();
	std::sort(vOrigins.begin(), vOrigins.end(), [&](std::pair<bool, Vector> a, std::pair<bool, Vector> b) -> bool
			  {
				  return a.second.DistTo(vLocalOrigin) < b.second.DistTo(vLocalOrigin);
			  });

	return vOrigins;
}

std::vector<std::pair<bool, Vector>> CNavBotSupplies::GetDispensers(CTFPlayer* pLocal)
{
	auto pLocalArea = F::NavEngine.GetLocalNavArea();

	std::vector<std::pair<bool, Vector>> vOrigins;
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::BuildingTeam))
	{
		if (pEntity->GetClassID() != ETFClassID::CObjectDispenser)
			continue;

		auto pDispenser = pEntity->As<CObjectDispenser>();
		if (pDispenser->m_bCarryDeploy() || pDispenser->m_bHasSapper() || pDispenser->m_bBuilding())
			continue;

		Vector vOrigin;
		if (!F::BotUtils.GetDormantOrigin(pDispenser->entindex(), vOrigin))
			continue;

		Vec2 vOrigin2D = Vec2(vOrigin.x, vOrigin.y);
		Vector vNearestPoint = pLocalArea->GetNearestPoint(vOrigin2D);

		// This fixes the fact that players can just place dispensers in unreachable locations
		if (vNearestPoint.DistTo(vOrigin) > 300.f ||
			vNearestPoint.z - vOrigin.z > PLAYER_CROUCHED_JUMP_HEIGHT)
			continue;

		vOrigins.emplace_back( true, vOrigin );
	}

	// Sort by distance, closer is better
	auto vLocalOrigin = pLocal->GetAbsOrigin();
	std::sort(vOrigins.begin(), vOrigins.end(), [&](std::pair<bool, Vector> a, std::pair<bool, Vector> b) -> bool
			  {
				  return a.second.DistTo(vLocalOrigin) < b.second.DistTo(vLocalOrigin);
			  });

	return vOrigins;
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

	float flHealthPercent = static_cast<float>(pLocal->m_iHealth()) / pLocal->GetMaxHealth();
	// Get health when below 65%, or below 80% and just patrolling
	return flHealthPercent < 0.64f || bLowPrio && (F::NavEngine.m_eCurrentPriority <= PriorityListEnum::Patrol || F::NavEngine.m_eCurrentPriority == PriorityListEnum::LowPrioGetHealth) && flHealthPercent <= 0.80f;
}

bool CNavBotSupplies::ShouldSearchAmmo(CTFPlayer* pLocal)
{
	if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::SearchAmmo))
		return false;

	// Priority too high
	if (F::NavEngine.m_eCurrentPriority > PriorityListEnum::GetAmmo)
		return false;

	for (int i = 0; i < 2; i++)
	{
		if (!G::AmmoInSlot[i].m_bUsesAmmo)
			continue;

		int iWeaponID = G::SavedWepIds[i];
		int iReserveAmmo = G::AmmoInSlot[i].m_iReserve;
		if (iReserveAmmo <= 5 &&
			(iWeaponID == TF_WEAPON_SNIPERRIFLE ||
			iWeaponID == TF_WEAPON_SNIPERRIFLE_CLASSIC ||
			iWeaponID == TF_WEAPON_SNIPERRIFLE_DECAP))
			return true;

		int iClip = G::AmmoInSlot[i].m_iClip;
		int iMaxClip = G::AmmoInSlot[i].m_iMaxClip;
		int iMaxReserveAmmo = G::AmmoInSlot[i].m_iMaxReserve;

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

bool CNavBotSupplies::GetHealth(CUserCmd* pCmd, CTFPlayer* pLocal, bool bLowPrio)
{
	static Timer tHealthCooldown{};
	static Timer tRepathTimer{};
	const PriorityListEnum::PriorityListEnum ePriority = bLowPrio ? PriorityListEnum::LowPrioGetHealth : PriorityListEnum::GetHealth;
	if (!tHealthCooldown.Check(1.f))
		return F::NavEngine.m_eCurrentPriority == ePriority;

	// This should also check if pLocal is valid
	if (ShouldSearchHealth(pLocal, bLowPrio))
	{
		// Already pathing, only try to repath every 2s
		if (F::NavEngine.m_eCurrentPriority == ePriority && !tRepathTimer.Run(2.f))
			return true;

		auto vHealthpacks = GetEntities(pLocal, true);
		auto vDispensers = GetDispensers(pLocal);
		auto vTotalEnts = vHealthpacks;

		// Add dispensers and sort list again
		const auto vLocalOrigin = pLocal->GetAbsOrigin();
		if (!vDispensers.empty())
		{
			vTotalEnts.reserve(vHealthpacks.size() + vDispensers.size());
			vTotalEnts.insert(vTotalEnts.end(), vDispensers.begin(), vDispensers.end());
			std::sort(vTotalEnts.begin(), vTotalEnts.end(), [&](std::pair<bool, Vector> a, std::pair<bool, Vector> b) -> bool
					  {
						  return a.second.DistTo(vLocalOrigin) < b.second.DistTo(vLocalOrigin);
					  });
		}

		std::pair<bool, Vector>* pBestEntOrigin = nullptr;
		if (!vTotalEnts.empty())
			pBestEntOrigin = &vTotalEnts.front();

		if (vTotalEnts.size() > 1)
		{
			float flFirstTargetCost = F::NavEngine.GetPathCost(vLocalOrigin, pBestEntOrigin->second);
			float flSecondTargetCost = F::NavEngine.GetPathCost(vLocalOrigin, vTotalEnts.at(1).second);
			if (flSecondTargetCost < flFirstTargetCost)
				pBestEntOrigin = &vTotalEnts.at(1);
		}

		if (pBestEntOrigin)
		{
			// Check if we are close enough to the health pack to pick it up (unless its not a health pack)
			if (!pBestEntOrigin->first && pBestEntOrigin->second.DistTo(pLocal->GetAbsOrigin()) < 75.0f)
			{
				F::NavEngine.m_eCurrentPriority = ePriority;
				// Try to touch the health pack
				Vector2D vTo = { pBestEntOrigin->second.x, pBestEntOrigin->second.y };
				Vector vPathPoint = F::NavEngine.GetLocalNavArea()->GetNearestPoint(vTo);
				vPathPoint.z = pBestEntOrigin->second.z;

				// Walk towards the health pack
				SDK::WalkTo(pCmd, pLocal, vPathPoint);
				return true;
			}

			if (F::NavEngine.NavTo(pBestEntOrigin->second, ePriority, true, pBestEntOrigin->second.DistTo(vLocalOrigin) > 200.f))
				return true;
		}

		tHealthCooldown.Update();
	}
	else if (F::NavEngine.m_eCurrentPriority == ePriority)
		F::NavEngine.CancelPath();

	return false;
}

bool CNavBotSupplies::GetAmmo(CUserCmd* pCmd, CTFPlayer* pLocal, bool bForce)
{
	static Timer tRepathTimer;
	static Timer tAmmoCooldown{};
	static bool bWasForce = false;

	if (!bForce && !tAmmoCooldown.Check(1.f))
		return F::NavEngine.m_eCurrentPriority == PriorityListEnum::GetAmmo;

	if (bForce || ShouldSearchAmmo(pLocal))
	{
		// Already pathing, only try to repath every 2s
		if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::GetAmmo && !tRepathTimer.Run(2.f))
			return true;
		else
			bWasForce = false;

		auto vAmmopacks = GetEntities(pLocal);
		auto vDispensers = GetDispensers(pLocal);
		auto vTotalEnts = vAmmopacks;

		// Add dispensers and sort list again
		const auto vLocalOrigin = pLocal->GetAbsOrigin();
		if (!vDispensers.empty())
		{
			vTotalEnts.reserve(vAmmopacks.size() + vDispensers.size());
			vTotalEnts.insert(vTotalEnts.end(), vDispensers.begin(), vDispensers.end());
			std::sort(vTotalEnts.begin(), vTotalEnts.end(), [&](std::pair<bool, Vector> a, std::pair<bool, Vector> b) -> bool
					  {
						  return a.second.DistTo(vLocalOrigin) < b.second.DistTo(vLocalOrigin);
					  });
		}

		std::pair<bool, Vector>* pBestEntOrigin = nullptr;
		if (!vTotalEnts.empty())
			pBestEntOrigin = &vTotalEnts.front();

		if (vTotalEnts.size() > 1)
		{
			float flFirstTargetCost = F::NavEngine.GetPathCost(vLocalOrigin, pBestEntOrigin->second);
			float flSecondTargetCost = F::NavEngine.GetPathCost(vLocalOrigin, vTotalEnts.at(1).second);
			pBestEntOrigin = flSecondTargetCost < flFirstTargetCost ? &vTotalEnts.at(1) : pBestEntOrigin;
		}

		if (pBestEntOrigin)
		{
			// Check if we are close enough to the ammo pack to pick it up (unless its not an ammo pack)
			if (!pBestEntOrigin->first && pBestEntOrigin->second.DistTo(pLocal->GetAbsOrigin()) < 75.0f)
			{
				// Try to touch the ammo pack
				F::NavEngine.m_eCurrentPriority = PriorityListEnum::GetAmmo;
				Vector2D vTo = { pBestEntOrigin->second.x, pBestEntOrigin->second.y };
				Vector vPathPoint = F::NavEngine.GetLocalNavArea()->GetNearestPoint(vTo);
				vPathPoint.z = pBestEntOrigin->second.z;

				// Walk towards the ammo pack
				SDK::WalkTo(pCmd, pLocal, vPathPoint);
				bWasForce = bForce;
				return true;
			}

			if (F::NavEngine.NavTo(pBestEntOrigin->second, PriorityListEnum::GetAmmo, true, pBestEntOrigin->second.DistTo(vLocalOrigin) > 200.f))
			{
				bWasForce = bForce;
				return true;
			}
		}

		tAmmoCooldown.Update();
	}
	else if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::GetAmmo && !bWasForce)
		F::NavEngine.CancelPath();

	return false;
}

void CNavBotSupplies::Reset()
{
	m_vCachedHealthOrigins.clear();
	m_sCachedHealthIndexes.clear();
	m_vCachedAmmoOrigins.clear();
	m_sCachedHealthIndexes.clear();
}
