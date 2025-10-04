#include "BotUtils.h"
#include "../Simulation/MovementSimulation/MovementSimulation.h"
#include "../Players/PlayerUtils.h"
#include "../Misc/Misc.h"
#include "../Aimbot/AimbotGlobal/AimbotGlobal.h"
#include "../Misc/NamedPipe/NamedPipe.h"
#include "../Ticks/Ticks.h"

bool CBotUtils::HasMedigunTargets(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	if (!Vars::Aimbot::Healing::AutoHeal.Value)
		return false;

	Vec3 vShootPos = F::Ticks.GetShootPos();
	float flRange = pWeapon->GetRange();
	for (auto pEntity : H::Entities.GetGroup(EGroupType::PLAYERS_TEAMMATES))
	{
		if (pEntity->entindex() == pLocal->entindex() || vShootPos.DistTo(pEntity->GetCenter()) > flRange)
			continue;

		if (pEntity->As<CTFPlayer>()->InCond(TF_COND_STEALTHED) ||
			(Vars::Aimbot::Healing::HealPriority.Value == Vars::Aimbot::Healing::HealPriorityEnum::FriendsOnly &&
			!H::Entities.IsFriend(pEntity->entindex()) && !H::Entities.InParty(pEntity->entindex())))
			continue;

		return true;
	}
	return false;
}

ShouldTargetState_t CBotUtils::ShouldTarget(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, int iPlayerIdx)
{
	auto pEntity = I::ClientEntityList->GetClientEntity(iPlayerIdx)->As<CBaseEntity>();
	if (!pEntity || !pEntity->IsPlayer())
		return ShouldTargetState_t::INVALID;

	auto pPlayer = pEntity->As<CTFPlayer>();
	if (!pPlayer->IsAlive() || pPlayer == pLocal)
		return ShouldTargetState_t::INVALID;

#ifdef TEXTMODE
	if (auto pResource = H::Entities.GetResource(); pResource && F::NamedPipe.IsLocalBot(pResource->m_iAccountID(iPlayerIdx)))
		return ShouldTargetState_t::DONT_TARGET;
#endif

	if (F::PlayerUtils.IsIgnored(iPlayerIdx))
		return ShouldTargetState_t::DONT_TARGET;

	if (Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Friends && H::Entities.IsFriend(iPlayerIdx)
		|| Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Party && H::Entities.InParty(iPlayerIdx)
		|| Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Invulnerable && pPlayer->IsInvulnerable() && G::SavedDefIndexes[SLOT_MELEE] != Heavy_t_TheHolidayPunch
		|| Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Invisible && pPlayer->m_flInvisibility() && pPlayer->m_flInvisibility() >= Vars::Aimbot::General::IgnoreInvisible.Value / 100.f
		|| Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::DeadRinger && pPlayer->m_bFeignDeathReady()
		|| Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Taunting && pPlayer->IsTaunting()
		|| Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Disguised && pPlayer->InCond(TF_COND_DISGUISED))
		return ShouldTargetState_t::DONT_TARGET;

	if (pPlayer->m_iTeamNum() == pLocal->m_iTeamNum())
		return ShouldTargetState_t::DONT_TARGET;

	if (Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Vaccinator)
	{
		switch (SDK::GetWeaponType(pWeapon))
		{
		case EWeaponType::HITSCAN:
			if (pPlayer->InCond(TF_COND_MEDIGUN_UBER_BULLET_RESIST) && SDK::AttribHookValue(0, "mod_pierce_resists_absorbs", pWeapon) != 0)
				return ShouldTargetState_t::DONT_TARGET;
			break;
		case EWeaponType::PROJECTILE:
			if (pPlayer->InCond(TF_COND_MEDIGUN_UBER_FIRE_RESIST) && (G::SavedWepIds[SLOT_PRIMARY] == TF_WEAPON_FLAMETHROWER && G::SavedWepIds[SLOT_SECONDARY] == TF_WEAPON_FLAREGUN))
				return ShouldTargetState_t::DONT_TARGET;
			else if (pPlayer->InCond(TF_COND_MEDIGUN_UBER_BULLET_RESIST) && G::SavedWepIds[SLOT_PRIMARY] == TF_WEAPON_COMPOUND_BOW)
				return ShouldTargetState_t::DONT_TARGET;
			else if (pPlayer->InCond(TF_COND_MEDIGUN_UBER_BLAST_RESIST))
				return ShouldTargetState_t::DONT_TARGET;
		}
	}

	return ShouldTargetState_t::TARGET;
}

ShouldTargetState_t CBotUtils::ShouldTargetBuilding(CTFPlayer* pLocal, int iEntIdx)
{
	auto pEntity = I::ClientEntityList->GetClientEntity(iEntIdx)->As<CBaseEntity>();
	if (!pEntity || !pEntity->IsBuilding())
		return ShouldTargetState_t::INVALID;

	auto pBuilding = pEntity->As<CBaseObject>();
	if (!(Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Sentry) && pBuilding->IsSentrygun()
		|| !(Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Dispenser) && pBuilding->IsDispenser()
		|| !(Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Teleporter) && pBuilding->IsTeleporter())
		return ShouldTargetState_t::TARGET;

	if (pLocal->m_iTeamNum() == pBuilding->m_iTeamNum())
		return ShouldTargetState_t::TARGET;

	auto pOwner = pBuilding->m_hBuilder().Get();
	if (pOwner)
	{
		if (F::PlayerUtils.IsIgnored(pOwner->entindex()))
			return ShouldTargetState_t::DONT_TARGET;

		if (Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Friends && H::Entities.IsFriend(pOwner->entindex())
			|| Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Party && H::Entities.InParty(pOwner->entindex()))
			return ShouldTargetState_t::DONT_TARGET;
	}

	return ShouldTargetState_t::TARGET;
}

ClosestEnemy_t CBotUtils::UpdateCloseEnemies(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	m_vCloseEnemies.clear();

	for (auto pEntity : H::Entities.GetGroup(EGroupType::PLAYERS_ENEMIES))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		int iEntIndex = pPlayer->entindex();
		if (!ShouldTarget(pLocal, pWeapon, iEntIndex))
			continue;

		auto vOrigin = F::NavParser.GetDormantOrigin(iEntIndex);
		if (!vOrigin)
			continue;

		m_vCloseEnemies.emplace_back(iEntIndex, pPlayer, pLocal->GetAbsOrigin().DistTo(*vOrigin));
	}
	
	std::sort(m_vCloseEnemies.begin(), m_vCloseEnemies.end(), [](const ClosestEnemy_t& a, const ClosestEnemy_t& b) -> bool
			  {
				  return a.m_flDist < b.m_flDist;
			  });

	if (m_vCloseEnemies.empty())
		return {};

	return m_vCloseEnemies.front();
}


void CBotUtils::UpdateBestSlot(CTFPlayer* pLocal)
{
	if (!Vars::Misc::Movement::BotUtils::WeaponSlot.Value)
	{
		m_iBestSlot = -1;
		return;
	}

	if (Vars::Misc::Movement::BotUtils::WeaponSlot.Value != Vars::Misc::Movement::BotUtils::WeaponSlotEnum::Best)
	{
		m_iBestSlot = Vars::Misc::Movement::BotUtils::WeaponSlot.Value - 2;
		return;
	}

	auto pPrimaryWeapon = pLocal->GetWeaponFromSlot(SLOT_PRIMARY);
	auto pSecondaryWeapon = pLocal->GetWeaponFromSlot(SLOT_SECONDARY);
	if (!pPrimaryWeapon || !pSecondaryWeapon)
		return;

	int iPrimaryResAmmo = SDK::WeaponDoesNotUseAmmo(pPrimaryWeapon) ? -1 : pLocal->GetAmmoCount(pPrimaryWeapon->m_iPrimaryAmmoType());
	int iSecondaryResAmmo = SDK::WeaponDoesNotUseAmmo(pSecondaryWeapon) ? -1 : pLocal->GetAmmoCount(pSecondaryWeapon->m_iPrimaryAmmoType());
	switch (pLocal->m_iClass())
	{
	case TF_CLASS_SCOUT:
	{
		if ((!G::AmmoInSlot[SLOT_PRIMARY] && (!G::AmmoInSlot[SLOT_SECONDARY] || (iSecondaryResAmmo != -1 &&
			iSecondaryResAmmo <= SDK::GetWeaponMaxReserveAmmo(G::SavedWepIds[SLOT_SECONDARY], G::SavedDefIndexes[SLOT_SECONDARY]) / 4))) && m_tClosestEnemy.m_flDist <= 200.f)
			m_iBestSlot = SLOT_MELEE;
		else if (G::AmmoInSlot[SLOT_PRIMARY] && m_tClosestEnemy.m_flDist <= 800.f)
			m_iBestSlot = SLOT_PRIMARY;
		else if (G::AmmoInSlot[SLOT_SECONDARY])
			m_iBestSlot = SLOT_SECONDARY;
		break;
	}
	case TF_CLASS_HEAVY:
	{
		if (!G::AmmoInSlot[SLOT_PRIMARY] &&
			(!G::AmmoInSlot[SLOT_SECONDARY] && iSecondaryResAmmo == 0) ||
			(G::SavedDefIndexes[SLOT_MELEE] == Heavy_t_TheHolidayPunch &&
			(m_tClosestEnemy.m_pPlayer && !m_tClosestEnemy.m_pPlayer->IsTaunting() && m_tClosestEnemy.m_pPlayer->IsInvulnerable()) && m_tClosestEnemy.m_flDist < 400.f))
			m_iBestSlot = SLOT_MELEE;
		else if ((!m_tClosestEnemy.m_pPlayer || m_tClosestEnemy.m_flDist <= 900.f) && G::AmmoInSlot[SLOT_PRIMARY])
			m_iBestSlot = SLOT_PRIMARY;
		else if (G::AmmoInSlot[SLOT_SECONDARY] && G::SavedWepIds[SLOT_SECONDARY] == TF_WEAPON_SHOTGUN_HWG)
			m_iBestSlot = SLOT_SECONDARY;
		break;
	}
	case TF_CLASS_MEDIC:
	{
		if (pSecondaryWeapon->As<CWeaponMedigun>()->m_hHealingTarget() || HasMedigunTargets(pLocal, pSecondaryWeapon))
			m_iBestSlot = SLOT_SECONDARY;
		else if (!G::AmmoInSlot[SLOT_PRIMARY] || (m_tClosestEnemy.m_flDist <= 400.f && m_tClosestEnemy.m_pPlayer))
			m_iBestSlot = SLOT_MELEE;
		else
			m_iBestSlot = SLOT_PRIMARY;
		break;
	}
	case TF_CLASS_SPY:
	{
		if (m_tClosestEnemy.m_flDist <= 250.f && m_tClosestEnemy.m_pPlayer)
			m_iBestSlot = SLOT_MELEE;
		else if (G::AmmoInSlot[SLOT_PRIMARY] || iPrimaryResAmmo)
			m_iBestSlot = SLOT_PRIMARY;
		break;
	}
	case TF_CLASS_SNIPER:
	{
		int iPlayerLowHp = m_tClosestEnemy.m_pPlayer ? (m_tClosestEnemy.m_pPlayer->m_iHealth() < m_tClosestEnemy.m_pPlayer->GetMaxHealth() * 0.35f ? 2 : m_tClosestEnemy.m_pPlayer->m_iHealth() < m_tClosestEnemy.m_pPlayer->GetMaxHealth() * 0.75f) : -1;
		if (!G::AmmoInSlot[SLOT_PRIMARY] && !G::AmmoInSlot[SLOT_SECONDARY] || (m_tClosestEnemy.m_flDist <= 200.f && m_tClosestEnemy.m_pPlayer))
			m_iBestSlot = SLOT_MELEE;
		else if ((G::AmmoInSlot[SLOT_SECONDARY] || iSecondaryResAmmo) && (m_tClosestEnemy.m_flDist <= 300.f && iPlayerLowHp > 1))
			m_iBestSlot = SLOT_SECONDARY;
		// Keep the smg if the target we previosly tried shooting is running away
		else if (m_iCurrentSlot != -1 && m_iCurrentSlot < 2 && G::AmmoInSlot[m_iCurrentSlot] && (m_tClosestEnemy.m_flDist <= 800.f && iPlayerLowHp > 1))
			break;
		else if (G::AmmoInSlot[SLOT_PRIMARY])
			m_iBestSlot = SLOT_PRIMARY;
		break;
	}
	case TF_CLASS_PYRO:
	{
		if (!G::AmmoInSlot[SLOT_PRIMARY] && (!G::AmmoInSlot[SLOT_SECONDARY] && iSecondaryResAmmo != -1 &&
			iSecondaryResAmmo <= SDK::GetWeaponMaxReserveAmmo(G::SavedWepIds[SLOT_SECONDARY], G::SavedDefIndexes[SLOT_SECONDARY]) / 4) &&
			(m_tClosestEnemy.m_pPlayer && m_tClosestEnemy.m_flDist <= 300.f))
			m_iBestSlot = SLOT_MELEE;
		else if (G::AmmoInSlot[SLOT_PRIMARY] && (m_tClosestEnemy.m_flDist <= 550.f || !m_tClosestEnemy.m_pPlayer))
			m_iBestSlot = SLOT_PRIMARY;
		else if (G::AmmoInSlot[SLOT_SECONDARY])
			m_iBestSlot = SLOT_SECONDARY;
		break;
	}
	case TF_CLASS_SOLDIER:
	{
		auto pEnemyWeapon = m_tClosestEnemy.m_pPlayer ? m_tClosestEnemy.m_pPlayer->m_hActiveWeapon().Get()->As<CTFWeaponBase>() : nullptr;
		bool bEnemyCanAirblast = pEnemyWeapon && pEnemyWeapon->GetWeaponID() == TF_WEAPON_FLAMETHROWER && pEnemyWeapon->m_iItemDefinitionIndex() != Pyro_m_ThePhlogistinator;
		bool bEnemyClose = m_tClosestEnemy.m_pPlayer && m_tClosestEnemy.m_flDist <= 250.f;
		if ((m_iCurrentSlot != SLOT_PRIMARY || !G::AmmoInSlot[SLOT_PRIMARY] && iPrimaryResAmmo == 0) && bEnemyClose && (m_tClosestEnemy.m_pPlayer->m_iHealth() < 80 ? !G::AmmoInSlot[SLOT_SECONDARY] : m_tClosestEnemy.m_pPlayer->m_iHealth() >= 150 || G::AmmoInSlot[SLOT_SECONDARY] < 2))
			m_iBestSlot = SLOT_MELEE;
		else if (G::AmmoInSlot[SLOT_SECONDARY] && (bEnemyCanAirblast || (m_tClosestEnemy.m_flDist <= 350.f && m_tClosestEnemy.m_pPlayer && m_tClosestEnemy.m_pPlayer->m_iHealth() <= 125)))
			m_iBestSlot = SLOT_SECONDARY;
		else if (G::AmmoInSlot[SLOT_PRIMARY])
			m_iBestSlot = SLOT_PRIMARY;
		break;
	}
	case TF_CLASS_DEMOMAN:
	{
		if (!G::AmmoInSlot[SLOT_PRIMARY] && !G::AmmoInSlot[SLOT_SECONDARY] && (m_tClosestEnemy.m_pPlayer && m_tClosestEnemy.m_flDist <= 200.f))
			m_iBestSlot = SLOT_MELEE;
		else if (G::AmmoInSlot[SLOT_PRIMARY] && (m_tClosestEnemy.m_flDist <= 800.f))
			m_iBestSlot = SLOT_PRIMARY;
		else if (G::AmmoInSlot[SLOT_SECONDARY] || iSecondaryResAmmo >= SDK::GetWeaponMaxReserveAmmo(G::SavedWepIds[SLOT_SECONDARY], G::SavedDefIndexes[SLOT_SECONDARY]) / 2)
			m_iBestSlot = SLOT_SECONDARY;
		break;
	}
	case TF_CLASS_ENGINEER:
	{
		if (!G::AmmoInSlot[SLOT_PRIMARY] && !G::AmmoInSlot[SLOT_SECONDARY] && (m_tClosestEnemy.m_pPlayer && m_tClosestEnemy.m_flDist <= 200.f))
			m_iBestSlot = SLOT_MELEE;
		else if ((G::AmmoInSlot[SLOT_PRIMARY] || iPrimaryResAmmo) && (m_tClosestEnemy.m_pPlayer && m_tClosestEnemy.m_flDist <= 500.f))
			m_iBestSlot = SLOT_PRIMARY;
		else if (G::AmmoInSlot[SLOT_SECONDARY] || iSecondaryResAmmo)
			m_iBestSlot = SLOT_SECONDARY;
		break;
	}
	default:
		break;
	}
}

void CBotUtils::SetSlot(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, int iSlot)
{
	if (iSlot > -1)
	{
		auto sCommand = "slot" + std::to_string(iSlot+1);
		if (m_iCurrentSlot != iSlot)
			I::EngineClient->ClientCmd_Unrestricted(sCommand.c_str());
	}
}

void CBotUtils::DoSlowAim(Vec3& vWishAngles, float flSpeed, Vec3 vPreviousAngles)
{
	// Yaw
	if (vPreviousAngles.y != vWishAngles.y)
	{
		Vec3 vSlowDelta = vWishAngles - vPreviousAngles;

		while (vSlowDelta.y > 180)
			vSlowDelta.y -= 360;
		while (vSlowDelta.y < -180)
			vSlowDelta.y += 360;

		vSlowDelta /= flSpeed;
		vWishAngles = vPreviousAngles + vSlowDelta;

		// Clamp as we changed angles
		Math::ClampAngles(vWishAngles);
	}
}

void CBotUtils::LookAtPath(CUserCmd* pCmd, Vec2 vDest, Vec3 vLocalEyePos, bool bSilent)
{
	Vec3 vWishAng{ vDest.x, vDest.y, vLocalEyePos.z };
	vWishAng = Math::CalcAngle(vLocalEyePos, vWishAng);

	DoSlowAim(vWishAng, 25.f, m_vLastAngles);
	if (bSilent)
		pCmd->viewangles = vWishAng;
	else
		I::EngineClient->SetViewAngles(vWishAng);
	m_vLastAngles = vWishAng;
}

void CBotUtils::LookAtPath(CUserCmd* pCmd, Vec3 vWishAngles, Vec3 vLocalEyePos, bool bSilent, bool bSmooth)
{
	if (bSmooth)
		DoSlowAim(vWishAngles, 25.f, m_vLastAngles);

	if (bSilent)
		pCmd->viewangles = vWishAngles;
	else
		I::EngineClient->SetViewAngles(vWishAngles);
	m_vLastAngles = vWishAngles;
}

void CBotUtils::AutoScope(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	static bool bKeep = false;
	static bool bShouldClearCache = false;
	static Timer tScopeTimer{};
	bool bIsClassic = pWeapon->GetWeaponID() == TF_WEAPON_SNIPERRIFLE_CLASSIC;
	if (!Vars::Misc::Movement::BotUtils::AutoScope.Value || pWeapon->GetWeaponID() != TF_WEAPON_SNIPERRIFLE && !bIsClassic && pWeapon->GetWeaponID() != TF_WEAPON_SNIPERRIFLE_DECAP)
	{
		bKeep = false;
		m_mAutoScopeCache.clear();
		return;
	}

	if (!Vars::Misc::Movement::BotUtils::AutoScopeUseCachedResults.Value)
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
			if (tScopeTimer.Check(Vars::Misc::Movement::BotUtils::AutoScopeCancelTime.Value)) // cancel classic charge
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
				if (tScopeTimer.Check(Vars::Misc::Movement::BotUtils::AutoScopeCancelTime.Value))
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

	bool bSimple = Vars::Misc::Movement::BotUtils::AutoScope.Value == Vars::Misc::Movement::BotUtils::AutoScopeEnum::Simple;

	int iMaxTicks = TIME_TO_TICKS(0.5f);
	PlayerStorage tStorage;
	for (auto [pEnemy, _] : vEnemiesSorted)
	{
		int iEntIndex = Vars::Misc::Movement::BotUtils::AutoScopeUseCachedResults.Value ? pEnemy->entindex() : -1;
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

void CBotUtils::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if ((!Vars::Misc::Movement::NavBot::Enabled.Value && !(Vars::Misc::Movement::FollowBot::Enabled.Value && Vars::Misc::Movement::FollowBot::Targets.Value)) ||
		!pLocal->IsAlive() || !pWeapon)
	{
		Reset();
		return;
	}

	m_tClosestEnemy = UpdateCloseEnemies(pLocal, pWeapon);
	m_iCurrentSlot = pWeapon->GetSlot();
	UpdateBestSlot(pLocal);

	if (!F::NavEngine.IsNavMeshLoaded() || (pCmd->buttons & (IN_FORWARD | IN_BACK | IN_MOVERIGHT | IN_MOVELEFT) && !F::Misc.m_bAntiAFK))
	{
		m_mAutoScopeCache.clear();
		return;
	}

	AutoScope(pLocal, pWeapon, pCmd);

	// Spin up the minigun if there are enemies nearby or if we had an active aimbot target 
	if (pWeapon->GetWeaponID() == TF_WEAPON_MINIGUN)
	{
		static Timer tSpinupTimer{};
		if (m_tClosestEnemy.m_pPlayer && m_tClosestEnemy.m_pPlayer->IsAlive() && !m_tClosestEnemy.m_pPlayer->IsInvulnerable() && pWeapon->HasAmmo())
		{
			if (G::AimTarget.m_iEntIndex && G::AimTarget.m_iDuration || m_tClosestEnemy.m_flDist <= 800.f)
				tSpinupTimer.Update();
			if (!tSpinupTimer.Check(3.f)) // 3 seconds until unrev
				pCmd->buttons |= IN_ATTACK2;
		}
	}
}

void CBotUtils::Reset()
{
	m_mAutoScopeCache.clear();
	m_vCloseEnemies.clear();
	m_tClosestEnemy = {};
	m_iBestSlot = -1;
	m_iCurrentSlot = -1;
}