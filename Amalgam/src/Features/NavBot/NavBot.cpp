#include "NavBot.h"
#include "NavEngine/Controllers/CPController/CPController.h"
#include "NavEngine/Controllers/FlagController/FlagController.h"
#include "NavEngine/Controllers/PLController/PLController.h"
#include "NavEngine/Controllers/Controller.h"
#include "../Players/PlayerUtils.h"
#include "../Misc/NamedPipe/NamedPipe.h"
#include "../Aimbot/AimbotGlobal/AimbotGlobal.h"
#include "../Ticks/Ticks.h"
#include "../PacketManip/FakeLag/FakeLag.h"
#include "../Misc/Misc.h"
#include "../Simulation/MovementSimulation/MovementSimulation.h"
#include "../CritHack/CritHack.h"
#include <unordered_set>
#include <cfloat>
#include <cmath>

bool CNavBot::ShouldSearchHealth(CTFPlayer* pLocal, bool bLowPrio)
{
	if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::SearchHealth))
		return false;

	// Priority too high
	if (F::NavEngine.current_priority > health)
		return false;

	// Check if being gradually healed in any way
	if (pLocal->m_nPlayerCond() & (1 << 21)/*TFCond_Healing*/)
		return false;

	float flHealthPercent = static_cast<float>(pLocal->m_iHealth()) / pLocal->GetMaxHealth();
	// Get health when below 65%, or below 80% and just patrolling
	return flHealthPercent < 0.64f || bLowPrio && (F::NavEngine.current_priority <= patrol || F::NavEngine.current_priority == lowprio_health) && flHealthPercent <= 0.80f;
}

bool CNavBot::ShouldSearchAmmo(CTFPlayer* pLocal)
{
	if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::SearchAmmo))
		return false;

	// Priority too high
	if (F::NavEngine.current_priority > ammo)
		return false;

	for (int i = 0; i < 2; i++)
	{
		auto pWeapon = pLocal->GetWeaponFromSlot(i);
		if (!pWeapon || SDK::WeaponDoesNotUseAmmo(pWeapon))
			continue;

		int iWepID = pWeapon->GetWeaponID();
		int iMaxClip = pWeapon->GetWeaponInfo() ? pWeapon->GetWeaponInfo()->iMaxClip1 : 0;
		int iCurClip = pWeapon->m_iClip1();
		
		if ((iWepID == TF_WEAPON_SNIPERRIFLE ||
			iWepID == TF_WEAPON_SNIPERRIFLE_CLASSIC ||
			iWepID == TF_WEAPON_SNIPERRIFLE_DECAP) &&
			pLocal->GetAmmoCount(pWeapon->m_iPrimaryAmmoType()) <= 5)
			return true;

		if (!pWeapon->HasAmmo())
			return true;

		int iMaxAmmo = SDK::GetWeaponMaxReserveAmmo(iWepID, pWeapon->m_iItemDefinitionIndex());
		if (!iMaxAmmo)
			continue;

		// Reserve ammo
		int iResAmmo = pLocal->GetAmmoCount(pWeapon->m_iPrimaryAmmoType());
		
		// If clip and reserve are both very low, definitely get ammo
		if (iMaxClip > 0 && iCurClip <= iMaxClip * 0.25f && iResAmmo <= iMaxAmmo * 0.25f)
			return true;
			
		// Don't search for ammo if we have more than 60% of max reserve
		if (iResAmmo >= iMaxAmmo * 0.6f)
			continue;
			
		// Search for ammo if we're below 33% of capacity
		if (iResAmmo <= iMaxAmmo / 3)
			return true;
	}

	return false;
}

int CNavBot::ShouldTarget(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, int iPlayerIdx)
{
	auto pEntity = I::ClientEntityList->GetClientEntity(iPlayerIdx)->As<CBaseEntity>();
	if (!pEntity || !pEntity->IsPlayer())
		return -1;

	auto pPlayer = pEntity->As<CTFPlayer>();
	if (!pPlayer->IsAlive() || pPlayer == pLocal)
		return -1;
#ifdef TEXTMODE
	if (auto pResource = H::Entities.GetResource(); pResource && F::NamedPipe.IsLocalBot(pResource->m_iAccountID(iPlayerIdx)))
		return 0;
#endif

	if (F::PlayerUtils.IsIgnored(iPlayerIdx))
		return 0;

	if (Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Friends && H::Entities.IsFriend(iPlayerIdx)
		|| Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Party && H::Entities.InParty(iPlayerIdx)
		|| Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Invulnerable && pPlayer->IsInvulnerable() && G::SavedDefIndexes[SLOT_MELEE] != Heavy_t_TheHolidayPunch
		|| Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Invisible && pPlayer->m_flInvisibility() && pPlayer->m_flInvisibility() >= Vars::Aimbot::General::IgnoreInvisible.Value / 100.f
		|| Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::DeadRinger && pPlayer->m_bFeignDeathReady()
		|| Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Taunting && pPlayer->IsTaunting()
		|| Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Disguised && pPlayer->InCond(TF_COND_DISGUISED))
		return 0;

	if (pPlayer->m_iTeamNum() == pLocal->m_iTeamNum())
		return 0;

	if (Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Vaccinator)
	{
		switch (SDK::GetWeaponType(pWeapon))
		{
		case EWeaponType::HITSCAN:
			if (pPlayer->InCond(TF_COND_MEDIGUN_UBER_BULLET_RESIST) && SDK::AttribHookValue(0, "mod_pierce_resists_absorbs", pWeapon) != 0)
				return 0;
			break;
		case EWeaponType::PROJECTILE:
			if (pPlayer->InCond(TF_COND_MEDIGUN_UBER_FIRE_RESIST) && (G::SavedWepIds[SLOT_PRIMARY] == TF_WEAPON_FLAMETHROWER && G::SavedWepIds[SLOT_SECONDARY] == TF_WEAPON_FLAREGUN))
				return 0;
			else if (pPlayer->InCond(TF_COND_MEDIGUN_UBER_BULLET_RESIST) && G::SavedWepIds[SLOT_PRIMARY] == TF_WEAPON_COMPOUND_BOW)
				return 0;
			else if (pPlayer->InCond(TF_COND_MEDIGUN_UBER_BLAST_RESIST))
				return 0;
		}
	}

	return 1;
}

int CNavBot::ShouldTargetBuilding(CTFPlayer* pLocal, int iEntIdx)
{
	auto pEntity = I::ClientEntityList->GetClientEntity(iEntIdx)->As<CBaseEntity>();
	if (!pEntity || !pEntity->IsBuilding())
		return -1;

	auto pBuilding = pEntity->As<CBaseObject>();
	if (!(Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Sentry) && pBuilding->IsSentrygun()
		|| !(Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Dispenser) && pBuilding->IsDispenser()
		|| !(Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Teleporter) && pBuilding->IsTeleporter())
		return 1;

	if (pLocal->m_iTeamNum() == pBuilding->m_iTeamNum())
		return 1;

	auto pOwner = pBuilding->m_hBuilder().Get();
	if (pOwner)
	{
		if (F::PlayerUtils.IsIgnored(pOwner->entindex()))
			return 0;

		if (Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Friends && H::Entities.IsFriend(pOwner->entindex())
			|| Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Party && H::Entities.InParty(pOwner->entindex()))
			return 0;
	}

	return 1;
}

bool CNavBot::ShouldAssist(CTFPlayer* pLocal, int iTargetIdx)
{
	auto pEntity = I::ClientEntityList->GetClientEntity(iTargetIdx);
	if (!pEntity || pEntity->As<CBaseEntity>()->m_iTeamNum() != pLocal->m_iTeamNum())
		return false;

	if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::HelpFriendlyCaptureObjectives))
		return true;

	if (F::PlayerUtils.IsIgnored(iTargetIdx)
		|| H::Entities.InParty(iTargetIdx)
		|| H::Entities.IsFriend(iTargetIdx))
		return true;

	return false;
}

std::vector<CObjectDispenser*> CNavBot::GetDispensers(CTFPlayer* pLocal)
{
	std::vector<CObjectDispenser*> vDispensers;
	for (auto pEntity : H::Entities.GetGroup(EGroupType::BUILDINGS_TEAMMATES))
	{
		if (pEntity->GetClassID() != ETFClassID::CObjectDispenser)
			continue;

		auto pDispenser = pEntity->As<CObjectDispenser>();
		if (pDispenser->m_bCarryDeploy() || pDispenser->m_bHasSapper() || pDispenser->m_bBuilding())
			continue;

		auto vOrigin = F::NavParser.GetDormantOrigin(pDispenser->entindex());
		if (!vOrigin)
			continue;

		Vec2 vOrigin2D = Vec2(vOrigin->x, vOrigin->y);
		auto pLocalNav = F::NavEngine.findClosestNavSquare(*vOrigin);

		// This fixes the fact that players can just place dispensers in unreachable locations
		if (pLocalNav->getNearestPoint(vOrigin2D).DistToSqr(*vOrigin) > pow(300.0f, 2) ||
			pLocalNav->getNearestPoint(vOrigin2D).z - vOrigin->z > PLAYER_JUMP_HEIGHT)
			continue;

		vDispensers.push_back(pDispenser);
	}

	// Sort by distance, closer is better
	auto vLocalOrigin = pLocal->GetAbsOrigin();
	std::sort(vDispensers.begin(), vDispensers.end(), [&](CBaseEntity* a, CBaseEntity* b) -> bool
			  {
				  return F::NavParser.GetDormantOrigin(a->entindex())->DistToSqr(vLocalOrigin) < F::NavParser.GetDormantOrigin(b->entindex())->DistToSqr(vLocalOrigin);
			  });

	return vDispensers;
}

std::vector<CBaseEntity*> CNavBot::GetEntities(CTFPlayer* pLocal, bool bHealth)
{
	EGroupType eGroupType = bHealth ? EGroupType::PICKUPS_HEALTH : EGroupType::PICKUPS_AMMO;

	std::vector<CBaseEntity*> vEntities;
	for (auto pEntity : H::Entities.GetGroup(eGroupType))
	{
		if (!pEntity->IsDormant())
			vEntities.push_back(pEntity);
	}

	// Sort by distance, closer is better
	auto vLocalOrigin = pLocal->GetAbsOrigin();
	std::sort(vEntities.begin(), vEntities.end(), [&](CBaseEntity* a, CBaseEntity* b) -> bool
			  {
				  return a->GetAbsOrigin().DistToSqr(vLocalOrigin) < b->GetAbsOrigin().DistToSqr(vLocalOrigin);
			  });

	return vEntities;
}

bool CNavBot::GetHealth(CUserCmd* pCmd, CTFPlayer* pLocal, bool bLowPrio)
{
	const Priority_list ePriority = bLowPrio ? lowprio_health : health;
	static Timer tHealthCooldown{};
	static Timer tRepathTimer;
	if (!tHealthCooldown.Check(1.f))
		return F::NavEngine.current_priority == ePriority;

	// This should also check if pLocal is valid
	if (ShouldSearchHealth(pLocal, bLowPrio))
	{
		// Already pathing, only try to repath every 2s
		if (F::NavEngine.current_priority == ePriority && !tRepathTimer.Run(2.f))
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
			std::sort(vTotalEnts.begin(), vTotalEnts.end(), [&](CBaseEntity* a, CBaseEntity* b) -> bool
					  {
						  return a->GetAbsOrigin().DistToSqr(vLocalOrigin) < b->GetAbsOrigin().DistToSqr(vLocalOrigin);
					  });
		}

		CBaseEntity* pBestEnt = nullptr;
		if (!vTotalEnts.empty())
			pBestEnt = vTotalEnts.front();

		if (vTotalEnts.size() > 1)
		{
			F::NavEngine.navTo(pBestEnt->GetAbsOrigin(), ePriority, true, pBestEnt->GetAbsOrigin().DistToSqr(vLocalOrigin) > pow(200.0f, 2));
			auto iFirstTargetPoints = F::NavEngine.crumbs.size();
			F::NavEngine.cancelPath();

			F::NavEngine.navTo(vTotalEnts.at(1)->GetAbsOrigin(), ePriority, true, vTotalEnts.at(1)->GetAbsOrigin().DistToSqr(vLocalOrigin) > pow(200.0f, 2));
			auto iSecondTargetPoints = F::NavEngine.crumbs.size();
			F::NavEngine.cancelPath();

			pBestEnt = iSecondTargetPoints < iFirstTargetPoints ? vTotalEnts.at(1) : pBestEnt;
		}

		if (pBestEnt)
		{
			if (F::NavEngine.navTo(pBestEnt->GetAbsOrigin(), ePriority, true, pBestEnt->GetAbsOrigin().DistToSqr(vLocalOrigin) > pow(200.0f, 2)))
			{
				// Check if we are close enough to the health pack to pick it up (unless its not a health pack)
				if (pBestEnt->GetAbsOrigin().DistToSqr(pLocal->GetAbsOrigin()) < pow(75.0f, 2) && !pBestEnt->IsDispenser())
				{
					// Try to touch the health pack
					auto pLocalNav = F::NavEngine.findClosestNavSquare(pLocal->GetAbsOrigin());
					if (pLocalNav)
					{
						Vector2D vTo = { pBestEnt->GetAbsOrigin().x, pBestEnt->GetAbsOrigin().y };
						Vector vPathPoint = pLocalNav->getNearestPoint(vTo);
						vPathPoint.z = pBestEnt->GetAbsOrigin().z;

						// Walk towards the health pack
						SDK::WalkTo(pCmd, pLocal, vPathPoint);
					}
				}
				return true;
			}
		}

		tHealthCooldown.Update();
	}
	else if (F::NavEngine.current_priority == ePriority)
		F::NavEngine.cancelPath();

	return false;
}

bool CNavBot::GetAmmo(CUserCmd* pCmd, CTFPlayer* pLocal, bool bForce)
{
	static Timer tRepathTimer;
	static Timer tAmmoCooldown{};
	static bool bWasForce = false;

	if (!bForce && !tAmmoCooldown.Check(1.f))
		return F::NavEngine.current_priority == ammo;

	if (bForce || ShouldSearchAmmo(pLocal))
	{
		// Already pathing, only try to repath every 2s
		if (F::NavEngine.current_priority == ammo && !tRepathTimer.Run(2.f))
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
			std::sort(vTotalEnts.begin(), vTotalEnts.end(), [&](CBaseEntity* a, CBaseEntity* b) -> bool
					  {
						  return a->GetAbsOrigin().DistToSqr(vLocalOrigin) < b->GetAbsOrigin().DistToSqr(vLocalOrigin);
					  });
		}

		CBaseEntity* pBestEnt = nullptr;
		if (!vTotalEnts.empty())
			pBestEnt = vTotalEnts.front();

		if (vTotalEnts.size() > 1)
		{
			F::NavEngine.navTo(pBestEnt->GetAbsOrigin(), ammo, true, pBestEnt->GetAbsOrigin().DistToSqr(vLocalOrigin) > pow(200.0f, 2));
			const auto iFirstTargetPoints = F::NavEngine.crumbs.size();
			F::NavEngine.cancelPath();

			F::NavEngine.navTo(vTotalEnts.at(1)->GetAbsOrigin(), ammo, true, vTotalEnts.at(1)->GetAbsOrigin().DistToSqr(vLocalOrigin) > pow(200.0f, 2));
			const auto iSecondTargetPoints = F::NavEngine.crumbs.size();
			F::NavEngine.cancelPath();
			pBestEnt = iSecondTargetPoints < iFirstTargetPoints ? vTotalEnts.at(1) : pBestEnt;
		}

		if (pBestEnt)
		{
			if (F::NavEngine.navTo(pBestEnt->GetAbsOrigin(), ammo, true, pBestEnt->GetAbsOrigin().DistToSqr(vLocalOrigin) > pow(200.0f, 2)))
			{
				// Check if we are close enough to the ammo pack to pick it up (unless its not an ammo pack)
				if (pBestEnt->GetAbsOrigin().DistToSqr(pLocal->GetAbsOrigin()) < pow(75.0f, 2) && !pBestEnt->IsDispenser())
				{
					// Try to touch the ammo pack
					auto pLocalNav = F::NavEngine.findClosestNavSquare(pLocal->GetAbsOrigin());
					if (pLocalNav)
					{
						Vector2D vTo = { pBestEnt->GetAbsOrigin().x, pBestEnt->GetAbsOrigin().y };
						Vector vPathPoint = pLocalNav->getNearestPoint(vTo);
						vPathPoint.z = pBestEnt->GetAbsOrigin().z;

						// Walk towards the ammo pack
						SDK::WalkTo(pCmd, pLocal, vPathPoint);
					}
				}
				bWasForce = bForce;
				return true;
			}
		}

		tAmmoCooldown.Update();
	}
	else if (F::NavEngine.current_priority == ammo && !bWasForce)
		F::NavEngine.cancelPath();

	return false;
}

static Timer tRefreshSniperspotsTimer{};
void CNavBot::RefreshSniperSpots()
{
	if (!tRefreshSniperspotsTimer.Run(60.f))
		return;

	m_vSniperSpots.clear();

	// Vector of exposed spots to nav to in case we find no sniper spots
	std::vector<Vector> vExposedSpots;
	// Search all nav areas for valid sniper spots
	for (auto tArea : F::NavEngine.getNavFile()->m_areas)
	{
		// Dont use spawn as a snipe spot
		if (tArea.m_TFattributeFlags & (TF_NAV_SPAWN_ROOM_BLUE | TF_NAV_SPAWN_ROOM_RED | TF_NAV_SPAWN_ROOM_EXIT))
			continue;

		for (auto tHidingSpot : tArea.m_hidingSpots)
		{
			// Spots actually marked for sniping
			if (tHidingSpot.IsGoodSniperSpot() || tHidingSpot.IsIdealSniperSpot() || tHidingSpot.HasGoodCover())
			{
				m_vSniperSpots.emplace_back(tHidingSpot.m_pos);
				continue;
			}

			if (tHidingSpot.IsExposed())
				vExposedSpots.emplace_back(tHidingSpot.m_pos);
		}
	}

	// If we have no sniper spots, just use nav areas marked as exposed. They're good enough for sniping.
	if (m_vSniperSpots.empty() && !vExposedSpots.empty())
		m_vSniperSpots = vExposedSpots;
}

ClosestEnemy_t CNavBot::GetNearestPlayerDistance(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	float flMinDist = FLT_MAX;
	int iBest = -1;

	if (pLocal && pWeapon)
	{
		for (auto pEntity : H::Entities.GetGroup(EGroupType::PLAYERS_ENEMIES))
		{
			auto pPlayer = pEntity->As<CTFPlayer>();
			if (!ShouldTarget(pLocal, pWeapon, pPlayer->entindex()))
				continue;

			auto vOrigin = F::NavParser.GetDormantOrigin(pPlayer->entindex());
			if (!vOrigin)
				continue;

			auto flDist = pLocal->GetAbsOrigin().DistToSqr(*vOrigin);
			if (flDist >= flMinDist)
				continue;

			flMinDist = flDist;
			iBest = pPlayer->entindex();
		}
	}
	return ClosestEnemy_t{ iBest, flMinDist };
}

bool CNavBot::IsEngieMode(CTFPlayer* pLocal)
{
	return Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::AutoEngie && (Vars::Aimbot::AutoEngie::AutoRepair.Value || Vars::Aimbot::AutoEngie::AutoUpgrade.Value) && pLocal && pLocal->IsAlive() && pLocal->m_iClass() == TF_CLASS_ENGINEER;
}

bool CNavBot::BlacklistedFromBuilding(CNavArea* pArea)
{
	// FIXME: Better way of doing this ?
	if (auto pBlackList = F::NavEngine.getFreeBlacklist())
	{
		for (auto tBlackListedArea : *pBlackList)
		{
			if (tBlackListedArea.first == pArea && tBlackListedArea.second.value == BlacklistReason_enum::BR_BAD_BUILDING_SPOT)
				return true;
		}
	}
	return false;
}

void CNavBot::RefreshBuildingSpots(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, ClosestEnemy_t tClosestEnemy, bool bForce)
{
	static Timer tRefreshBuildingSpotsTimer;
	if (!IsEngieMode(pLocal) || !pWeapon)
		return;

	bool bHasGunslinger = pWeapon->m_iItemDefinitionIndex() == Engi_t_TheGunslinger;
	if (bForce || tRefreshBuildingSpotsTimer.Run(bHasGunslinger ? 1.f : 5.f))
	{
		m_vBuildingSpots.clear();
		auto vTarget = F::FlagController.GetSpawnPosition(pLocal->m_iTeamNum());;
		if (!vTarget)
		{
			if (tClosestEnemy.m_iEntIdx)
				vTarget = F::NavParser.GetDormantOrigin(tClosestEnemy.m_iEntIdx);
			if (!vTarget)
				vTarget = pLocal->GetAbsOrigin();
		}
		if (vTarget)
		{
			// Search all nav areas for valid spots
			for (auto& tArea : F::NavEngine.getNavFile()->m_areas)
			{
				if (BlacklistedFromBuilding(&tArea))
					continue;

				if (tArea.m_TFattributeFlags & (TF_NAV_SPAWN_ROOM_RED | TF_NAV_SPAWN_ROOM_BLUE | TF_NAV_SPAWN_ROOM_EXIT))
					continue;

				if (tArea.m_TFattributeFlags & TF_NAV_SENTRY_SPOT)
					m_vBuildingSpots.emplace_back(tArea.m_center);
				else
				{
					for (auto tHidingSpot : tArea.m_hidingSpots)
					{
						if (tHidingSpot.HasGoodCover())
							m_vBuildingSpots.emplace_back(tHidingSpot.m_pos);
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
							  auto a_flDist = a.DistTo(*vTarget);
							  auto b_flDist = b.DistTo(*vTarget);

							  // Penalty for being in danger ranges
							  if (a_flDist + 100.0f < 300.0f)
								  a_flDist += 4000.0f;
							  if (b_flDist + 100.0f < 300.0f)
								  b_flDist += 4000.0f;

							  if (a_flDist + 1000.0f < 500.0f)
								  a_flDist += 1500.0f;
							  if (b_flDist + 1000.0f < 500.0f)
								  b_flDist += 1500.0f;

							  return a_flDist < b_flDist;
						  }
						  else
							  return a.DistTo(*vTarget) < b.DistTo(*vTarget);
					  });
		}
	}
}

void CNavBot::RefreshLocalBuildings(CTFPlayer* pLocal)
{
	if (IsEngieMode(pLocal))
	{
		m_iMySentryIdx = -1;
		m_iMyDispenserIdx = -1;

		for (auto pEntity : H::Entities.GetGroup(EGroupType::BUILDINGS_TEAMMATES))
		{
			auto iClassID = pEntity->GetClassID();
			if (iClassID != ETFClassID::CObjectSentrygun && iClassID != ETFClassID::CObjectDispenser)
				continue;

			auto pBuilding = pEntity->As<CBaseObject>();
			auto pBuilder = pBuilding->m_hBuilder().Get();
			if (!pBuilder)
				continue;

			if (pBuilding->m_bPlacing())
				continue;

			if (pBuilder->entindex() != pLocal->entindex())
				continue;

			if (iClassID == ETFClassID::CObjectSentrygun)
				m_iMySentryIdx = pBuilding->entindex();
			else if (iClassID == ETFClassID::CObjectDispenser)
				m_iMyDispenserIdx = pBuilding->entindex();
		}
	}
}

bool CNavBot::NavToSentrySpot()
{
	static Timer tWaitUntilPathSentryTimer;

	// Wait a bit before pathing again
	if (!tWaitUntilPathSentryTimer.Run(0.3f))
		return false;

	// Try to nav to our existing sentry spot
	if (auto pSentry = I::ClientEntityList->GetClientEntity(m_iMySentryIdx))
	{
		// Don't overwrite current nav
		if (F::NavEngine.current_priority == engineer)
			return true;

		auto vSentryOrigin = F::NavParser.GetDormantOrigin(pSentry->entindex());
		if (!vSentryOrigin)
			return false;

		if (F::NavEngine.navTo(*vSentryOrigin, engineer))
			return true;
	}
	else
		m_iMySentryIdx = -1;

	if (m_vBuildingSpots.empty())
		return false;

	// Don't overwrite current nav
	if (F::NavEngine.current_priority == engineer)
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
		if (F::NavEngine.navTo(vRandom, engineer))
		{
			vCurrentBuildingSpot = vRandom;
			return true;
		}
	}
	return false;
}


void CNavBot::UpdateEnemyBlacklist(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, int iSlot)
{
	if (!(Vars::Misc::Movement::NavBot::Blacklist.Value & Vars::Misc::Movement::NavBot::BlacklistEnum::Players))
		return;

	if (!(Vars::Misc::Movement::NavBot::Blacklist.Value & Vars::Misc::Movement::NavBot::BlacklistEnum::DormantThreats))
		F::NavEngine.clearFreeBlacklist(BlacklistReason(BR_ENEMY_DORMANT));

	if (!(Vars::Misc::Movement::NavBot::Blacklist.Value & Vars::Misc::Movement::NavBot::BlacklistEnum::NormalThreats))
	{
		F::NavEngine.clearFreeBlacklist(BlacklistReason(BR_ENEMY_NORMAL));
		return;
	}

	static Timer tBlacklistUpdateTimer{};
	static Timer tDormantUpdateTimer{};
	static int iLastSlotBlacklist = primary;

	bool bShouldRunNormal = tBlacklistUpdateTimer.Run(Vars::Misc::Movement::NavBot::BlacklistDelay.Value) || iLastSlotBlacklist != iSlot;
	bool bShouldRunDormant = Vars::Misc::Movement::NavBot::Blacklist.Value & Vars::Misc::Movement::NavBot::BlacklistEnum::DormantThreats && (tDormantUpdateTimer.Run(Vars::Misc::Movement::NavBot::BlacklistDormantDelay.Value) || iLastSlotBlacklist != iSlot);
	// Don't run since we do not care here
	if (!bShouldRunNormal && !bShouldRunDormant)
		return;

	// Clear blacklist for normal entities
	if (bShouldRunNormal)
		F::NavEngine.clearFreeBlacklist(BlacklistReason(BR_ENEMY_NORMAL));

	// Clear blacklist for dormant entities
	if (bShouldRunDormant)
		F::NavEngine.clearFreeBlacklist(BlacklistReason(BR_ENEMY_DORMANT));

	if (const auto& pGameRules = I::TFGameRules())
	{
		if (pGameRules->m_iRoundState() == GR_STATE_TEAM_WIN)
			return;
	}

	// #NoFear
	if (iSlot == melee)
		return;

	// Store the danger of the individual nav areas
	std::unordered_map<CNavArea*, int> mDormantSlightDanger;
	std::unordered_map<CNavArea*, int> mNormalSlightDanger;

	// This is used to cache Dangerous areas between ents
	std::unordered_map<CTFPlayer*, std::vector<CNavArea*>> mEntMarkedDormantSlightDanger;
	std::unordered_map<CTFPlayer*, std::vector<CNavArea*>> mEntMarkedNormalSlightDanger;

	std::vector<std::pair<CTFPlayer*, Vector>> vCheckedPlayerOrigins;
	for (auto pEntity : H::Entities.GetGroup(EGroupType::PLAYERS_ENEMIES))
	{
		// Entity is generally invalid, ignore
		auto pPlayer = pEntity->As<CTFPlayer>();
		if (!pPlayer->IsAlive())
			continue;

		bool bDormant = pPlayer->IsDormant();
		if (!bDormant)
		{
			// Should not run on normal entity and entity is not dormant, ignore
			if (!bShouldRunNormal)
				continue;
		}
		// Should not run on dormant and entity is dormant, ignore.
		else if (!bShouldRunDormant)
			continue;

		// Avoid excessive calls by ignoring new checks if people are too close to each other
		auto vOrigin = F::NavParser.GetDormantOrigin(pPlayer->entindex());
		if (!vOrigin)
			continue;

		vOrigin->z += PLAYER_JUMP_HEIGHT;

		bool bShouldCheck = true;

		// Find already dangerous marked areas by other entities
		auto mToLoop = bDormant ? &mEntMarkedDormantSlightDanger : &mEntMarkedNormalSlightDanger;

		// Add new danger entries
		auto mToMark = bDormant ? &mDormantSlightDanger : &mNormalSlightDanger;

		for (auto [pCheckedPlayer, vCheckedOrigin] : vCheckedPlayerOrigins)
		{
			// If this origin is closer than a quarter of the min HU (or less than 100 HU) to a cached one, don't go through
			// all nav areas again DistToSqr is much faster than DistTo which is why we use it here
			float flDist = m_tSelectedConfig.m_flMinSlightDanger;

			flDist *= 0.25f;
			flDist = std::max(100.0f, flDist);

			// Square the distance
			flDist *= flDist;

			if (vOrigin->DistToSqr(vCheckedOrigin) < flDist)
			{
				bShouldCheck = false;

				bool is_absolute_danger = flDist < pow(m_tSelectedConfig.m_flMinFullDanger, 2);
				if (!is_absolute_danger && (false/*slight danger when capping*/ || F::NavEngine.current_priority != capture))
				{
					// The area is not visible by the player
					if (!F::NavParser.IsVectorVisibleNavigation(*vOrigin, vCheckedOrigin, MASK_SHOT))
						continue;

					for (auto& pArea : (*mToLoop)[pCheckedPlayer])
					{
						(*mToMark)[pArea]++;
						if ((*mToMark)[pArea] >= Vars::Misc::Movement::NavBot::BlacklistSlightDangerLimit.Value)
							(*F::NavEngine.getFreeBlacklist())[pArea] = bDormant ? BlacklistReason_enum::BR_ENEMY_DORMANT : BlacklistReason_enum::BR_ENEMY_NORMAL;
						// pointers scare me..
					}
				}
				break;
			}
		}
		if (!bShouldCheck)
			continue;

		// Now check which areas they are close to
		for (auto& tArea : F::NavEngine.getNavFile()->m_areas)
		{
			float flDist = tArea.m_center.DistToSqr(*vOrigin);
			float flSlightDangerDist = m_tSelectedConfig.m_flMinSlightDanger;
			float flFullDangerDist = m_tSelectedConfig.m_flMinFullDanger;

			// Not dangerous, Still don't bump
			if (!ShouldTarget(pLocal, pWeapon, pPlayer->entindex()))
			{
				flSlightDangerDist = PLAYER_WIDTH * 1.2f;
				flFullDangerDist = PLAYER_WIDTH * 1.2f;
			}

			if (flDist < pow(flSlightDangerDist, 2))
			{
				Vector vNavAreaPos = tArea.m_center;
				vNavAreaPos.z += PLAYER_JUMP_HEIGHT;
				// The area is not visible by the player
				if (!F::NavParser.IsVectorVisibleNavigation(*vOrigin, vNavAreaPos, MASK_SHOT))
					continue;

				// Add as marked area
				(*mToLoop)[pPlayer].push_back(&tArea);

				// Just slightly dangerous, only mark as such if it's clear
				if (flDist >= pow(flFullDangerDist, 2) && (Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::SafeCapping || F::NavEngine.current_priority != capture))
				{
					(*mToMark)[&tArea]++;
					if ((*mToMark)[&tArea] < Vars::Misc::Movement::NavBot::BlacklistSlightDangerLimit.Value)
						continue;
				}
				(*F::NavEngine.getFreeBlacklist())[&tArea] = bDormant ? BlacklistReason_enum::BR_ENEMY_DORMANT : BlacklistReason_enum::BR_ENEMY_NORMAL;
			}
		}
		vCheckedPlayerOrigins.emplace_back(pPlayer, *vOrigin);
	}
}

bool CNavBot::IsAreaValidForStayNear(Vector vEntOrigin, CNavArea* pArea, bool bFixLocalZ)
{
	if (bFixLocalZ)
		vEntOrigin.z += PLAYER_JUMP_HEIGHT;
	auto vAreaOrigin = pArea->m_center;
	vAreaOrigin.z += PLAYER_JUMP_HEIGHT;

	float flDist = vEntOrigin.DistToSqr(vAreaOrigin);
	// Too close
	if (flDist < pow(m_tSelectedConfig.m_flMinFullDanger, 2))
		return false;

	// Blacklisted
	if (F::NavEngine.getFreeBlacklist()->find(pArea) != F::NavEngine.getFreeBlacklist()->end())
		return false;

	// Too far away
	if (flDist > pow(m_tSelectedConfig.m_flMax, 2))
		return false;

	CGameTrace trace = {};
	CTraceFilterWorldAndPropsOnly filter = {};

	// Attempt to vischeck
	SDK::Trace(vEntOrigin, vAreaOrigin, MASK_SHOT | CONTENTS_GRATE, &filter, &trace);
	return trace.fraction == 1.f;
}

bool CNavBot::StayNearTarget(int iEntIndex)
{
	auto pEntity = I::ClientEntityList->GetClientEntity(iEntIndex);
	if (!pEntity)
		return false;

	auto vOrigin = F::NavParser.GetDormantOrigin(iEntIndex);
	// No origin recorded, don't bother
	if (!vOrigin)
		return false;

	// Add the vischeck height
	vOrigin->z += PLAYER_JUMP_HEIGHT;

	// Use std::pair to avoid using the distance functions more than once
	std::vector<std::pair<CNavArea*, float>> vGoodAreas{};

	for (auto& tArea : F::NavEngine.getNavFile()->m_areas)
	{
		auto vAreaOrigin = tArea.m_center;

		// Is this area valid for stay near purposes?
		if (!IsAreaValidForStayNear(*vOrigin, &tArea, false))
			continue;

		// Good area found
		vGoodAreas.emplace_back(&tArea, (*vOrigin).DistToSqr(vAreaOrigin));
	}
	// Sort based on distance
	if (m_tSelectedConfig.m_bPreferFar)
		std::sort(vGoodAreas.begin(), vGoodAreas.end(), [](std::pair<CNavArea*, float> a, std::pair<CNavArea*, float> b) { return a.second > b.second; });
	else
		std::sort(vGoodAreas.begin(), vGoodAreas.end(), [](std::pair<CNavArea*, float> a, std::pair<CNavArea*, float> b) { return a.second < b.second; });

	// Try to path to all the good areas, based on distance
	if (std::ranges::any_of(vGoodAreas, [&](std::pair<CNavArea*, float> pair) -> bool
		{
			return F::NavEngine.navTo(pair.first->m_center, staynear, true, !F::NavEngine.isPathing());
		})
		)
	{
		m_iStayNearTargetIdx = pEntity->entindex();
		if (auto pPlayerResource = H::Entities.GetResource())
			m_sFollowTargetName = SDK::ConvertUtf8ToWide(pPlayerResource->GetName(pEntity->entindex()));
		return true;
	}

	return false;
}

int CNavBot::IsStayNearTargetValid(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, int iEntIndex)
{
	int iShouldTarget = ShouldTarget(pLocal, pWeapon, iEntIndex);
	int iResult = iEntIndex ? iShouldTarget : 0;
	return iResult;
}

std::optional<std::pair<CNavArea*, int>> CNavBot::FindClosestHidingSpot(CNavArea* pArea, std::optional<Vector> vVischeckPoint, int iRecursionCount, int iRecursionIndex)
{
	static std::vector<CNavArea*> vAlreadyRecursed;
	if (iRecursionIndex == 0)
		vAlreadyRecursed.clear();

	Vector vAreaOrigin = pArea->m_center;
	vAreaOrigin.z += PLAYER_JUMP_HEIGHT;

	// Increment recursion index
	iRecursionIndex++;

	// If the area works, return it
	if (vVischeckPoint && !F::NavParser.IsVectorVisibleNavigation(vAreaOrigin, *vVischeckPoint))
		return std::pair<CNavArea*, int>{ pArea, iRecursionIndex - 1 };
	// Termination condition not hit yet
	else if (iRecursionIndex < iRecursionCount)
	{
		// Store the nearest area
		std::optional<std::pair<CNavArea*, int>> vBestArea;
		for (auto& tConnection : pArea->m_connections)
		{
			if (std::find(vAlreadyRecursed.begin(), vAlreadyRecursed.end(), tConnection.area) != vAlreadyRecursed.end())
				continue;

			vAlreadyRecursed.push_back(tConnection.area);

			auto pArea = FindClosestHidingSpot(tConnection.area, vVischeckPoint, iRecursionCount, iRecursionIndex);
			if (pArea && (!vBestArea || pArea->second < vBestArea->second))
				vBestArea = { pArea->first, pArea->second };
		}
		return vBestArea;
	}
	return std::nullopt;
}

bool CNavBot::RunReload(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	static Timer tReloadrunCooldown{};

	// Not reloading, do not run
	if (!G::Reloading && pWeapon->m_iClip1())
		return false;

	if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::StalkEnemies))
		return false;

	// Too high priority, so don't try
	if (F::NavEngine.current_priority > run_reload)
		return false;

	// Re-calc only every once in a while
	if (!tReloadrunCooldown.Run(1.f))
		return F::NavEngine.current_priority == run_reload;

	// Get our area and start recursing the neighbours
	auto pLocalNav = F::NavEngine.findClosestNavSquare(pLocal->GetAbsOrigin());
	if (!pLocalNav)
		return false;

	// Get closest enemy to vicheck
	CBaseEntity* pClosestEnemy = nullptr;
	float flBestDist = FLT_MAX;
	for (auto pEntity : H::Entities.GetGroup(EGroupType::PLAYERS_ENEMIES))
	{
		if (!ShouldTarget(pLocal, pWeapon, pEntity->entindex()))
			continue;

		float flDist = pEntity->GetAbsOrigin().DistToSqr(pLocal->GetAbsOrigin());
		if (flDist > pow(flBestDist, 2))
			continue;

		flBestDist = flDist;
		pClosestEnemy = pEntity;
	}

	if (!pClosestEnemy)
		return false;

	Vector vVischeckPoint = pClosestEnemy->GetAbsOrigin();
	vVischeckPoint.z += PLAYER_JUMP_HEIGHT;

	// Get the best non visible area
	auto vBestArea = FindClosestHidingSpot(pLocalNav, vVischeckPoint, 5);
	if (!vBestArea)
		return false;

	// If we can, path
	if (F::NavEngine.navTo((*vBestArea).first->m_center, run_reload, true, !F::NavEngine.isPathing()))
		return true;
	return false;
}

slots CNavBot::GetReloadWeaponSlot(CTFPlayer* pLocal, ClosestEnemy_t tClosestEnemy)
{
	if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::ReloadWeapons))
		return slots();

	// Priority too high
	if (F::NavEngine.current_priority > capture)
		return slots();

	// Dont try to reload in combat
	if (F::NavEngine.current_priority == staynear && tClosestEnemy.m_flDist <= pow(500, 2)
		|| tClosestEnemy.m_flDist <= pow(250, 2))
		return slots();

	auto pPrimaryWeapon = pLocal->GetWeaponFromSlot(SLOT_PRIMARY);
	auto pSecondaryWeapon = pLocal->GetWeaponFromSlot(SLOT_SECONDARY);
	bool bCheckPrimary = !SDK::WeaponDoesNotUseAmmo(G::SavedWepIds[SLOT_PRIMARY], G::SavedDefIndexes[SLOT_PRIMARY], false);
	bool bCheckSecondary = !SDK::WeaponDoesNotUseAmmo(G::SavedWepIds[SLOT_SECONDARY], G::SavedDefIndexes[SLOT_SECONDARY], false);

	float flDivider = F::NavEngine.current_priority < staynear && tClosestEnemy.m_flDist > pow(500, 2) ? 1.f : 3.f;

	CTFWeaponInfo* pWeaponInfo = nullptr;
	bool bWeaponCantReload = false;
	if (bCheckPrimary && pPrimaryWeapon)
	{
		pWeaponInfo = pPrimaryWeapon->GetWeaponInfo();
		bWeaponCantReload = (!pWeaponInfo || pWeaponInfo->iMaxClip1 < 0 || !pLocal->GetAmmoCount(pPrimaryWeapon->m_iPrimaryAmmoType())) && G::SavedWepIds[SLOT_PRIMARY] != TF_WEAPON_PARTICLE_CANNON && G::SavedWepIds[SLOT_PRIMARY] != TF_WEAPON_DRG_POMSON;
		if (pWeaponInfo && !bWeaponCantReload && G::AmmoInSlot[SLOT_PRIMARY] < (pWeaponInfo->iMaxClip1 / flDivider))
			return primary;
	}

	bool bFoundPrimaryWepInfo = pWeaponInfo;
	if (bCheckSecondary && pSecondaryWeapon && (bFoundPrimaryWepInfo || !bCheckPrimary))
	{
		pWeaponInfo = pSecondaryWeapon->GetWeaponInfo();
		bWeaponCantReload = (!pWeaponInfo || pWeaponInfo->iMaxClip1 < 0 || !pLocal->GetAmmoCount(pSecondaryWeapon->m_iPrimaryAmmoType())) && G::SavedWepIds[SLOT_SECONDARY] != TF_WEAPON_RAYGUN;
		if (pWeaponInfo && !bWeaponCantReload && G::AmmoInSlot[SLOT_SECONDARY] < (pWeaponInfo->iMaxClip1 / flDivider))
			return secondary;
	}

	// We succeeded in finding a reload slot previously
	// return our current slot to avoid getting stuck switching between our best weapon slot and reload weapon slot
	/*if ( m_eLastReloadSlot && ( ( bCheckPrimary && ( !pPrimaryWeapon || !bFoundPrimaryWepInfo ) )
		|| ( bCheckSecondary && ( !pSecondaryWeapon || !pWeaponInfo ) ) ) )
	{
		return m_eLastReloadSlot;
	}*/

	return slots();
}

bool CNavBot::RunSafeReload(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	static Timer tReloadrunCooldown{};
	if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::ReloadWeapons) || !m_eLastReloadSlot && !G::Reloading)
	{
		if (F::NavEngine.current_priority == run_safe_reload)
			F::NavEngine.cancelPath();
		return false;
	}

	// Re-calc only every once in a while
	if (!tReloadrunCooldown.Run(1.f))
		return F::NavEngine.current_priority == run_safe_reload;

	// Get our area and start recursing the neighbours
	auto pLocalNav = F::NavEngine.findClosestNavSquare(pLocal->GetAbsOrigin());
	if (!pLocalNav)
		return false;

	// If pathing try to avoid going to our current destination until we fully reload
	std::optional<Vector> vCurrentDestination;
	auto pCrumbs = F::NavEngine.getCrumbs();
	if (F::NavEngine.current_priority != run_safe_reload && pCrumbs->size() > 4)
		vCurrentDestination = pCrumbs->at(4).vec;

	if (vCurrentDestination)
		vCurrentDestination->z += PLAYER_JUMP_HEIGHT;
	else
	{
		// Get closest enemy to vicheck
		CBaseEntity* pClosestEnemy = nullptr;
		float flBestDist = FLT_MAX;
		for (auto pEntity : H::Entities.GetGroup(EGroupType::PLAYERS_ENEMIES))
		{
			if (pEntity->IsDormant())
				continue;

			if (!ShouldTarget(pLocal, pWeapon, pEntity->entindex()))
				continue;

			float flDist = pEntity->GetAbsOrigin().DistToSqr(pLocal->GetAbsOrigin());
			if (flDist > pow(flBestDist, 2))
				continue;

			flBestDist = flDist;
			pClosestEnemy = pEntity;
		}

		if (pClosestEnemy)
		{
			vCurrentDestination = pClosestEnemy->GetAbsOrigin();
			vCurrentDestination->z += PLAYER_JUMP_HEIGHT;
		}
	}
	// Get the best non visible area
	auto vBestArea = FindClosestHidingSpot(pLocalNav, vCurrentDestination, 5);
	if (vBestArea)
	{
		// If we can, path
		if (F::NavEngine.navTo((*vBestArea).first->m_center, run_safe_reload, true, !F::NavEngine.isPathing()))
			return true;
	}

	return false;
}

bool CNavBot::StayNear(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	static Timer tStaynearCooldown{};
	static Timer tInvalidTargetTimer{};
	static int iStayNearTargetIdx = -1;

	// Stay near is expensive so we have to cache. We achieve this by only checking a pre-determined amount of players every
	// CreateMove
	constexpr int MAX_STAYNEAR_CHECKS_RANGE = 3;
	constexpr int MAX_STAYNEAR_CHECKS_CLOSE = 2;

	// Stay near is off
	if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::StalkEnemies))
	{
		iStayNearTargetIdx = -1;
		return false;
	}

	// Don't constantly path, it's slow.
	// Far range classes do not need to repath nearly as often as close range ones.
	if (!tStaynearCooldown.Run(m_tSelectedConfig.m_bPreferFar ? 2.f : 0.5f))
		return F::NavEngine.current_priority == staynear;

	// Too high priority, so don't try
	if (F::NavEngine.current_priority > staynear)
	{
		iStayNearTargetIdx = -1;
		return false;
	}

	int iPreviousTargetValid = IsStayNearTargetValid(pLocal, pWeapon, iStayNearTargetIdx);
	// Check and use our previous target if available
	if (iPreviousTargetValid)
	{
		tInvalidTargetTimer.Update();

		// Check if target is RAGE status - if so, always keep targeting them
		int iPriority = H::Entities.GetPriority(iStayNearTargetIdx);
		if (iPriority > F::PlayerUtils.m_vTags[F::PlayerUtils.TagToIndex(DEFAULT_TAG)].m_iPriority)
		{
			if (StayNearTarget(iStayNearTargetIdx))
				return true;
		}

		auto vOrigin = F::NavParser.GetDormantOrigin(iStayNearTargetIdx);
		if (vOrigin)
		{
			// Check if current target area is valid
			if (F::NavEngine.isPathing())
			{
				auto pCrumbs = F::NavEngine.getCrumbs();
				// We cannot just use the last crumb, as it is always nullptr
				if (pCrumbs->size() > 2)
				{
					auto tLastCrumb = (*pCrumbs)[pCrumbs->size() - 2];
					// Area is still valid, stay on it
					if (IsAreaValidForStayNear(*vOrigin, tLastCrumb.navarea))
						return true;
				}
			}
			// Else Check our origin for validity (Only for ranged classes)
			else if (m_tSelectedConfig.m_bPreferFar && IsAreaValidForStayNear(*vOrigin, F::NavEngine.findClosestNavSquare(pLocal->GetAbsOrigin())))
				return true;
		}
		// Else we try to path again
		if (StayNearTarget(iStayNearTargetIdx))
			return true;

	}
	// Our previous target wasn't properly checked, try again unless
	else if (iPreviousTargetValid == -1 && !tInvalidTargetTimer.Check(0.1f))
		return F::NavEngine.current_priority == staynear;

	// Failed, invalidate previous target and try others
	iStayNearTargetIdx = -1;
	tInvalidTargetTimer.Update();

	// Cancel path so that we dont follow old target
	if (F::NavEngine.current_priority == staynear)
		F::NavEngine.cancelPath();

	std::vector<std::pair<int, int>> vPriorityPlayers{};
	std::unordered_set<int> sHasPriority{};
	for (const auto& pEntity : H::Entities.GetGroup(EGroupType::PLAYERS_ENEMIES))
	{
		int iPriority = H::Entities.GetPriority(pEntity->entindex());
		if (iPriority > F::PlayerUtils.m_vTags[F::PlayerUtils.TagToIndex(DEFAULT_TAG)].m_iPriority)
		{
			vPriorityPlayers.push_back({ pEntity->entindex(), iPriority });
			sHasPriority.insert(pEntity->entindex());
		}
	}
	std::sort(vPriorityPlayers.begin(), vPriorityPlayers.end(), [](std::pair<int, int> a, std::pair<int, int> b) { return a.second > b.second; });

	// First check for RAGE players - they get highest priority
	for (auto [iPlayerIdx, _] : vPriorityPlayers)
	{
		if (!IsStayNearTargetValid(pLocal, pWeapon, iPlayerIdx))
			continue;

		if (StayNearTarget(iPlayerIdx))
		{
			iStayNearTargetIdx = iPlayerIdx;
			return true;
		}
	}

	// Then check other players
	int iCalls = 0;
	auto iAdvanceCount = m_tSelectedConfig.m_bPreferFar ? MAX_STAYNEAR_CHECKS_RANGE : MAX_STAYNEAR_CHECKS_CLOSE;
	std::vector<std::pair<int, float>> vSortedPlayers{};
	for (auto pEntity : H::Entities.GetGroup(EGroupType::PLAYERS_ENEMIES))
	{
		if (iCalls >= iAdvanceCount)
			break;
		iCalls++;

		// Skip RAGE players as we already checked them
		if (sHasPriority.contains(pEntity->entindex()))
			continue;

		auto iPlayerIdx = pEntity->entindex();
		if (!IsStayNearTargetValid(pLocal, pWeapon, iPlayerIdx))
		{
			iCalls--;
			continue;
		}

		auto vOrigin = F::NavParser.GetDormantOrigin(iPlayerIdx);
		if (!vOrigin)
			continue;

		vSortedPlayers.push_back({ iPlayerIdx, vOrigin->DistToSqr(pLocal->GetAbsOrigin()) });
	}
	if (!vSortedPlayers.empty())
	{
		std::sort(vSortedPlayers.begin(), vSortedPlayers.end(), [](std::pair<int, float> a, std::pair<int, float> b) { return a.second < b.second; });

		for (auto [iIdx, _] : vSortedPlayers)
		{
			// Succeeded pathing
			if (StayNearTarget(iIdx))
			{
				iStayNearTargetIdx = iIdx;
				return true;
			}
		}
	}

	// Stay near failed to find any good targets, add extra delay
	tStaynearCooldown += 3.f;
	return false;
}

bool CNavBot::MeleeAttack(CUserCmd* pCmd, CTFPlayer* pLocal, int iSlot, ClosestEnemy_t tClosestEnemy)
{
	static bool bIsVisible = false;
	auto pEntity = I::ClientEntityList->GetClientEntity(tClosestEnemy.m_iEntIdx);
	if (!pEntity || pEntity->IsDormant())
		return F::NavEngine.current_priority == prio_melee;

	if (iSlot != melee || m_eLastReloadSlot)
	{
		if (F::NavEngine.current_priority == prio_melee)
			F::NavEngine.cancelPath();
		return false;
	}

	auto pPlayer = pEntity->As<CTFPlayer>();
	if (pPlayer->IsInvulnerable() && G::SavedDefIndexes[SLOT_MELEE] != Heavy_t_TheHolidayPunch)
		return false;

	// Too high priority, so don't try
	if (F::NavEngine.current_priority > prio_melee)
		return false;

	
	static Timer tVischeckCooldown{};
	if (tVischeckCooldown.Run(0.2f))
	{
		trace_t trace;
		CTraceFilterHitscan filter{}; filter.pSkip = pLocal;
		SDK::TraceHull(pLocal->GetShootPos(), pPlayer->GetAbsOrigin(), pLocal->m_vecMins() * 0.3f, pLocal->m_vecMaxs() * 0.3f, MASK_PLAYERSOLID, &filter, &trace);
		bIsVisible = trace.DidHit() ? trace.m_pEnt && trace.m_pEnt == pPlayer : true;
	}

	Vector vTargetOrigin = pPlayer->GetAbsOrigin();
	Vector vLocalOrigin = pLocal->GetAbsOrigin();
	// If we are close enough, don't even bother with using the navparser to get there
	if (tClosestEnemy.m_flDist < pow(100.0f, 2) && bIsVisible)
	{
		// Crouch if we are standing on someone
		if (pLocal->m_hGroundEntity().Get() && pLocal->m_hGroundEntity().Get()->IsPlayer())
			pCmd->buttons |= IN_DUCK;

		SDK::WalkTo(pCmd, pLocal, vTargetOrigin);
		F::NavEngine.cancelPath();
		F::NavEngine.current_priority = prio_melee;
		return true;
	}
	
	// Don't constantly path, it's slow.
	// The closer we are, the more we should try to path
	static Timer tMeleeCooldown{};
	if (!tMeleeCooldown.Run(tClosestEnemy.m_flDist < pow(100.0f, 2) ? 0.2f : tClosestEnemy.m_flDist < pow(1000.0f, 2) ? 0.5f : 2.f) && F::NavEngine.isPathing())
		return F::NavEngine.current_priority == prio_melee;

	// Just walk at the enemy l0l
	if (F::NavEngine.navTo(vTargetOrigin, prio_melee, true, !F::NavEngine.isPathing()))
		return true;
	return false;
}

bool CNavBot::IsAreaValidForSnipe(Vector vEntOrigin, Vector vAreaOrigin, bool bFixSentryZ)
{
	if (bFixSentryZ)
		vEntOrigin.z += 40.0f;
	vAreaOrigin.z += PLAYER_JUMP_HEIGHT;

	// Too close to be valid
	if (vEntOrigin.DistToSqr(vAreaOrigin) <= pow(1100.0f + HALF_PLAYER_WIDTH, 2))
		return false;

	// Fails vischeck, bad
	if (!F::NavParser.IsVectorVisibleNavigation(vAreaOrigin, vEntOrigin))
		return false;
	return true;
}

bool CNavBot::TryToSnipe(CBaseObject* pBulding)
{
	auto vOrigin = F::NavParser.GetDormantOrigin(pBulding->entindex());
	if (!vOrigin)
		return false;

	// Add some z to dormant sentries as it only returns origin
	//if (ent->IsDormant())
	vOrigin->z += 40.0f;

	std::vector<std::pair<CNavArea*, float>> vGoodAreas;
	for (auto& area : F::NavEngine.getNavFile()->m_areas)
	{
		// Not usable
		if (!IsAreaValidForSnipe(*vOrigin, area.m_center, false))
			continue;
		vGoodAreas.push_back(std::pair<CNavArea*, float>(&area, area.m_center.DistToSqr(*vOrigin)));
	}

	// Sort based on distance
	if (m_tSelectedConfig.m_bPreferFar)
		std::sort(vGoodAreas.begin(), vGoodAreas.end(), [](std::pair<CNavArea*, float> a, std::pair<CNavArea*, float> b) { return a.second > b.second; });
	else
		std::sort(vGoodAreas.begin(), vGoodAreas.end(), [](std::pair<CNavArea*, float> a, std::pair<CNavArea*, float> b) { return a.second < b.second; });

	if (std::ranges::any_of(vGoodAreas, [](std::pair<CNavArea*, float> pair) { return F::NavEngine.navTo(pair.first->m_center, snipe_sentry); }))
		return true;
	return false;
}

int CNavBot::IsSnipeTargetValid(CTFPlayer* pLocal, int iBuildingIdx)
{
	if (!(Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Sentry))
		return 0;

	int iShouldTarget = ShouldTargetBuilding(pLocal, iBuildingIdx);
	int iResult = iBuildingIdx ? iShouldTarget : 0;
	return iResult;
}

bool CNavBot::SnipeSentries(CTFPlayer* pLocal)
{
	static Timer tSentrySnipeCooldown;
	static Timer tInvalidTargetTimer{};
	static int iPreviousTargetIdx = -1;

	if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::TargetSentries))
		return false;

	// Make sure we don't try to do it on shortrange classes unless specified
	if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::TargetSentriesLowRange)
		&& (pLocal->m_iClass() == TF_CLASS_SCOUT || pLocal->m_iClass() == TF_CLASS_PYRO))
		return false;

	// Sentries don't move often, so we can use a slightly longer timer
	if (!tSentrySnipeCooldown.Run(2.f))
		return F::NavEngine.current_priority == snipe_sentry;

	int iPreviousTargetValid = IsSnipeTargetValid(pLocal, iPreviousTargetIdx);
	if (iPreviousTargetValid)
	{
		tInvalidTargetTimer.Update();

		auto pCrumbs = F::NavEngine.getCrumbs();
		if (auto pPrevTarget = I::ClientEntityList->GetClientEntity(iPreviousTargetIdx)->As<CBaseObject>())
		{
			auto vOrigin = F::NavParser.GetDormantOrigin(iPreviousTargetIdx);
			if (vOrigin)
			{
				// We cannot just use the last crumb, as it is always nullptr
				if (pCrumbs->size() > 2)
				{
					auto tLastCrumb = (*pCrumbs)[pCrumbs->size() - 2];

					// Area is still valid, stay on it
					if (IsAreaValidForSnipe(*vOrigin, tLastCrumb.navarea->m_center))
						return true;
				}
				if (TryToSnipe(pPrevTarget))
					return true;
			}
		}
	}
	// Our previous target wasn't properly checked
	else if (iPreviousTargetValid == -1 && !tInvalidTargetTimer.Check(0.1f))
		return F::NavEngine.current_priority == snipe_sentry;

	tInvalidTargetTimer.Update();

	for (auto pEntity : H::Entities.GetGroup(EGroupType::BUILDINGS_ENEMIES))
	{
		// Invalid sentry
		if (IsSnipeTargetValid(pLocal, pEntity->entindex()))
			continue;

		// Succeeded in trying to snipe it
		if (TryToSnipe(pEntity->As<CBaseObject>()))
		{
			iPreviousTargetIdx = pEntity->entindex();
			return true;
		}
	}

	iPreviousTargetIdx = -1;
	return false;
}

static CBaseObject* GetCarriedBuilding(CTFPlayer* pLocal)
{
	if (auto pEntity = pLocal->m_hCarriedObject().Get())
		return pEntity->As<CBaseObject>();
	return nullptr;
}

bool CNavBot::BuildBuilding(CUserCmd* pCmd, CTFPlayer* pLocal, ClosestEnemy_t tClosestEnemy, int building)
{
	auto pMeleeWeapon = pLocal->GetWeaponFromSlot(SLOT_MELEE);
	if (!pMeleeWeapon)
		return false;

	// Blacklist this spot and refresh the building spots
	if (m_iBuildAttempts >= 15)
	{
		(*F::NavEngine.getFreeBlacklist())[F::NavEngine.findClosestNavSquare(pLocal->GetAbsOrigin())] = BlacklistReason_enum::BR_BAD_BUILDING_SPOT;
		RefreshBuildingSpots(pLocal, pMeleeWeapon, tClosestEnemy, true);
		vCurrentBuildingSpot = std::nullopt;
		m_iBuildAttempts = 0;
		return false;
	}

	// Make sure we have right amount of metal
	int iRequiredMetal = (pMeleeWeapon->m_iItemDefinitionIndex() == Engi_t_TheGunslinger || building == dispenser) ? 100 : 130;
	if (pLocal->m_iMetalCount() < iRequiredMetal)
		return GetAmmo(pCmd, pLocal, true);

	m_sEngineerTask = std::format(L"Build {}", building == dispenser ? L"dispenser" : L"sentry");
	static float flPrevYaw = 0.0f;
	// Try to build! we are close enough
	if (vCurrentBuildingSpot && vCurrentBuildingSpot->DistToSqr(pLocal->GetAbsOrigin()) <= pow(building == dispenser ? 500.0f : 200.0f, 2))
	{
		// TODO: Rotate our angle to a valid building spot ? also rotate building itself to face enemies ?
		pCmd->viewangles.x = 20.0f;
		pCmd->viewangles.y = flPrevYaw += 2.0f;

		// Gives us 4 1/2 seconds to build
		static Timer tAttemptTimer;
		if (tAttemptTimer.Run(0.3f))
			m_iBuildAttempts++;

		//auto pCarriedBuilding = GetCarriedBuilding( pLocal );
		if (!pLocal->m_bCarryingObject())
		{
			static Timer command_timer;
			if (command_timer.Run(0.1f))
				I::EngineClient->ClientCmd_Unrestricted(std::format("build {}", building).c_str());
		}
		//else if (pCarriedBuilding->m_bServerOverridePlacement()) // Can place
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

bool CNavBot::BuildingNeedsToBeSmacked(CBaseObject* pBuilding)
{
	if (!pBuilding || pBuilding->IsDormant())
		return false;

	if (pBuilding->m_iUpgradeLevel() != 3 || pBuilding->m_iHealth() / pBuilding->m_iMaxHealth() <= 0.80f)
		return true;

	if (pBuilding->GetClassID() == ETFClassID::CObjectSentrygun)
	{
		int iMaxAmmo = 0;
		switch (pBuilding->m_iUpgradeLevel())
		{
		case 1:
			iMaxAmmo = 150;
			break;
		case 2:
		case 3:
			iMaxAmmo = 200;
			break;
		}

		return pBuilding->As<CObjectSentrygun>()->m_iAmmoShells() / iMaxAmmo <= 0.50f;
	}
	return false;
}

bool CNavBot::SmackBuilding(CUserCmd* pCmd, CTFPlayer* pLocal, CBaseObject* pBuilding)
{
	if (!pBuilding || pBuilding->IsDormant())
		return false;

	if (!pLocal->m_iMetalCount())
		return GetAmmo(pCmd, pLocal, true);

	CTFWeaponBase* pWeapon = H::Entities.GetWeapon();
	if (!pWeapon)
		return false;

	m_sEngineerTask = std::format(L"Smack {}", pBuilding->GetClassID() == ETFClassID::CObjectDispenser ? L"dispenser" : L"sentry");

	if (pBuilding->GetAbsOrigin().DistToSqr(pLocal->GetAbsOrigin()) <= pow(100.0f, 2) && pWeapon->GetSlot() == SLOT_MELEE)
	{
		pCmd->buttons |= IN_ATTACK;

		auto vAngTo = Math::CalcAngle(pLocal->GetEyePosition(), pBuilding->GetCenter());
		G::Attacking = SDK::IsAttacking(pLocal, pWeapon, pCmd, true);
		if (G::Attacking == 1)
			pCmd->viewangles = vAngTo;
	}
	else if (F::NavEngine.current_priority != engineer)
		return F::NavEngine.navTo(pBuilding->GetAbsOrigin(), engineer);

	return true;
}

bool CNavBot::EngineerLogic(CUserCmd* pCmd, CTFPlayer* pLocal, ClosestEnemy_t tClosestEnemy)
{
	if (!IsEngieMode(pLocal))
	{
		m_sEngineerTask.clear();
		return false;
	}

	auto pMeleeWeapon = pLocal->GetWeaponFromSlot(SLOT_MELEE);
	if (!pMeleeWeapon)
		return false;

	auto pSentry = I::ClientEntityList->GetClientEntity(m_iMySentryIdx);
	// Already have a sentry
	if (pSentry && pSentry->GetClassID() == ETFClassID::CObjectSentrygun)
	{
		if (pMeleeWeapon->m_iItemDefinitionIndex() == Engi_t_TheGunslinger)
		{
			auto vSentryOrigin = F::NavParser.GetDormantOrigin(m_iMySentryIdx);
			// Too far away, destroy it
			// BUG Ahead, building isnt destroyed lol
			if (!vSentryOrigin || vSentryOrigin->DistToSqr(pLocal->GetAbsOrigin()) >= pow(1800.0f, 2))
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
			if (BuildingNeedsToBeSmacked(pSentry->As<CBaseObject>()))
				return SmackBuilding(pCmd, pLocal, pSentry->As<CBaseObject>());
			else
			{
				auto pDispenser = I::ClientEntityList->GetClientEntity(m_iMyDispenserIdx);
				// We put dispenser by sentry
				if (!pDispenser)
					return BuildBuilding(pCmd, pLocal, tClosestEnemy, dispenser);
				else
				{
					// We already have a dispenser, see if it needs to be smacked
					if (BuildingNeedsToBeSmacked(pDispenser->As<CBaseObject>()))
						return SmackBuilding(pCmd, pLocal, pDispenser->As<CBaseObject>());

					// Return false so we run another task
					return false;
				}
			}

		}
	}
	// Try to build a sentry
	return BuildBuilding(pCmd, pLocal, tClosestEnemy, sentry);
}

std::optional<Vector> CNavBot::GetCtfGoal(CTFPlayer* pLocal, int iOurTeam, int iEnemyTeam)
{
	// Get Flag related information
	auto iStatus = F::FlagController.GetStatus(iEnemyTeam);
	auto vPosition = F::FlagController.GetPosition(iEnemyTeam);
	auto iCarrierIdx = F::FlagController.GetCarrier(iEnemyTeam);

	// CTF is the current capture type
	if (iStatus == TF_FLAGINFO_STOLEN)
	{
		if (iCarrierIdx == pLocal->entindex())
		{
			// Return our capture point location
			return F::FlagController.GetSpawnPosition(iOurTeam);
		}
		// Assist with capturing
		else if (Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::HelpCaptureObjectives)
		{
			if (ShouldAssist(pLocal, iCarrierIdx))
			{
				// Stay slightly behind and to the side to avoid blocking
				if (vPosition)
				{
					Vector vOffset(40.0f, 40.0f, 0.0f);
					*vPosition -= vOffset;
				}
				return vPosition;
			}
		}
		return std::nullopt;
	}

	// Get the flag if not taken by us already
	return vPosition;
}

std::optional<Vector> CNavBot::GetPayloadGoal(const Vector vLocalOrigin, int iOurTeam)
{
	auto vPosition = F::PLController.GetClosestPayload(vLocalOrigin, iOurTeam);
	if (!vPosition)
		return std::nullopt;

	// Get number of teammates near cart to coordinate positioning
	int iTeammatesNearCart = 0;
	constexpr float flCartRadius = 150.0f; // Approx cart capture radius

	for (auto pEntity : H::Entities.GetGroup(EGroupType::PLAYERS_TEAMMATES))
	{
		if (pEntity->IsDormant() || pEntity->entindex() == I::EngineClient->GetLocalPlayer())
			continue;

		auto pTeammate = pEntity->As<CTFPlayer>();
		if (!pTeammate->IsAlive())
			continue;

		if (pTeammate->GetAbsOrigin().DistToSqr(*vPosition) <= pow(flCartRadius, 2))
			iTeammatesNearCart++;
	}

	// Adjust position based on number of teammates to avoid crowding
	Vector vAdjusted_pos = *vPosition;
	if (iTeammatesNearCart > 0)
	{
		// Add ourselves to the total amount
		iTeammatesNearCart++;

		// Create a ring formation around cart
		float flAngle = PI * 2 * (float)(I::EngineClient->GetLocalPlayer() % iTeammatesNearCart) / iTeammatesNearCart;
		Vector vOffset(cos(flAngle) * 75.0f, sin(flAngle) * 75.0f, 0.0f);
		vAdjusted_pos += vOffset;
	}

	// Adjust position, so it's not floating high up, provided the local player is close.
	if (vLocalOrigin.DistToSqr(vAdjusted_pos) <= pow(150.0f, 2))
		vAdjusted_pos.z = vLocalOrigin.z;

	// If close enough, don't move (mostly due to lifts)
	if (vAdjusted_pos.DistToSqr(vLocalOrigin) <= pow(15.0f, 2))
	{
		m_bOverwriteCapture = true;
		return std::nullopt;
	}

	return vAdjusted_pos;
}

std::optional<Vector> CNavBot::GetControlPointGoal(const Vector vLocalOrigin, int iOurTeam)
{
	auto vPosition = F::CPController.GetClosestControlPoint(vLocalOrigin, iOurTeam);
	if (!vPosition)
	{
		m_vCurrentCaptureSpot.reset();
		m_vCurrentCaptureCenter.reset();
		return std::nullopt;
	}

	if (!m_vCurrentCaptureCenter.has_value() || m_vCurrentCaptureCenter->DistToSqr(*vPosition) > 1.0f)
	{
		m_vCurrentCaptureCenter = *vPosition;
		m_vCurrentCaptureSpot.reset();
	}

	constexpr float flCapRadius = 100.0f; // Approximate capture radius
	constexpr float flThreatRadius = 800.0f; // Distance to check for enemies
	constexpr float flOccupancyRadius = 28.0f;
	const float flOccupancyRadiusSq = flOccupancyRadius * flOccupancyRadius;
	const int iLocalIndex = I::EngineClient->GetLocalPlayer();

	std::vector<Vector> vTeammatePositions;
	vTeammatePositions.reserve(8);
	int iTeammatesOnPoint = 0;

	for (auto pEntity : H::Entities.GetGroup(EGroupType::PLAYERS_TEAMMATES))
	{
		if (pEntity->IsDormant() || pEntity->entindex() == iLocalIndex)
			continue;

		auto pTeammate = pEntity->As<CTFPlayer>();
		if (!pTeammate || !pTeammate->IsAlive())
			continue;

		Vector vTeammateOrigin = pTeammate->GetAbsOrigin();
		vTeammatePositions.push_back(vTeammateOrigin);

		if (vTeammateOrigin.DistToSqr(*vPosition) <= pow(flCapRadius, 2))
			iTeammatesOnPoint++;
	}

	bool bEnemiesNear = false;
	for (auto pEntity : H::Entities.GetGroup(EGroupType::PLAYERS_ENEMIES))
	{
		if (pEntity->IsDormant())
			continue;

		auto pEnemy = pEntity->As<CTFPlayer>();
		if (!pEnemy || !pEnemy->IsAlive())
			continue;

		if (pEnemy->GetAbsOrigin().DistToSqr(*vPosition) <= pow(flThreatRadius, 2))
		{
			bEnemiesNear = true;
			break;
		}
	}

	auto spotTakenByOther = [&](const Vector& spot) -> bool
	{
		for (const auto& teammatePos : vTeammatePositions)
		{
			Vector2D delta(teammatePos.x - spot.x, teammatePos.y - spot.y);
			if (delta.LengthSqr() <= flOccupancyRadiusSq)
				return true;
		}
		return false;
	};

	auto closestTeammateDistanceSq = [&](const Vector& spot) -> float
	{
		if (vTeammatePositions.empty())
			return FLT_MAX;

		float best = FLT_MAX;
		for (const auto& teammatePos : vTeammatePositions)
		{
			Vector2D delta(teammatePos.x - spot.x, teammatePos.y - spot.y);
			float distSq = delta.LengthSqr();
			if (distSq < best)
				best = distSq;
		}
		return best;
	};

	Vector vAdjustedPos = *vPosition;

	if (bEnemiesNear)
	{
		m_vCurrentCaptureSpot.reset();
		for (auto tArea : F::NavEngine.getNavFile()->m_areas)
		{
			for (auto& tHidingSpot : tArea.m_hidingSpots)
			{
				if (tHidingSpot.HasGoodCover() && tHidingSpot.m_pos.DistTo(*vPosition) <= flCapRadius)
				{
					vAdjustedPos = tHidingSpot.m_pos;
					break;
				}
			}
		}
	}
	else
	{
		if (m_vCurrentCaptureSpot && spotTakenByOther(m_vCurrentCaptureSpot.value()))
			m_vCurrentCaptureSpot.reset();

		if (!m_vCurrentCaptureSpot)
		{
			const int iSlots = std::clamp(iTeammatesOnPoint + 1, 1, 8);
			const float flBaseRadius = iSlots == 1 ? 0.0f : std::min(flCapRadius - 12.0f, 45.0f + 12.0f * static_cast<float>(iSlots - 1));
			const int iPreferredSlot = iSlots > 0 ? (iLocalIndex % iSlots) : 0;

			auto adjustToNav = [&](Vector candidate)
			{
				if (F::NavEngine.IsNavMeshLoaded())
				{
					if (auto pArea = F::NavEngine.findClosestNavSquare(candidate))
					{
						Vector corrected = pArea->getNearestPoint(Vector2D(candidate.x, candidate.y));
						corrected.z = pArea->m_center.z;
						candidate = corrected;
					}
				}
				return candidate;
			};

			std::vector<Vector> vFallbackCandidates;
			vFallbackCandidates.reserve(iSlots + 12);

			for (int offset = 0; offset < iSlots; ++offset)
			{
				int slotIndex = (iPreferredSlot + offset) % iSlots;
				float t = static_cast<float>(slotIndex) / static_cast<float>(iSlots);
				float flAngle = t * PI * 2.0f;

				Vector candidate = *vPosition;
				if (flBaseRadius > 1.0f)
				{
					candidate.x += cos(flAngle) * flBaseRadius;
					candidate.y += sin(flAngle) * flBaseRadius;
				}

				candidate = adjustToNav(candidate);
				vFallbackCandidates.push_back(candidate);

				if (!spotTakenByOther(candidate))
				{
					m_vCurrentCaptureSpot = candidate;
					break;
				}
			}

			if (!m_vCurrentCaptureSpot)
			{
				for (int ring = 1; ring <= 2; ++ring)
				{
					float flRingRadius = std::min(flCapRadius - 12.0f, flBaseRadius + 14.0f * static_cast<float>(ring));
					int iSegments = std::max(6, iSlots + ring * 2);
					for (int seg = 0; seg < iSegments; ++seg)
					{
						float flAngle = (static_cast<float>(seg) / iSegments) * PI * 2.0f;
						Vector candidate = *vPosition;
						if (flRingRadius > 1.0f)
						{
							candidate.x += cos(flAngle) * flRingRadius;
							candidate.y += sin(flAngle) * flRingRadius;
						}

						candidate = adjustToNav(candidate);
						vFallbackCandidates.push_back(candidate);

						if (!spotTakenByOther(candidate))
						{
							m_vCurrentCaptureSpot = candidate;
							break;
						}
					}
					if (m_vCurrentCaptureSpot)
						break;
				}
			}

			if (!m_vCurrentCaptureSpot)
			{
				vFallbackCandidates.push_back(adjustToNav(*vPosition));

				Vector vBestCandidate = *vPosition;
				float flBestScore = -1.0f;
				for (const auto& candidate : vFallbackCandidates)
				{
					float flScore = closestTeammateDistanceSq(candidate);
					if (flScore > flBestScore)
					{
						flBestScore = flScore;
						vBestCandidate = candidate;
					}
				}

				m_vCurrentCaptureSpot = vBestCandidate;
			}
		}

		if (m_vCurrentCaptureSpot)
			vAdjustedPos = m_vCurrentCaptureSpot.value();
	}

	if (vLocalOrigin.DistToSqr(vAdjustedPos) <= pow(150.0f, 2))
		vAdjustedPos.z = vLocalOrigin.z;

	Vector2D vFlatDelta(vAdjustedPos.x - vLocalOrigin.x, vAdjustedPos.y - vLocalOrigin.y);
	if (vFlatDelta.LengthSqr() <= pow(45.0f, 2))
	{
		m_bOverwriteCapture = true;
		return std::nullopt;
	}

	return vAdjustedPos;
}

std::optional<Vector> CNavBot::GetDoomsdayGoal(CTFPlayer* pLocal, int iOurTeam, int iEnemyTeam)
{
	int iTeam = TEAM_UNASSIGNED;
	while (iTeam != -1)
	{
		auto tFlag = F::FlagController.GetFlag(iTeam);
		if (tFlag.m_pFlag)
			break;

		iTeam = iTeam != iOurTeam ? iOurTeam : -1;
	}

	// No australium found
	if (iTeam == -1)
		return std::nullopt;

	// Get Australium related information
	auto iStatus = F::FlagController.GetStatus(iTeam);
	auto vPosition = F::FlagController.GetPosition(iTeam);
	auto iCarrierIdx = F::FlagController.GetCarrier(iTeam);

	if (iStatus == TF_FLAGINFO_STOLEN)
	{
		// We have the australium
		if (iCarrierIdx == pLocal->entindex())
		{
			// Get rocket position - in Doomsday it's marked as a cap point
			auto vRocket = F::CPController.GetClosestControlPoint(pLocal->GetAbsOrigin(), iOurTeam);
			if (vRocket)
			{
				// If close enough, don't move
				if (vRocket->DistToSqr(pLocal->GetAbsOrigin()) <= pow(50.0f, 2))
				{
					m_bOverwriteCapture = true;
					return std::nullopt;
				}

				// Check for enemies near the capture point that might intercept
				bool bEnemiesNearRocket = false;
				constexpr float flThreatRadius = 500.0f; // Distance to check for enemies
				
				for (auto pEntity : H::Entities.GetGroup(EGroupType::PLAYERS_ENEMIES))
				{
					if (pEntity->IsDormant())
						continue;

					auto pEnemy = pEntity->As<CTFPlayer>();
					if (!pEnemy->IsAlive())
						continue;

					if (pEnemy->GetAbsOrigin().DistToSqr(*vRocket) <= pow(flThreatRadius, 2))
					{
						bEnemiesNearRocket = true;
						break;
					}
				}

				// If enemies are near the rocket, stay back a bit until teammates can help
				if (bEnemiesNearRocket && (Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::DefendObjectives))
				{
					// Find a safer approach path or wait for teammates
					auto vPathToRocket = *vRocket - pLocal->GetAbsOrigin();
					float pathLen = vPathToRocket.Length();
					if (pathLen > 0.001f) {
						vPathToRocket.x /= pathLen;
						vPathToRocket.y /= pathLen;
						vPathToRocket.z /= pathLen;
					}
					
					// Back up a bit from the rocket
					Vector vSaferPosition = *vRocket - (vPathToRocket * 300.0f);
					return vSaferPosition;
				}

				return vRocket;
			}
		}
		// Help friendly carrier
		else if (Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::HelpCaptureObjectives)
		{
			if (ShouldAssist(pLocal, iCarrierIdx))
			{
				// Check if carrier is navigating to the rocket
				auto pCarrier = I::ClientEntityList->GetClientEntity(iCarrierIdx);
				if (pCarrier && !pCarrier->IsDormant())
				{
					// Stay slightly behind and to the side to avoid blocking
					if (vPosition)
					{
						// Try to position strategically to protect the carrier
						auto vRocket = F::CPController.GetClosestControlPoint(pCarrier->GetAbsOrigin(), iOurTeam);
						if (vRocket)
						{
							Vector vCarrierToRocket = *vRocket - pCarrier->GetAbsOrigin();
							float len = vCarrierToRocket.Length();
							if (len > 0.001f) {
								vCarrierToRocket.x /= len;
								vCarrierToRocket.y /= len;
								vCarrierToRocket.z /= len;
							}
							
							// Position to the side and slightly behind the carrier in the direction of the rocket
							Vector vCrossProduct = vCarrierToRocket.Cross(Vector(0, 0, 1));
							float crossLen = vCrossProduct.Length();
							if (crossLen > 0.001f) {
								vCrossProduct.x /= crossLen;
								vCrossProduct.y /= crossLen;
								vCrossProduct.z /= crossLen;
							}
							
							// Position offset from carrier toward rocket but slightly to the side
							Vector vOffset = (vCarrierToRocket * -80.0f) + (vCrossProduct * 60.0f);
							return pCarrier->GetAbsOrigin() + vOffset;
						}
						
						// Default offset if rocket position not found
						Vector vOffset(40.0f, 40.0f, 0.0f);
						*vPosition -= vOffset;
					}
					return vPosition;
				}
			}
		}
	}

	// If nobody has the australium, look for it
	if (vPosition)
	{
		// Check if enemies are near the australium
		bool bEnemiesNearAustralium = false;
		constexpr float flThreatRadius = 600.0f;
		
		for (auto pEntity : H::Entities.GetGroup(EGroupType::PLAYERS_ENEMIES))
		{
			if (pEntity->IsDormant())
				continue;

			auto pEnemy = pEntity->As<CTFPlayer>();
			if (!pEnemy->IsAlive())
				continue;

			if (pEnemy->GetAbsOrigin().DistToSqr(*vPosition) <= pow(flThreatRadius, 2))
			{
				bEnemiesNearAustralium = true;
				break;
			}
		}
		
		// If enemies are near and we're not close, approach carefully
		if (bEnemiesNearAustralium && pLocal->GetAbsOrigin().DistToSqr(*vPosition) > pow(300.0f, 2))
		{
			// Try to find a safer approach path
			auto pClosestNav = F::NavEngine.findClosestNavSquare(*vPosition);
			if (pClosestNav)
			{
				std::optional<Vector> vVischeckPoint = *vPosition;
				if (auto vHidingSpot = FindClosestHidingSpot(pClosestNav, vVischeckPoint, 5))
				{
					return (*vHidingSpot).first->m_center;
				}
			}
		}
	}

	// Get the australium if not taken
	return vPosition;
}

bool CNavBot::CaptureObjectives(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	static Timer tCaptureTimer;
	static Vector vPreviousTarget;

	if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::CaptureObjectives))
		return false;

	if (const auto& pGameRules = I::TFGameRules())
	{
		if (!((pGameRules->m_iRoundState() == GR_STATE_RND_RUNNING || pGameRules->m_iRoundState() == GR_STATE_STALEMATE) && !pGameRules->m_bInWaitingForPlayers())
			|| pGameRules->m_iRoundState() == GR_STATE_TEAM_WIN
			|| pGameRules->m_bPlayingSpecialDeliveryMode())
			return false;
	}

	if (!tCaptureTimer.Check(2.f))
		return F::NavEngine.current_priority == capture;

	// Priority too high, don't try
	if (F::NavEngine.current_priority > capture)
		return false;

	// Where we want to go
	std::optional<Vector> vTarget;

	int iOurTeam = pLocal->m_iTeamNum();
	int iEnemyTeam = iOurTeam == TF_TEAM_BLUE ? TF_TEAM_RED : TF_TEAM_BLUE;

	m_bOverwriteCapture = false;

	const auto vLocalOrigin = pLocal->GetAbsOrigin();

	// Run logic
	switch (F::GameObjectiveController.m_eGameMode)
	{
	case TF_GAMETYPE_CTF:
		vTarget = GetCtfGoal(pLocal, iOurTeam, iEnemyTeam);
		break;
	case TF_GAMETYPE_CP:
		vTarget = GetControlPointGoal(vLocalOrigin, iOurTeam);
		break;
	case TF_GAMETYPE_ESCORT:
		vTarget = GetPayloadGoal(vLocalOrigin, iOurTeam);
		break;
	default:
		if (F::GameObjectiveController.m_bDoomsday)
		{
			vTarget = GetDoomsdayGoal(pLocal, iOurTeam, iEnemyTeam);
		}
		break;
	}

	// Overwritten, for example because we are currently on the payload, cancel any sort of pathing and return true
	if (m_bOverwriteCapture)
	{
		F::NavEngine.cancelPath();
		return true;
	}
	// No target, bail and set on cooldown
	else if (!vTarget)
	{
		tCaptureTimer.Update();
		return F::NavEngine.current_priority == capture;
	}
	// If priority is not capturing, or we have a new target, try to path there
	else if (F::NavEngine.current_priority != capture || *vTarget != vPreviousTarget)
	{
		if (F::NavEngine.navTo(*vTarget, capture, true, !F::NavEngine.isPathing()))
		{
			vPreviousTarget = *vTarget;
			return true;
		}
		tCaptureTimer.Update();
	}
	return false;
}

bool CNavBot::Roam(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
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

	if (F::NavEngine.current_priority > patrol)
		return false;

	// Defend our objective if possible
	if (Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::DefendObjectives)
	{
		int iEnemyTeam = pLocal->m_iTeamNum() == TF_TEAM_BLUE ? TF_TEAM_RED : TF_TEAM_BLUE;

		std::optional<Vector> vTarget;
		const auto vLocalOrigin = pLocal->GetAbsOrigin();

		switch (F::GameObjectiveController.m_eGameMode)
		{
		case TF_GAMETYPE_CP:
			vTarget = GetControlPointGoal(vLocalOrigin, iEnemyTeam);
			break;
		case TF_GAMETYPE_ESCORT:
			vTarget = GetPayloadGoal(vLocalOrigin, iEnemyTeam);
			break;
		default:
			break;
		}
		if (vTarget)
		{
			if (auto pClosestNav = F::NavEngine.findClosestNavSquare(*vTarget))
			{
				// Get closest enemy to vicheck
				CBaseEntity* pClosestEnemy = nullptr;
				float flBestDist = FLT_MAX;
				for (auto pEntity : H::Entities.GetGroup(EGroupType::PLAYERS_ENEMIES))
				{
					if (!ShouldTarget(pLocal, pWeapon, pEntity->entindex()))
						continue;

					float flDist = pEntity->GetAbsOrigin().DistToSqr(pClosestNav->m_center);
					if (flDist > pow(flBestDist, 2))
						continue;

					flBestDist = flDist;
					pClosestEnemy = pEntity;
				}

				std::optional<Vector> vVischeckPoint;
				if (pClosestEnemy && flBestDist <= 1000.f)
				{
					vVischeckPoint = pClosestEnemy->GetAbsOrigin();
					vVischeckPoint->z += PLAYER_JUMP_HEIGHT;
				}

				if (auto vClosestSpot = FindClosestHidingSpot(pClosestNav, vVischeckPoint, 5))
				{
					if ((*vClosestSpot).first->m_center.DistToSqr(vLocalOrigin) <= pow(250.0f, 2))
					{
						F::NavEngine.cancelPath();
						m_bDefending = true;
						return true;
					}
					if (F::NavEngine.navTo((*vClosestSpot).first->m_center, patrol, true, !F::NavEngine.isPathing()))
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
	if (pCurrentTargetArea && F::NavEngine.current_priority == patrol)
		return true;

	// Reset current target
	pCurrentTargetArea = nullptr;

	std::vector<CNavArea*> vValidAreas;

	// Get all nav areas
	for (auto& tArea : F::NavEngine.getNavFile()->m_areas)
	{
		// Skip if area is blacklisted
		if (F::NavEngine.getFreeBlacklist()->find(&tArea) != F::NavEngine.getFreeBlacklist()->end())
			continue;

		// Dont run in spawn bitch
		if (tArea.m_TFattributeFlags & (TF_NAV_SPAWN_ROOM_BLUE | TF_NAV_SPAWN_ROOM_RED | TF_NAV_SPAWN_ROOM_EXIT))
			continue;

		// Skip if we recently visited this area
		if (std::find(vVisitedAreas.begin(), vVisitedAreas.end(), &tArea) != vVisitedAreas.end())
			continue;

		// Skip areas that are too close
		float flDist = tArea.m_center.DistToSqr(pLocal->GetAbsOrigin());
		if (flDist < pow(500.0f, 2))
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
		float flDist = pArea->m_center.DistToSqr(pLocal->GetAbsOrigin());
		if (flDist > pow(2000.0f, 2))
			vPotentialTargets.push_back(pArea);
	}

	// Strategy 2: If no far areas found, try areas that are at medium distance
	if (vPotentialTargets.empty())
	{
		for (auto pArea : vValidAreas)
		{
			float flDist = pArea->m_center.DistToSqr(pLocal->GetAbsOrigin());
			if (flDist > pow(1000.0f, 2) && flDist <= pow(2000.0f, 2))
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
		if (F::NavEngine.navTo(pArea->m_center, patrol))
		{
			pCurrentTargetArea = pArea;
			vVisitedAreas.push_back(pArea);
			return true;
		}
	}

	return false;
}

// Check if a position is safe from stickies and projectiles
static bool IsPositionSafe(Vector vPos, int iLocalTeam)
{
	if (!(Vars::Misc::Movement::NavBot::Blacklist.Value & Vars::Misc::Movement::NavBot::BlacklistEnum::Stickies) &&
		!(Vars::Misc::Movement::NavBot::Blacklist.Value & Vars::Misc::Movement::NavBot::BlacklistEnum::Projectiles))
		return true;

	for (auto pEntity : H::Entities.GetGroup(EGroupType::WORLD_PROJECTILES))
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

			float flDist = pEntity->m_vecOrigin().DistToSqr(vPos);
			if (flDist < pow(Vars::Misc::Movement::NavBot::StickyDangerRange.Value, 2))
				return false;
		}

		// Check for rockets and pipes
		if (Vars::Misc::Movement::NavBot::Blacklist.Value & Vars::Misc::Movement::NavBot::BlacklistEnum::Projectiles)
		{
			if (iClassId == ETFClassID::CTFProjectile_Rocket ||
				(iClassId == ETFClassID::CTFGrenadePipebombProjectile && pEntity->As<CTFGrenadePipebombProjectile>()->m_iType() == TF_GL_MODE_REGULAR))
			{
				float flDist = pEntity->m_vecOrigin().DistToSqr(vPos);
				if (flDist < pow(Vars::Misc::Movement::NavBot::ProjectileDangerRange.Value, 2))
					return false;
			}
		}
	}
	return true;
}

bool CNavBot::EscapeProjectiles(CTFPlayer* pLocal)
{
	if (!(Vars::Misc::Movement::NavBot::Blacklist.Value & Vars::Misc::Movement::NavBot::BlacklistEnum::Stickies) &&
		!(Vars::Misc::Movement::NavBot::Blacklist.Value & Vars::Misc::Movement::NavBot::BlacklistEnum::Projectiles))
		return false;

	// Don't interrupt higher priority tasks
	if (F::NavEngine.current_priority > danger)
		return false;

	// Check if current position is unsafe
	if (IsPositionSafe(pLocal->GetAbsOrigin(), pLocal->m_iTeamNum()))
	{
		if (F::NavEngine.current_priority == danger)
			F::NavEngine.cancelPath();
		return false;
	}

	auto pLocalNav = F::NavEngine.findClosestNavSquare(pLocal->GetAbsOrigin());
	if (!pLocalNav)
		return false;

	// Find safe nav areas sorted by distance
	std::vector<std::pair<CNavArea*, float>> vSafeAreas;
	for (auto& tArea : F::NavEngine.getNavFile()->m_areas)
	{
		// Skip current area
		if (&tArea == pLocalNav)
			continue;

		// Skip if area is blacklisted
		if (F::NavEngine.getFreeBlacklist()->find(&tArea) != F::NavEngine.getFreeBlacklist()->end())
			continue;

		if (IsPositionSafe(tArea.m_center, pLocal->m_iTeamNum()))
		{
			float flDist = tArea.m_center.DistToSqr(pLocal->GetAbsOrigin());
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
		if (F::NavEngine.navTo(pArea.first->m_center, danger))
			return true;
	}

	return false;
}

bool CNavBot::EscapeDanger(CTFPlayer* pLocal)
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
	if (F::NavEngine.current_priority > danger || F::NavEngine.current_priority == prio_melee || F::NavEngine.current_priority == run_safe_reload)
		return false;

	auto pLocalNav = F::NavEngine.findClosestNavSquare(pLocal->GetAbsOrigin());

	// Check if we're in spawn - if so, ignore danger and focus on getting out
	if (pLocalNav && (pLocalNav->m_TFattributeFlags & TF_NAV_SPAWN_ROOM_RED || pLocalNav->m_TFattributeFlags & TF_NAV_SPAWN_ROOM_BLUE))
		return false;

	auto pBlacklist = F::NavEngine.getFreeBlacklist();
	
	// Check if we're in any danger
	bool bInHighDanger = false;
	bool bInMediumDanger = false;
	bool bInLowDanger = false;
	
	if (pBlacklist && pBlacklist->contains(pLocalNav))
	{
		// Check building spot - don't run away from that
		if ((*pBlacklist)[pLocalNav].value == BR_BAD_BUILDING_SPOT)
			return false;
			
		// Determine danger level
		switch ((*pBlacklist)[pLocalNav].value)
		{
		case BR_SENTRY:
		case BR_STICKY:
		case BR_ENEMY_INVULN:
			bInHighDanger = true;
			break;
		case BR_SENTRY_MEDIUM:
		case BR_ENEMY_NORMAL:
			bInMediumDanger = true;
			break;
		case BR_SENTRY_LOW:
		case BR_ENEMY_DORMANT:
			bInLowDanger = true;
			break;
		}
		
		// Only escape from high danger by default
		// Also escape from medium danger if health is low
		bool bShouldEscape = bInHighDanger || 
		                    (bInMediumDanger && pLocal->m_iHealth() < pLocal->GetMaxHealth() * 0.5f);
		
		// If we're not in high danger and on an important task, we might not need to escape
		bool bImportantTask = (F::NavEngine.current_priority == capture || 
		                      F::NavEngine.current_priority == health ||
		                      F::NavEngine.current_priority == engineer);
		
		if (!bShouldEscape && bImportantTask)
			return false;
		
		// If we're in low danger only and on any task, don't escape
		if (bInLowDanger && !bInMediumDanger && !bInHighDanger && F::NavEngine.current_priority != 0)
			return false;

		static CNavArea* pTargetArea = nullptr;
		// Already running and our target is still valid
		if (F::NavEngine.current_priority == danger && !pBlacklist->contains(pTargetArea))
			return true;

		// Determine the reference position to stay close to
		Vector vReferencePosition;
		bool bHasTarget = false;

		// If we were pursuing a specific objective or following a target, try to stay close to it
		if (F::NavEngine.current_priority != 0 && F::NavEngine.current_priority != danger && !F::NavEngine.crumbs.empty())
		{
			// Use the last crumb in our path as the reference position
			vReferencePosition = F::NavEngine.crumbs.back().vec;
			bHasTarget = true;
		}
		else
		{
			// Use current position if we don't have a target
			vReferencePosition = pLocal->GetAbsOrigin();
		}

		std::vector<std::pair<CNavArea*, float>> vSafeAreas;
		// Copy areas and calculate distances
		for (auto& tArea : F::NavEngine.getNavFile()->m_areas)
		{
			// Skip if area is blacklisted with high danger
			auto it = pBlacklist->find(&tArea);
			if (it != pBlacklist->end())
			{
				// Check danger level - allow pathing through medium or low danger if we have a target
				BlacklistReason_enum danger = it->second.value;
				
				// Skip high danger areas
				if (danger == BR_SENTRY || danger == BR_STICKY || danger == BR_ENEMY_INVULN)
					continue;
					
				// Skip medium danger areas if we don't have a target or have low health
				if ((danger == BR_SENTRY_MEDIUM || danger == BR_ENEMY_NORMAL) && 
				    (!bHasTarget || pLocal->m_iHealth() < pLocal->GetMaxHealth() * 0.5f))
					continue;
			}

			float flDistToReference = tArea.m_center.DistToSqr(vReferencePosition);
			float flDistToCurrent = tArea.m_center.DistToSqr(pLocal->GetAbsOrigin());
			
			// Only consider areas that are not too far away and reachable
			if (flDistToCurrent < pow(2000.0f, 2))
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
			for (auto pEntity : H::Entities.GetGroup(EGroupType::PLAYERS_ENEMIES))
			{
				if (!ShouldTarget(pLocal, pLocal->m_hActiveWeapon().Get()->As<CTFWeaponBase>(), pEntity->entindex()))
					continue;

				// If enemy is too close to this area, mark it as unsafe
				float flDist = pEntity->GetAbsOrigin().DistToSqr(pArea.first->m_center);
				if (flDist < pow(m_tSelectedConfig.m_flMinFullDanger * 1.2f, 2))
				{
					bIsSafe = false;
					break;
				}
			}

			// Skip unsafe areas
			if (!bIsSafe)
				continue;

			if (F::NavEngine.navTo(pArea.first->m_center, danger))
			{
				pTargetArea = pArea.first;
				return true;
			}
		}

		// If we couldn't find a safe area close to the target, fall back to any safe area
		if (iCalls <= 0 || (bInHighDanger && iCalls < 10))
		{
			std::vector<CNavArea*> vAreaPointers;
			// Get all areas
			for (auto& tArea : F::NavEngine.getNavFile()->m_areas)
				vAreaPointers.push_back(&tArea);

			// Sort by distance to player
			std::sort(vAreaPointers.begin(), vAreaPointers.end(), [&](CNavArea* a, CNavArea* b) -> bool
			{
				return a->m_center.DistToSqr(pLocal->GetAbsOrigin()) < b->m_center.DistToSqr(pLocal->GetAbsOrigin());
			});

			// Try to path to any non-blacklisted area
			for (auto& pArea : vAreaPointers)
			{
				auto it = pBlacklist->find(pArea);
				if (it == pBlacklist->end() || 
				   (bInHighDanger && (it->second.value == BR_SENTRY_LOW || it->second.value == BR_ENEMY_DORMANT)))
				{
					iCalls++;
					if (iCalls > 5)
						break;
					if (F::NavEngine.navTo(pArea->m_center, danger))
					{
						pTargetArea = pArea;
						return true;
					}
				}
			}
		}
	}
	// No longer in danger
	else if (F::NavEngine.current_priority == danger)
		F::NavEngine.cancelPath();

	return false;
}

bool CNavBot::EscapeSpawn(CTFPlayer* pLocal)
{
	static Timer tSpawnEscapeCooldown{};

	// Don't try too often
	if (!tSpawnEscapeCooldown.Run(2.f))
		return F::NavEngine.current_priority == escape_spawn;

	auto pLocalNav = F::NavEngine.findClosestNavSquare(pLocal->GetAbsOrigin());
	if (!pLocalNav)
		return false;

	// Cancel if we're not in spawn and this is running
	if (!(pLocalNav->m_TFattributeFlags & (TF_NAV_SPAWN_ROOM_RED | TF_NAV_SPAWN_ROOM_BLUE | TF_NAV_SPAWN_ROOM_EXIT)))
	{
		if (F::NavEngine.current_priority == escape_spawn)
			F::NavEngine.cancelPath();
		return false;
	}

	// Try to find an exit
	for (auto tArea : F::NavEngine.getNavFile()->m_areas)
	{
		// Skip spawn areas
		if (tArea.m_TFattributeFlags & (TF_NAV_SPAWN_ROOM_RED | TF_NAV_SPAWN_ROOM_BLUE | TF_NAV_SPAWN_ROOM_EXIT))
			continue;

		// Try to get there
		if (F::NavEngine.navTo(tArea.m_center, escape_spawn))
			return true;
	}

	return false;
}

slots CNavBot::GetBestSlot(CTFPlayer* pLocal, slots eActiveSlot, ClosestEnemy_t tClosestEnemy)
{
	if (Vars::Misc::Movement::NavBot::WeaponSlot.Value != Vars::Misc::Movement::NavBot::WeaponSlotEnum::Best)
		return static_cast<slots>(Vars::Misc::Movement::NavBot::WeaponSlot.Value - 1);

	if (pLocal && pLocal->IsAlive())
	{
		auto pClosestEnemy = I::ClientEntityList->GetClientEntity(tClosestEnemy.m_iEntIdx)->As<CTFPlayer>();
		auto pPrimaryWeapon = pLocal->GetWeaponFromSlot(SLOT_PRIMARY);
		auto pSecondaryWeapon = pLocal->GetWeaponFromSlot(SLOT_SECONDARY);
		if (pPrimaryWeapon && pSecondaryWeapon)
		{
			int iPrimaryResAmmo = SDK::WeaponDoesNotUseAmmo(pPrimaryWeapon) ? -1 : pLocal->GetAmmoCount(pPrimaryWeapon->m_iPrimaryAmmoType());
			int iSecondaryResAmmo = SDK::WeaponDoesNotUseAmmo(pSecondaryWeapon) ? -1 : pLocal->GetAmmoCount(pSecondaryWeapon->m_iPrimaryAmmoType());
			switch (pLocal->m_iClass())
			{
			case TF_CLASS_SCOUT:
			{
				if ((!G::AmmoInSlot[SLOT_PRIMARY] && (!G::AmmoInSlot[SLOT_SECONDARY] || (iSecondaryResAmmo != -1 &&
					iSecondaryResAmmo <= SDK::GetWeaponMaxReserveAmmo(G::SavedWepIds[SLOT_SECONDARY], G::SavedDefIndexes[SLOT_SECONDARY]) / 4))) && tClosestEnemy.m_flDist <= pow(200.f, 2))
					return melee;

				if (G::AmmoInSlot[SLOT_PRIMARY] && tClosestEnemy.m_flDist <= pow(800.f, 2))
					return primary;

				else if (G::AmmoInSlot[SLOT_SECONDARY])
					return secondary;

				break;
			}
			case TF_CLASS_HEAVY:
			{
				if (!G::AmmoInSlot[SLOT_PRIMARY] && (!G::AmmoInSlot[SLOT_SECONDARY] && iSecondaryResAmmo == 0) ||
					(G::SavedDefIndexes[SLOT_MELEE] == Heavy_t_TheHolidayPunch && (pClosestEnemy && !pClosestEnemy->IsTaunting() && pClosestEnemy->IsInvulnerable()) && tClosestEnemy.m_flDist < pow(400.f, 2)))
					return melee;

				else if ((!pClosestEnemy || tClosestEnemy.m_flDist <= pow(900.f, 2)) && G::AmmoInSlot[SLOT_PRIMARY])
					return primary;

				else if (G::AmmoInSlot[SLOT_SECONDARY] && G::SavedWepIds[SLOT_SECONDARY] == TF_WEAPON_SHOTGUN_HWG)
					return secondary;

				break;
			}
			case TF_CLASS_MEDIC:
			{
				/*if ( AutoMedic->FoundTarget )
				{
					return secondary;
				}*/

				if (!G::AmmoInSlot[SLOT_PRIMARY] ||
					(tClosestEnemy.m_flDist <= pow(400.f, 2) && pClosestEnemy/* && !pPlayer->IsInvulnerable( )*/))
					return melee;

				return primary;
			}
			case TF_CLASS_SPY:
			{
				if (tClosestEnemy.m_flDist <= pow(250.0f, 2) && pClosestEnemy/* && !pPlayer->IsInvulnerable( )*/)
					return melee;
				else if (G::AmmoInSlot[SLOT_PRIMARY] || iPrimaryResAmmo)
					return primary;

				break;
			}
			case TF_CLASS_SNIPER:
			{
				// We have already skipped the targets which are invulnerable
				// bool bPlayerInvuln = pPlayer && pPlayer->IsInvulnerable( );
				int iPlayerLowHp = pClosestEnemy ? (pClosestEnemy->m_iHealth() < pClosestEnemy->GetMaxHealth() * 0.35f ? 2 : pClosestEnemy->m_iHealth() < pClosestEnemy->GetMaxHealth() * 0.75f) : -1;

				if (!G::AmmoInSlot[SLOT_PRIMARY] && !G::AmmoInSlot[SLOT_SECONDARY] ||
					(tClosestEnemy.m_flDist <= pow(200.0f, 2) && pClosestEnemy/*&& !bPlayerInvuln*/))
					return melee;

				if ((G::AmmoInSlot[SLOT_SECONDARY] || iSecondaryResAmmo) &&
					(tClosestEnemy.m_flDist <= pow(300.0f, 2) && iPlayerLowHp > 1 /*&& !bPlayerInvuln*/))
					return secondary;

				// Keep the smg if the target we previosly tried shooting is running away
				else if (eActiveSlot && eActiveSlot < 3 && G::AmmoInSlot[eActiveSlot - 1] &&
						 (tClosestEnemy.m_flDist <= pow(800.0f, 2) && iPlayerLowHp > 1 /*&& !bPlayerInvuln*/))
					break;

				else if (G::AmmoInSlot[SLOT_PRIMARY])
					return primary;

				break;
			}
			case TF_CLASS_PYRO:
			{
				if (!G::AmmoInSlot[SLOT_PRIMARY] && (!G::AmmoInSlot[SLOT_SECONDARY] && iSecondaryResAmmo != -1 &&
					iSecondaryResAmmo <= SDK::GetWeaponMaxReserveAmmo(G::SavedWepIds[SLOT_SECONDARY], G::SavedDefIndexes[SLOT_SECONDARY]) / 4) &&
					(pClosestEnemy && tClosestEnemy.m_flDist <= pow(300.0f, 2)))
					return melee;

				if (G::AmmoInSlot[SLOT_PRIMARY] && (tClosestEnemy.m_flDist <= pow(550.0f, 2) || !pClosestEnemy))
					return primary;
				else if (G::AmmoInSlot[SLOT_SECONDARY])
					return secondary;

				break;
			}
			case TF_CLASS_SOLDIER:
			{
				auto pEnemyWeapon = pClosestEnemy ? pClosestEnemy->m_hActiveWeapon().Get()->As<CTFWeaponBase>() : nullptr;
				bool bEnemyCanAirblast = pEnemyWeapon && pEnemyWeapon->GetWeaponID() == TF_WEAPON_FLAMETHROWER && pEnemyWeapon->m_iItemDefinitionIndex() != Pyro_m_ThePhlogistinator;
				bool bEnemyClose = pClosestEnemy && tClosestEnemy.m_flDist <= pow(250.0f, 2);
				if ((eActiveSlot != primary || !G::AmmoInSlot[SLOT_PRIMARY] && iPrimaryResAmmo == 0) && bEnemyClose && (pClosestEnemy->m_iHealth() < 80 ? !G::AmmoInSlot[SLOT_SECONDARY] : pClosestEnemy->m_iHealth() >= 150 || G::AmmoInSlot[SLOT_SECONDARY] < 2))
					return melee;

				if (G::AmmoInSlot[SLOT_SECONDARY] && (bEnemyCanAirblast || (tClosestEnemy.m_flDist <= pow(350.0f, 2) && pClosestEnemy && pClosestEnemy->m_iHealth() <= 125)))
					return secondary;
				else if (G::AmmoInSlot[SLOT_PRIMARY])
					return primary;

				break;
			}
			case TF_CLASS_DEMOMAN:
			{
				if (!G::AmmoInSlot[SLOT_PRIMARY] && !G::AmmoInSlot[SLOT_SECONDARY] && (pClosestEnemy && tClosestEnemy.m_flDist <= pow(200.0f, 2)))
					return melee;

				if (G::AmmoInSlot[SLOT_PRIMARY] && (tClosestEnemy.m_flDist <= pow(800.0f, 2)))
					return primary;
				else if (G::AmmoInSlot[SLOT_SECONDARY] || iSecondaryResAmmo >= SDK::GetWeaponMaxReserveAmmo(G::SavedWepIds[SLOT_SECONDARY], G::SavedDefIndexes[SLOT_SECONDARY]) / 2)
					return secondary;

				break;
			}
			case TF_CLASS_ENGINEER:
			{
				auto pSentry = I::ClientEntityList->GetClientEntity(m_iMySentryIdx);
				auto pDispenser = I::ClientEntityList->GetClientEntity(m_iMyDispenserIdx);
				if (((pSentry && pSentry->GetAbsOrigin().DistToSqr(pLocal->GetAbsOrigin()) <= pow(300.0f, 2)) || (pDispenser && pDispenser->GetAbsOrigin().DistToSqr(pLocal->GetAbsOrigin()) <= pow(500.0f, 2))) || (vCurrentBuildingSpot && vCurrentBuildingSpot->DistToSqr(pLocal->GetAbsOrigin()) <= pow(500.0f, 2)))
				{
					if (eActiveSlot >= melee && F::NavEngine.current_priority != prio_melee)
						return eActiveSlot;
					else
						return melee;
				}
				else if (!G::AmmoInSlot[SLOT_PRIMARY] && !G::AmmoInSlot[SLOT_SECONDARY] && (pClosestEnemy && tClosestEnemy.m_flDist <= pow(200.0f, 2)))
					return melee;
				else if ((G::AmmoInSlot[SLOT_PRIMARY] || iPrimaryResAmmo) && (pClosestEnemy && tClosestEnemy.m_flDist <= pow(500.0f, 2)))
					return primary;
				else if (G::AmmoInSlot[SLOT_SECONDARY] || iSecondaryResAmmo)
					return secondary;

				break;
			}
			default:
				break;
			}
		}
	}
	return eActiveSlot;
}

void CNavBot::UpdateSlot(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, ClosestEnemy_t tClosestEnemy)
{
	static Timer tSlotTimer{};
	if (!tSlotTimer.Run(0.2f))
		return;

	m_iCurrentSlot = pWeapon->GetSlot() + 1;

	// Prioritize reloading
	int iReloadSlot = GetReloadWeaponSlot(pLocal, tClosestEnemy);
	m_eLastReloadSlot = static_cast<slots>(iReloadSlot);

	int iNewSlot = iReloadSlot ? iReloadSlot : (Vars::Misc::Movement::NavBot::WeaponSlot.Value ? GetBestSlot(pLocal, static_cast<slots>(m_iCurrentSlot), tClosestEnemy) : -1);
	if (iNewSlot > -1)
	{
		auto sCommand = "slot" + std::to_string(iNewSlot);
		if (m_iCurrentSlot != iNewSlot)
			I::EngineClient->ClientCmd_Unrestricted(sCommand.c_str());
	}
}

void CNavBot::AutoScope(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	static bool bKeep = false;
	static bool bShouldClearCache = false;
	static Timer tScopeTimer{};
	bool bIsClassic = pWeapon->GetWeaponID() == TF_WEAPON_SNIPERRIFLE_CLASSIC;
	if (!Vars::Misc::Movement::NavBot::AutoScope.Value || pWeapon->GetWeaponID() != TF_WEAPON_SNIPERRIFLE && !bIsClassic && pWeapon->GetWeaponID() != TF_WEAPON_SNIPERRIFLE_DECAP)
	{
		bKeep = false;
		m_mAutoScopeCache.clear();
		return;
	}

	if (!Vars::Misc::Movement::NavBot::AutoScopeUseCachedResults.Value)
		bShouldClearCache = true;

	if (bShouldClearCache)
	{
		m_mAutoScopeCache.clear();
		bShouldClearCache = false;
	}
	else if (m_mAutoScopeCache.size())
		bShouldClearCache = true;

	if (bIsClassic)
	{
		if (bKeep)
		{
			if (!(pCmd->buttons & IN_ATTACK))
				pCmd->buttons |= IN_ATTACK;
			if (tScopeTimer.Check(Vars::Misc::Movement::NavBot::AutoScopeCancelTime.Value)) // cancel classic charge
				pCmd->buttons |= IN_JUMP;
		}
		if (!pLocal->OnSolid() && !(pCmd->buttons & IN_ATTACK))
			bKeep = false;
	}
	else
	{
		if (bKeep)
		{
			if (pLocal->InCond(TF_COND_ZOOMED))
			{
				if (tScopeTimer.Check(Vars::Misc::Movement::NavBot::AutoScopeCancelTime.Value))
				{
					bKeep = false;
					pCmd->buttons |= IN_ATTACK2;
					return;
				}
			}
		}
	}

	CNavArea* pCurrentDestinationArea = nullptr;
	auto pCrumbs = F::NavEngine.getCrumbs();
	if (pCrumbs->size() > 4)
		pCurrentDestinationArea = pCrumbs->at(4).navarea;

	auto vLocalOrigin = pLocal->GetAbsOrigin();
	auto pLocalNav = pCurrentDestinationArea ? pCurrentDestinationArea : F::NavEngine.findClosestNavSquare(vLocalOrigin);
	if (!pLocalNav)
		return;

	Vector vFrom = pLocalNav->m_center;
	vFrom.z += PLAYER_JUMP_HEIGHT;

	std::vector<std::pair<CBaseEntity*, float>> vEnemiesSorted;
	for (auto pEnemy : H::Entities.GetGroup(EGroupType::PLAYERS_ENEMIES))
	{
		if (pEnemy->IsDormant())
			continue;

		if (!ShouldTarget(pLocal, pWeapon, pEnemy->entindex()))
			continue;

		vEnemiesSorted.emplace_back(pEnemy, pEnemy->GetAbsOrigin().DistToSqr(vLocalOrigin));
	}

	for (auto pEnemyBuilding : H::Entities.GetGroup(EGroupType::BUILDINGS_ENEMIES))
	{
		if (pEnemyBuilding->IsDormant())
			continue;

		if (!ShouldTargetBuilding(pLocal, pEnemyBuilding->entindex()))
			continue;

		vEnemiesSorted.emplace_back(pEnemyBuilding, pEnemyBuilding->GetAbsOrigin().DistToSqr(vLocalOrigin));
	}

	if (vEnemiesSorted.empty())
		return;

	std::sort(vEnemiesSorted.begin(), vEnemiesSorted.end(), [&](std::pair<CBaseEntity*, float> a, std::pair<CBaseEntity*, float> b) -> bool { return a.second < b.second; });

	auto CheckVisibility = [&](const Vec3& vTo, int iEntIndex) -> bool
		{
			CGameTrace trace = {};
			CTraceFilterWorldAndPropsOnly filter = {};

			// Trace from local pos first
			SDK::Trace(Vector(vLocalOrigin.x, vLocalOrigin.y, vLocalOrigin.z + PLAYER_JUMP_HEIGHT), vTo, MASK_SHOT | CONTENTS_GRATE, &filter, &trace);
			bool bHit = trace.fraction == 1.0f;
			if (!bHit)
			{
				// Try to trace from our destination pos
				SDK::Trace(vFrom, vTo, MASK_SHOT | CONTENTS_GRATE, &filter, &trace);
				bHit = trace.fraction == 1.0f;
			}

			if (iEntIndex != -1)
				m_mAutoScopeCache[iEntIndex] = bHit;

			if (bHit)
			{
				if (bIsClassic)
					pCmd->buttons |= IN_ATTACK;
				else if (!pLocal->InCond(TF_COND_ZOOMED) && !(pCmd->buttons & IN_ATTACK2))
					pCmd->buttons |= IN_ATTACK2;

				tScopeTimer.Update();
				return bKeep = true;
			}
			return false;
		};

	bool bSimple = Vars::Misc::Movement::NavBot::AutoScope.Value == Vars::Misc::Movement::NavBot::AutoScopeEnum::Simple;

	int iMaxTicks = TIME_TO_TICKS(0.5f);
	PlayerStorage tStorage;
	for (auto [pEnemy, _] : vEnemiesSorted)
	{
		int iEntIndex = Vars::Misc::Movement::NavBot::AutoScopeUseCachedResults.Value ? pEnemy->entindex() : -1;
		if (m_mAutoScopeCache.contains(iEntIndex))
		{
			if (m_mAutoScopeCache[iEntIndex])
			{
				if (bIsClassic)
					pCmd->buttons |= IN_ATTACK;
				else if (!pLocal->InCond(TF_COND_ZOOMED) && !(pCmd->buttons & IN_ATTACK2))
					pCmd->buttons |= IN_ATTACK2;

				tScopeTimer.Update();
				bKeep = true;
				break;
			}
			continue;
		}

		Vector vNonPredictedPos = pEnemy->GetAbsOrigin();
		vNonPredictedPos.z += PLAYER_JUMP_HEIGHT;
		if (CheckVisibility(vNonPredictedPos, iEntIndex))
			return;

		if (!bSimple)
		{
			F::MoveSim.Initialize(pEnemy, tStorage, false);
			if (tStorage.m_bFailed)
			{
				F::MoveSim.Restore(tStorage);
				continue;
			}
	
			for (int i = 0; i < iMaxTicks; i++)
				F::MoveSim.RunTick(tStorage);
		}

		bool bResult = false;
		Vector vPredictedPos = bSimple ? pEnemy->GetAbsOrigin() + pEnemy->GetAbsVelocity() * TICKS_TO_TIME(iMaxTicks) : tStorage.m_vPredictedOrigin;
		
		auto pTargetNav = F::NavEngine.findClosestNavSquare(vPredictedPos);
		if (pTargetNav)
		{
			Vector vTo = pTargetNav->m_center;

			// If player is in the air dont try to vischeck nav areas below him, check the predicted position instead
			if (!pEnemy->As<CBasePlayer>()->OnSolid() && vTo.DistToSqr(vPredictedPos) >= pow(400.f, 2))
				vTo = vPredictedPos;

			vTo.z += PLAYER_JUMP_HEIGHT;
			bResult = CheckVisibility(vTo, iEntIndex);
		}
		if (!bSimple)
			F::MoveSim.Restore(tStorage);

		if (bResult)
			break;
	}
}

bool IsWeaponValidForDT(CTFWeaponBase* pWeapon)
{
	if (!pWeapon)
		return false;

	auto iWepID = pWeapon->GetWeaponID();
	if (iWepID == TF_WEAPON_SNIPERRIFLE || iWepID == TF_WEAPON_SNIPERRIFLE_CLASSIC || iWepID == TF_WEAPON_SNIPERRIFLE_DECAP)
		return false;

	return SDK::WeaponDoesNotUseAmmo(pWeapon, false);
}

void CNavBot::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	static Timer tDoubletapRecharge{};
	if (!Vars::Misc::Movement::NavBot::Enabled.Value || !Vars::Misc::Movement::NavEngine::Enabled.Value || !pLocal->IsAlive() || !F::NavEngine.isReady())
	{
		m_iStayNearTargetIdx = -1;
		m_mAutoScopeCache.clear();
		return;
	}

	if (F::NavEngine.current_priority != staynear)
		m_iStayNearTargetIdx = -1;

	if (F::Ticks.m_bWarp || F::Ticks.m_bDoubletap)
		return;

	if (!pWeapon || !pCmd)
		return;

	if (pCmd->buttons & (IN_FORWARD | IN_BACK | IN_MOVERIGHT | IN_MOVELEFT) && !F::Misc.m_bAntiAFK)
		return;

	AutoScope(pLocal, pWeapon, pCmd);

	UpdateLocalBotPositions(pLocal);
	//Recharge doubletap every n seconds
	if (Vars::Misc::Movement::NavBot::RechargeDT.Value && IsWeaponValidForDT(pWeapon))
	{
		if (!F::Ticks.m_bRechargeQueue &&
			(Vars::Misc::Movement::NavBot::RechargeDT.Value != Vars::Misc::Movement::NavBot::RechargeDTEnum::WaitForFL || !Vars::Fakelag::Fakelag.Value || !F::FakeLag.m_iGoal) &&
			G::Attacking != 1 &&
			(F::Ticks.m_iShiftedTicks < F::Ticks.m_iShiftedGoal) && tDoubletapRecharge.Check(Vars::Misc::Movement::NavBot::RechargeDTDelay.Value))
			F::Ticks.m_bRechargeQueue = true;
		else if (F::Ticks.m_iShiftedTicks >= F::Ticks.m_iShiftedGoal)
			tDoubletapRecharge.Update();
	}

	RefreshSniperSpots();
	RefreshLocalBuildings(pLocal);
	auto tClosestEnemy = GetNearestPlayerDistance(pLocal, pWeapon);
	RefreshBuildingSpots(pLocal, pLocal->GetWeaponFromSlot(SLOT_MELEE), tClosestEnemy);

	// Update the distance config
	switch (pLocal->m_iClass())
	{
	case TF_CLASS_SCOUT:
	case TF_CLASS_HEAVY:
		m_tSelectedConfig = CONFIG_SHORT_RANGE;
		break;
	case TF_CLASS_ENGINEER:
		m_tSelectedConfig = IsEngieMode(pLocal) ? pWeapon->m_iItemDefinitionIndex() == Engi_t_TheGunslinger ? CONFIG_GUNSLINGER_ENGINEER : CONFIG_ENGINEER : CONFIG_SHORT_RANGE;
		break;
	case TF_CLASS_SNIPER:
		m_tSelectedConfig = pWeapon->GetWeaponID() == TF_WEAPON_COMPOUND_BOW ? CONFIG_MID_RANGE : CONFIG_LONG_RANGE;
		break;
	default:
		m_tSelectedConfig = CONFIG_MID_RANGE;
	}

	// Spin up the minigun if there are enemies nearby or if we had an active aimbot target 
	if (pWeapon->GetWeaponID() == TF_WEAPON_MINIGUN)
	{
		static Timer tSpinupTimer{};
		auto pEntity = I::ClientEntityList->GetClientEntity(tClosestEnemy.m_iEntIdx);
		if (pEntity && pEntity->As<CTFPlayer>()->IsAlive() && !pEntity->As<CTFPlayer>()->IsInvulnerable() && pWeapon->HasAmmo())
		{
			if (G::AimTarget.m_iEntIndex && G::AimTarget.m_iDuration || tClosestEnemy.m_flDist <= pow(800.f, 2))
				tSpinupTimer.Update();
			if (!tSpinupTimer.Check(3.f)) // 3 seconds until unrev
				pCmd->buttons |= IN_ATTACK2;
		}
	}

	UpdateSlot(pLocal, pWeapon, tClosestEnemy);
	UpdateEnemyBlacklist(pLocal, pWeapon, m_iCurrentSlot);

	// TODO:
	// Add engie logic and target sentries logic. (Done)
	// Also maybe add some spy sapper logic? (No.)
	// Fix defend and help capture logic
	// Fix reload stuff because its really janky
	// Finish auto wewapon stuff
	// Make a better closest enemy lorgic

	if (EscapeSpawn(pLocal)
		|| EscapeProjectiles(pLocal)
		|| MeleeAttack(pCmd, pLocal, m_iCurrentSlot, tClosestEnemy)
		|| EscapeDanger(pLocal)
		|| GetHealth(pCmd, pLocal)
		|| GetAmmo(pCmd, pLocal)
		//|| RunReload(pLocal, pWeapon)
		|| RunSafeReload(pLocal, pWeapon)
		|| MoveInFormation(pLocal, pWeapon)
		|| CaptureObjectives(pLocal, pWeapon)
		|| EngineerLogic(pCmd, pLocal, tClosestEnemy)
		|| SnipeSentries(pLocal)
		|| StayNear(pLocal, pWeapon)
		|| GetHealth(pCmd, pLocal, true)
		|| Roam(pLocal, pWeapon))
	{
		// Force crithack in dangerous conditions
		// TODO:
		// Maybe add some logic to it (more logic)
		CTFPlayer* pPlayer = nullptr;
		switch (F::NavEngine.current_priority)
		{
		case staynear:
			pPlayer = I::ClientEntityList->GetClientEntity(m_iStayNearTargetIdx)->As<CTFPlayer>();
			if (pPlayer)
				F::CritHack.m_bForce = !pPlayer->IsDormant() && pPlayer->m_iHealth() >= pWeapon->GetDamage();
			break;
		case prio_melee:
		case health:
		case danger:
			pPlayer = I::ClientEntityList->GetClientEntity(tClosestEnemy.m_iEntIdx)->As<CTFPlayer>();
			F::CritHack.m_bForce = pPlayer && !pPlayer->IsDormant() && pPlayer->m_iHealth() >= pWeapon->GetDamage();
			break;
		default:
			F::CritHack.m_bForce = false;
			break;
		}
	}
}

void CNavBot::Reset( )
{
	// Make it run asap
	tRefreshSniperspotsTimer -= 60;
	m_iStayNearTargetIdx = -1;
	m_iMySentryIdx = -1;
	m_iMyDispenserIdx = -1;
	m_vSniperSpots.clear( );
	m_mAutoScopeCache.clear();
	m_vCurrentCaptureSpot.reset();
	m_vCurrentCaptureCenter.reset();
}

void CNavBot::UpdateLocalBotPositions(CTFPlayer* pLocal)
{
	if (!m_tUpdateFormationTimer.Run(0.5f))
		return;

	m_vLocalBotPositions.clear();

	auto pResource = H::Entities.GetResource();
	if (!pResource)
		return;

	int iLocalIdx = pLocal->entindex();
	uint32 uLocalUserID = pResource->m_iUserID(iLocalIdx);
	int iLocalTeam = pLocal->m_iTeamNum();

	// Then check each player
	for (int i = 1; i <= I::EngineClient->GetMaxClients(); i++)
	{
		if (i == iLocalIdx || !pResource->m_bValid(i))
			continue;
#ifdef TEXTMODE
		// Is this a local bot????
		if (!F::NamedPipe.IsLocalBot(pResource->m_iAccountID(i)))
			continue;
#endif

		// Get the player entity
		auto pEntity = I::ClientEntityList->GetClientEntity(i)->As<CBaseEntity>();
		if (!pEntity || pEntity->IsDormant() ||
			!pEntity->IsPlayer() || pEntity->m_iTeamNum() != iLocalTeam)
			continue;

		auto pPlayer = pEntity->As<CTFPlayer>();
		if (!pPlayer->IsAlive())
			continue;

		// Add to our list
		m_vLocalBotPositions.push_back({ pResource->m_iUserID(i), pPlayer->m_vecVelocity() });
	}

	// Sort by friendsID to ensure consistent ordering across all bots
	std::sort(m_vLocalBotPositions.begin(), m_vLocalBotPositions.end(), 
		[](const std::pair<uint32_t, Vector>& a, const std::pair<uint32_t, Vector>& b) {
			return a.first < b.first;
		});

	// Determine our position in the formatin
	m_iPositionInFormation = -1;
	
	// Add ourselves to the list for calculation purposes
	std::vector<uint32_t> vAllBotsInOrder;
	vAllBotsInOrder.push_back(uLocalUserID);
	
	for (const auto& bot : m_vLocalBotPositions)
		vAllBotsInOrder.push_back(bot.first);
	
	// Sort all bots (including us)
	std::sort(vAllBotsInOrder.begin(), vAllBotsInOrder.end());
	
	// Find our pofition
	for (size_t i = 0; i < vAllBotsInOrder.size(); i++)
	{
		if (vAllBotsInOrder[i] == uLocalUserID)
		{
			m_iPositionInFormation = static_cast<int>(i);
			break;
		}
	}
}

std::optional<Vector> CNavBot::GetFormationOffset(CTFPlayer* pLocal, int positionIndex)
{
	if (positionIndex <= 0)
		return std::nullopt; // Leader has no offset
	
	// Calculate the movement direction of the leader
	Vector vLeaderVelocity(0, 0, 0);

	if (!m_vLocalBotPositions.empty())
		vLeaderVelocity = m_vLocalBotPositions[0].second;
	else
	{
		// No leader found, use our own direction
		vLeaderVelocity = pLocal->m_vecVelocity();
	}
	
	// Normalize leader velocity for direction
	Vector vDirection = vLeaderVelocity;
	if (vDirection.Length() < 10.0f) // If leader is barely moving, use view direction
	{
		QAngle viewAngles;
		I::EngineClient->GetViewAngles(viewAngles);
		Math::AngleVectors(viewAngles, &vDirection);
	}
	
	vDirection.z = 0; // Ignore vertical component
	float length = vDirection.Length();
	if (length > 0.001f) {
		vDirection.x /= length;
		vDirection.y /= length;
		vDirection.z /= length;
	}
	
	// Calculate cross product for perpendicular direction (for side-by-side formations)
	Vector vRight = vDirection.Cross(Vector(0, 0, 1));
	// Normalize right vector
	length = vRight.Length();
	if (length > 0.001f) {
		vRight.x /= length;
		vRight.y /= length;
		vRight.z /= length;
	}
	
	// Different formation styles:
	// 1. Line formation (bots following one after another)
	return (vDirection * -m_flFormationDistance * positionIndex);
}

bool CNavBot::MoveInFormation(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::GroupWithOthers))
		return false;
		
	// UpdateLocalBotPositions is called from Run(), so we don't need to call it here
	// If we haven't found a position in formation, we can't move in formation
	if (m_iPositionInFormation < 0 || m_vLocalBotPositions.empty())
		return false;
	
	// If we're the leader, don't move in formation
	if (m_iPositionInFormation == 0)
		return false;
	
	// Get our offset in the formation
	auto vOffsetOpt = GetFormationOffset(pLocal, m_iPositionInFormation);
	if (!vOffsetOpt)
		return false;
	
	// Find the leader
	Vector vLeaderPos;
	CTFPlayer* pLeaderPlayer = nullptr;
	
	if (!m_vLocalBotPositions.empty())
	{
		// Find the actual leader in-game
		auto pLeader = I::ClientEntityList->GetClientEntity(I::EngineClient->GetPlayerForUserID(m_vLocalBotPositions[0].first))->As<CBaseEntity>();
		if (pLeader && pLeader->IsPlayer())
		{
			pLeaderPlayer = pLeader->As<CTFPlayer>();
			vLeaderPos = pLeaderPlayer->GetAbsOrigin();
		}
	}
	if (!pLeaderPlayer)
		return false;

	if (pLeaderPlayer->m_iTeamNum() != pLocal->m_iTeamNum())
		return false;

	Vector vTargetPos = vLeaderPos + *vOffsetOpt;
	
	// If we're already close enough to our position, don't bother moving
	float flDistToTarget = pLocal->GetAbsOrigin().DistToSqr(vTargetPos);
	if (flDistToTarget <= pow(30.0f, 2))
		return true;
	
	// Only try to move to the position if we're not already pathing to something important
	if (F::NavEngine.current_priority > patrol)
		return false;
	
	static Timer tFailureTimer;
	static int iConsecutiveFailures = 0;
	static Vector vLastTargetPos;
	
	// Check if we're trying to path to the same position but repeatedly failing
	if (vLastTargetPos.DistToSqr(vTargetPos) <= pow(50.0f, 2) && !F::NavEngine.isPathing())
	{
		iConsecutiveFailures++;
		
		// If we've been failing to reach the same target for a while,
		// temporarily increase the acceptable distance to prevent getting stuck
		if (iConsecutiveFailures >= 3)
		{
			iConsecutiveFailures = 0;
			
			// Try a different path approach or temp increase formation distance
			m_flFormationDistance += 50.0f; // Temp increase formation distance
			if (m_flFormationDistance > 300.0f) // Cap the maximum distance
				m_flFormationDistance = 120.0f; // Reset to default if it gets too large
			
			return true; // Skip this attempt and try again with new formation distance
		}
	}
	else if (vLastTargetPos.DistToSqr(vTargetPos) > pow(50.0f, 2))
	{
		iConsecutiveFailures = 0;
	}
	vLastTargetPos = vTargetPos;
	// Try to navigate to our position in formation
	if (F::NavEngine.navTo(vTargetPos, patrol, true, !F::NavEngine.isPathing()))
		return true;
	
	return false;
}

void CNavBot::Draw(CTFPlayer* pLocal)
{
	if (!(Vars::Menu::Indicators.Value & Vars::Menu::IndicatorsEnum::NavBot) || !pLocal->IsAlive())
		return;

	auto bIsReady = F::NavEngine.isReady();
	if (!Vars::Debug::Info.Value && !bIsReady)
		return;

	int x = Vars::Menu::NavBotDisplay.Value.x;
	int y = Vars::Menu::NavBotDisplay.Value.y + 8;
	const auto& fFont = H::Fonts.GetFont(FONT_INDICATORS);
	const int nTall = fFont.m_nTall + H::Draw.Scale(1);

	EAlign align = ALIGN_TOP;
	if (x <= 100 + H::Draw.Scale(50, Scale_Round))
	{
		x -= H::Draw.Scale(42, Scale_Round);
		align = ALIGN_TOPLEFT;
	}
	else if (x >= H::Draw.m_nScreenW - 100 - H::Draw.Scale(50, Scale_Round))
	{
		x += H::Draw.Scale(42, Scale_Round);
		align = ALIGN_TOPRIGHT;
	}

	const auto& cColor = F::NavEngine.isPathing() ? Vars::Menu::Theme::Active.Value : Vars::Menu::Theme::Inactive.Value;
	const auto& cReadyColor = bIsReady ? Vars::Menu::Theme::Active.Value : Vars::Menu::Theme::Inactive.Value;
	auto pLocalArea = F::NavEngine.findClosestNavSquare(pLocal->GetAbsOrigin());
	const int iInSpawn = pLocalArea ? pLocalArea->m_TFattributeFlags & (TF_NAV_SPAWN_ROOM_BLUE | TF_NAV_SPAWN_ROOM_RED | TF_NAV_SPAWN_ROOM_EXIT) : -1;
	std::wstring sJob = L"None";
	switch (F::NavEngine.current_priority)
	{
	case patrol:
		sJob = m_bDefending ? L"Defend" : L"Patrol";
		break;
	case lowprio_health:
		sJob = L"Get health (Low-Prio)";
		break;
	case staynear:
		sJob = std::format(L"Follow enemy ( {} )", m_sFollowTargetName.data());
		break;
	case run_reload:
		sJob = L"Run reload";
		break;
	case run_safe_reload:
		sJob = L"Run safe reload";
		break;
	case snipe_sentry:
		sJob = L"Snipe sentry";
		break;
	case ammo:
		sJob = L"Get ammo";
		break;
	case capture:
		sJob = L"Capture";
		break;
	case prio_melee:
		sJob = L"Melee";
		break;
	case engineer:
		sJob = std::format(L"Engineer ({})", m_sEngineerTask.data());
		break;
	case health:
		sJob = L"Get health";
		break;
	case escape_spawn:
		sJob = L"Escape spawn";
		break;
	case danger:
		sJob = L"Escape danger";
		break;
	default:
		break;
	}

	H::Draw.StringOutlined(fFont, x, y, cColor, Vars::Menu::Theme::Background.Value, align, std::format(L"Job: {} {}", sJob, std::wstring(F::CritHack.m_bForce ? L"(Crithack on)" : L"")).data());
	if (Vars::Debug::Info.Value)
	{
		H::Draw.StringOutlined(fFont, x, y += nTall, cReadyColor, Vars::Menu::Theme::Background.Value, align, std::format("Is ready: {}", std::to_string(bIsReady)).c_str());
		H::Draw.StringOutlined(fFont, x, y += nTall, cReadyColor, Vars::Menu::Theme::Background.Value, align, std::format("In spawn: {}", std::to_string(iInSpawn)).c_str());
		H::Draw.StringOutlined(fFont, x, y += nTall, cReadyColor, Vars::Menu::Theme::Background.Value, align, std::format("Area flags: {}", std::to_string(pLocalArea ? pLocalArea->m_TFattributeFlags : -1)).c_str());
	}
}