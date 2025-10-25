#include "Reload.h"
#include "../NavEngine/NavEngine.h"

bool CNavBotReload::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	static Timer tReloadrunCooldown{};

	// Not reloading, do not run
	if (!G::Reloading && F::BotUtils.m_iCurrentSlot != SLOT_MELEE && G::AmmoInSlot[F::BotUtils.m_iCurrentSlot].m_iClip)
		return false;

	if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::StalkEnemies))
		return false;

	// Too high priority, so don't try
	if (F::NavEngine.m_eCurrentPriority > PriorityListEnum::RunReload)
		return false;

	// Re-calc only every once in a while
	if (!tReloadrunCooldown.Run(1.f))
		return F::NavEngine.m_eCurrentPriority == PriorityListEnum::RunReload;

	// Get closest enemy to vicheck
	CBaseEntity* pClosestEnemy = nullptr;
	float flBestDist = FLT_MAX;
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
	{
		if (!F::BotUtils.ShouldTarget(pLocal, pWeapon, pEntity->entindex()))
			continue;

		float flDist = pEntity->GetAbsOrigin().DistTo(pLocal->GetAbsOrigin());
		if (flDist > flBestDist)
			continue;

		flBestDist = flDist;
		pClosestEnemy = pEntity;
	}

	if (!pClosestEnemy)
		return false;

	Vector vVischeckPoint = pClosestEnemy->GetAbsOrigin();
	vVischeckPoint.z += PLAYER_CROUCHED_JUMP_HEIGHT;

	// Get the best non visible area
	std::pair<CNavArea*, int> tBestSpot;
	if (!F::NavBotCore.FindClosestHidingSpot(F::NavEngine.GetLocalNavArea(), vVischeckPoint, 5, tBestSpot))
		return false;

	// If we can, path
	if (F::NavEngine.NavTo(tBestSpot.first->m_vCenter, PriorityListEnum::RunReload, true, !F::NavEngine.IsPathing()))
		return true;
	return false;
}

bool CNavBotReload::RunSafe(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	static Timer tReloadrunCooldown{};
	if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::ReloadWeapons) || m_iLastReloadSlot == -1 && !G::Reloading)
	{
		if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::RunSafeReload)
			F::NavEngine.CancelPath();
		return false;
	}

	// Re-calc only every once in a while
	if (!tReloadrunCooldown.Run(1.f))
		return F::NavEngine.m_eCurrentPriority == PriorityListEnum::RunSafeReload;

	// If pathing try to avoid going to our current destination until we fully reload
	Vector vCurrentDestination;
	auto pCrumbs = F::NavEngine.GetCrumbs();
	bool bHasDestination = F::NavEngine.m_eCurrentPriority != PriorityListEnum::RunSafeReload && pCrumbs->size() > 4;
	if (bHasDestination)
		vCurrentDestination = pCrumbs->at(4).m_vPos;

	if (bHasDestination)
		vCurrentDestination.z += PLAYER_CROUCHED_JUMP_HEIGHT;
	else
	{
		// Get closest enemy to vicheck
		CBaseEntity* pClosestEnemy = nullptr;
		float flBestDist = FLT_MAX;
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
		{
			if (pEntity->IsDormant())
				continue;

			if (!F::BotUtils.ShouldTarget(pLocal, pWeapon, pEntity->entindex()))
				continue;

			float flDist = pEntity->GetAbsOrigin().DistTo(pLocal->GetAbsOrigin());
			if (flDist > flBestDist)
				continue;

			flBestDist = flDist;
			pClosestEnemy = pEntity;
		}

		if (pClosestEnemy)
		{
			bHasDestination = true;
			vCurrentDestination = pClosestEnemy->GetAbsOrigin();
			vCurrentDestination.z += PLAYER_CROUCHED_JUMP_HEIGHT;
		}
	}

	if (bHasDestination)
	{
		// Get the best non visible area
		std::pair<CNavArea*, int> tBestSpot;
		if (F::NavBotCore.FindClosestHidingSpot(F::NavEngine.GetLocalNavArea(), vCurrentDestination, 5, tBestSpot))
		{
			// If we can, path
			if (F::NavEngine.NavTo(tBestSpot.first->m_vCenter, PriorityListEnum::RunSafeReload, true, !F::NavEngine.IsPathing()))
				return true;
		}
	}

	return false;
}

int CNavBotReload::GetReloadWeaponSlot(CTFPlayer* pLocal, ClosestEnemy_t tClosestEnemy)
{
	if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::ReloadWeapons))
		return -1;

	// Priority too high
	if (F::NavEngine.m_eCurrentPriority > PriorityListEnum::Capture)
		return -1;

	// Dont try to reload in combat
	if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::StayNear && tClosestEnemy.m_flDist <= 500.f
		|| tClosestEnemy.m_flDist <= 250.f)
		return -1;

	auto pPrimaryWeapon = pLocal->GetWeaponFromSlot(SLOT_PRIMARY);
	auto pSecondaryWeapon = pLocal->GetWeaponFromSlot(SLOT_SECONDARY);
	bool bCheckPrimary = !SDK::WeaponDoesNotUseAmmo(G::SavedWepIds[SLOT_PRIMARY], G::SavedDefIndexes[SLOT_PRIMARY], false);
	bool bCheckSecondary = !SDK::WeaponDoesNotUseAmmo(G::SavedWepIds[SLOT_SECONDARY], G::SavedDefIndexes[SLOT_SECONDARY], false);

	float flDivider = F::NavEngine.m_eCurrentPriority < PriorityListEnum::StayNear && tClosestEnemy.m_flDist > 500.f ? 1.f : 3.f;

	CTFWeaponInfo* pWeaponInfo = nullptr;
	bool bWeaponCantReload = false;
	if (bCheckPrimary && pPrimaryWeapon)
	{
		pWeaponInfo = pPrimaryWeapon->GetWeaponInfo();
		bWeaponCantReload = (!pWeaponInfo || pWeaponInfo->iMaxClip1 < 0 || !pLocal->GetAmmoCount(pPrimaryWeapon->m_iPrimaryAmmoType())) && G::SavedWepIds[SLOT_PRIMARY] != TF_WEAPON_PARTICLE_CANNON && G::SavedWepIds[SLOT_PRIMARY] != TF_WEAPON_DRG_POMSON;
		if (pWeaponInfo && !bWeaponCantReload && G::AmmoInSlot[SLOT_PRIMARY].m_iClip < (pWeaponInfo->iMaxClip1 / flDivider))
			return SLOT_PRIMARY;
	}

	bool bFoundPrimaryWepInfo = pWeaponInfo;
	if (bCheckSecondary && pSecondaryWeapon && (bFoundPrimaryWepInfo || !bCheckPrimary))
	{
		pWeaponInfo = pSecondaryWeapon->GetWeaponInfo();
		bWeaponCantReload = (!pWeaponInfo || pWeaponInfo->iMaxClip1 < 0 || !pLocal->GetAmmoCount(pSecondaryWeapon->m_iPrimaryAmmoType())) && G::SavedWepIds[SLOT_SECONDARY] != TF_WEAPON_RAYGUN;
		if (pWeaponInfo && !bWeaponCantReload && G::AmmoInSlot[SLOT_SECONDARY].m_iClip < (pWeaponInfo->iMaxClip1 / flDivider))
			return SLOT_SECONDARY;
	}

	return -1;
}
