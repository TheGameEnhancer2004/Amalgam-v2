#include "BotUtils.h"
#include "NavEngine/NavEngine.h"
#include "../Simulation/MovementSimulation/MovementSimulation.h"
#include "../Players/PlayerUtils.h"
#include "../Misc/Misc.h"
#include "../Aimbot/AimbotGlobal/AimbotGlobal.h"
#include "../Aimbot/AutoRocketJump/AutoRocketJump.h"
#include "../Misc/NamedPipe/NamedPipe.h"
#include "../Ticks/Ticks.h"

static bool SmoothAimHasPriority()
{
	const auto iAimType = Vars::Aimbot::General::AimType.Value;
	if (iAimType != Vars::Aimbot::General::AimTypeEnum::Smooth &&
		iAimType != Vars::Aimbot::General::AimTypeEnum::Assistive)
		return false;

	return G::AimbotSteering;
}

bool CBotUtils::HasMedigunTargets(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	if (!Vars::Aimbot::Healing::AutoHeal.Value)
		return false;

	Vec3 vShootPos = F::Ticks.GetShootPos();
	float flRange = pWeapon->GetRange();
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerTeam))
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

bool CBotUtils::ShouldAssist(CTFPlayer* pLocal, int iEntIdx)
{
	auto pEntity = I::ClientEntityList->GetClientEntity(iEntIdx);
	if (!pEntity || pEntity->As<CBaseEntity>()->m_iTeamNum() != pLocal->m_iTeamNum())
		return false;

	if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::HelpFriendlyCaptureObjectives))
		return true;

	if (F::PlayerUtils.IsIgnored(iEntIdx)
		|| H::Entities.InParty(iEntIdx)
		|| H::Entities.IsFriend(iEntIdx))
		return true;

	return false;
}

ShouldTargetEnum::ShouldTargetEnum CBotUtils::ShouldTarget(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, int iEntIdx)
{
	auto pEntity = I::ClientEntityList->GetClientEntity(iEntIdx)->As<CBaseEntity>();
	if (!pEntity || !pEntity->IsPlayer())
		return ShouldTargetEnum::Invalid;

	auto pPlayer = pEntity->As<CTFPlayer>();
	if (!pPlayer->IsAlive() || pPlayer == pLocal)
		return ShouldTargetEnum::Invalid;

#ifdef TEXTMODE
	if (auto pResource = H::Entities.GetResource(); pResource && F::NamedPipe.IsLocalBot(pResource->m_iAccountID(iEntIdx)) && !(Vars::Aimbot::General::BypassIgnore.Value & Vars::Aimbot::General::BypassIgnoreEnum::LocalBots))
		return ShouldTargetEnum::DontTarget;
#endif

	if (F::PlayerUtils.IsIgnored(iEntIdx) && !(Vars::Aimbot::General::BypassIgnore.Value & Vars::Aimbot::General::BypassIgnoreEnum::Ignored))
		return ShouldTargetEnum::DontTarget;

	if (Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Friends && H::Entities.IsFriend(iEntIdx) && !(Vars::Aimbot::General::BypassIgnore.Value & Vars::Aimbot::General::BypassIgnoreEnum::Friends)
		|| Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Party && H::Entities.InParty(iEntIdx) && !(Vars::Aimbot::General::BypassIgnore.Value & Vars::Aimbot::General::BypassIgnoreEnum::Friends)
		|| Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Invulnerable && pPlayer->IsInvulnerable() && G::SavedDefIndexes[SLOT_MELEE] != Heavy_t_TheHolidayPunch
		|| Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Invisible && pPlayer->m_flInvisibility() && pPlayer->m_flInvisibility() >= Vars::Aimbot::General::IgnoreInvisible.Value / 100.f
		|| Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::DeadRinger && pPlayer->m_bFeignDeathReady()
		|| Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Taunting && pPlayer->IsTaunting()
		|| Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Disguised && pPlayer->InCond(TF_COND_DISGUISED))
		return ShouldTargetEnum::DontTarget;

	if (pPlayer->m_iTeamNum() == pLocal->m_iTeamNum())
		return ShouldTargetEnum::DontTarget;

	if (Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Vaccinator)
	{
		switch (SDK::GetWeaponType(pWeapon))
		{
		case EWeaponType::HITSCAN:
			if (pPlayer->InCond(TF_COND_MEDIGUN_UBER_BULLET_RESIST) && SDK::AttribHookValue(0, "mod_pierce_resists_absorbs", pWeapon) != 0)
				return ShouldTargetEnum::DontTarget;
			break;
		case EWeaponType::PROJECTILE:
			if (pPlayer->InCond(TF_COND_MEDIGUN_UBER_FIRE_RESIST) && (G::SavedWepIds[SLOT_PRIMARY] == TF_WEAPON_FLAMETHROWER && G::SavedWepIds[SLOT_SECONDARY] == TF_WEAPON_FLAREGUN))
				return ShouldTargetEnum::DontTarget;
			else if (pPlayer->InCond(TF_COND_MEDIGUN_UBER_BULLET_RESIST) && G::SavedWepIds[SLOT_PRIMARY] == TF_WEAPON_COMPOUND_BOW)
				return ShouldTargetEnum::DontTarget;
			else if (pPlayer->InCond(TF_COND_MEDIGUN_UBER_BLAST_RESIST))
				return ShouldTargetEnum::DontTarget;
		}
	}

	return ShouldTargetEnum::Target;
}

ShouldTargetEnum::ShouldTargetEnum CBotUtils::ShouldTargetBuilding(CTFPlayer* pLocal, int iEntIdx)
{
	if (iEntIdx <= 0)
		return ShouldTargetEnum::DontTarget;

	auto pEntity = I::ClientEntityList->GetClientEntity(iEntIdx)->As<CBaseEntity>();
	if (!pEntity)
		return ShouldTargetEnum::Invalid;

	if (!pEntity->IsBuilding())
		return ShouldTargetEnum::DontTarget;

	auto pBuilding = pEntity->As<CBaseObject>();
	if (!(Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Sentry) && pBuilding->IsSentrygun()
		|| !(Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Dispenser) && pBuilding->IsDispenser()
		|| !(Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Teleporter) && pBuilding->IsTeleporter())
		return ShouldTargetEnum::Target;

	if (pLocal->m_iTeamNum() == pBuilding->m_iTeamNum())
		return ShouldTargetEnum::Target;

	auto pOwner = pBuilding->m_hBuilder().Get();
	if (pOwner)
	{
		if (F::PlayerUtils.IsIgnored(pOwner->entindex()) && !(Vars::Aimbot::General::BypassIgnore.Value & Vars::Aimbot::General::BypassIgnoreEnum::Ignored))
			return ShouldTargetEnum::DontTarget;

		if (Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Friends && H::Entities.IsFriend(pOwner->entindex()) && !(Vars::Aimbot::General::BypassIgnore.Value & Vars::Aimbot::General::BypassIgnoreEnum::Friends)
			|| Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Party && H::Entities.InParty(pOwner->entindex()) && !(Vars::Aimbot::General::BypassIgnore.Value & Vars::Aimbot::General::BypassIgnoreEnum::Friends))
			return ShouldTargetEnum::DontTarget;
	}

	return ShouldTargetEnum::Target;
}

bool CBotUtils::GetDormantOrigin(int iIndex, Vector& vOut)
{
	if (iIndex <= 0)
		return false;

	auto pEntity = I::ClientEntityList->GetClientEntity(iIndex)->As<CBaseEntity>();
	if (!pEntity ||
		(pEntity->IsPlayer() ? !pEntity->As<CBasePlayer>()->IsAlive() :
		pEntity->IsBuilding() ? !pEntity->As<CBaseObject>()->m_iHealth() : true))
		return false;

	if (!pEntity->IsDormant() || H::Entities.GetDormancy(iIndex))
	{
		vOut = pEntity->GetAbsOrigin();
		return true;
	}

	return false;
}

ClosestEnemy_t CBotUtils::UpdateCloseEnemies(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	m_vCloseEnemies.clear();

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		int iEntIndex = pPlayer->entindex();
		if (!ShouldTarget(pLocal, pWeapon, iEntIndex))
			continue;

		Vector vOrigin;
		if (!GetDormantOrigin(iEntIndex, vOrigin))
			continue;

		m_vCloseEnemies.emplace_back(iEntIndex, pPlayer, pLocal->GetAbsOrigin().DistTo(vOrigin));
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

	switch (pLocal->m_iClass())
	{
	case TF_CLASS_SCOUT:
	{
		if ((!G::AmmoInSlot[SLOT_PRIMARY].m_iClip &&
			(!G::AmmoInSlot[SLOT_SECONDARY].m_bUsesAmmo || !G::AmmoInSlot[SLOT_SECONDARY].m_iClip || G::AmmoInSlot[SLOT_SECONDARY].m_iReserve <= G::AmmoInSlot[SLOT_SECONDARY].m_iMaxReserve / 4)) &&
			m_tClosestEnemy.m_flDist <= 200.f)
			m_iBestSlot = SLOT_MELEE;
		else if (G::AmmoInSlot[SLOT_PRIMARY].m_iClip && m_tClosestEnemy.m_flDist <= 800.f)
			m_iBestSlot = SLOT_PRIMARY;
		else if (G::AmmoInSlot[SLOT_SECONDARY].m_bUsesAmmo && G::AmmoInSlot[SLOT_SECONDARY].m_iClip)
			m_iBestSlot = SLOT_SECONDARY;
		break;
	}
	case TF_CLASS_HEAVY:
	{
		if (!G::AmmoInSlot[SLOT_PRIMARY].m_iClip && (!G::AmmoInSlot[SLOT_SECONDARY].m_iClip && G::AmmoInSlot[SLOT_SECONDARY].m_iReserve == 0) ||
			(G::SavedDefIndexes[SLOT_MELEE] == Heavy_t_TheHolidayPunch && 
			(m_tClosestEnemy.m_pPlayer && !m_tClosestEnemy.m_pPlayer->IsTaunting() && m_tClosestEnemy.m_pPlayer->IsInvulnerable()) && m_tClosestEnemy.m_flDist < 400.f))
			m_iBestSlot = SLOT_MELEE;
		else if ((!m_tClosestEnemy.m_pPlayer || m_tClosestEnemy.m_flDist <= 900.f) && G::AmmoInSlot[SLOT_PRIMARY].m_iClip)
			m_iBestSlot = SLOT_PRIMARY;
		else if (G::AmmoInSlot[SLOT_SECONDARY].m_bUsesAmmo && G::AmmoInSlot[SLOT_SECONDARY].m_iClip)
			m_iBestSlot = SLOT_SECONDARY;
		break;
	}
	case TF_CLASS_MEDIC:
	{
		auto pSecondaryWeapon = pLocal->GetWeaponFromSlot(SLOT_SECONDARY);
		if (!pSecondaryWeapon)
			return;

		if (pSecondaryWeapon->As<CWeaponMedigun>()->m_hHealingTarget() || HasMedigunTargets(pLocal, pSecondaryWeapon))
			m_iBestSlot = SLOT_SECONDARY;
		else if (!G::AmmoInSlot[SLOT_PRIMARY].m_iClip || (m_tClosestEnemy.m_flDist <= 400.f && m_tClosestEnemy.m_pPlayer))
			m_iBestSlot = SLOT_MELEE;
		else
			m_iBestSlot = SLOT_PRIMARY;
		break;
	}
	case TF_CLASS_SPY:
	{
		if (m_tClosestEnemy.m_flDist <= 250.f && m_tClosestEnemy.m_pPlayer)
			m_iBestSlot = SLOT_MELEE;
		else if (G::AmmoInSlot[SLOT_PRIMARY].m_iClip || G::AmmoInSlot[SLOT_PRIMARY].m_iReserve)
			m_iBestSlot = SLOT_PRIMARY;
		break;
	}
	case TF_CLASS_SNIPER:
	{
		int iPlayerLowHp = m_tClosestEnemy.m_pPlayer ? (m_tClosestEnemy.m_pPlayer->m_iHealth() < m_tClosestEnemy.m_pPlayer->GetMaxHealth() * 0.35f ? 2 : m_tClosestEnemy.m_pPlayer->m_iHealth() < m_tClosestEnemy.m_pPlayer->GetMaxHealth() * 0.75f) : -1;
		if (!G::AmmoInSlot[SLOT_PRIMARY].m_iClip && !G::AmmoInSlot[SLOT_SECONDARY].m_iClip || (m_tClosestEnemy.m_flDist <= 200.f && m_tClosestEnemy.m_pPlayer))
			m_iBestSlot = SLOT_MELEE;
		else if (G::AmmoInSlot[SLOT_SECONDARY].m_bUsesAmmo && (G::AmmoInSlot[SLOT_SECONDARY].m_iClip || G::AmmoInSlot[SLOT_SECONDARY].m_iReserve) && (m_tClosestEnemy.m_flDist <= 300.f && iPlayerLowHp > 1))
			m_iBestSlot = SLOT_SECONDARY;
		// Keep currently selected weapon if the target we previosly tried shooting at is running away
		else if (m_iCurrentSlot < 2 && m_iCurrentSlot != -1 && G::AmmoInSlot[m_iCurrentSlot].m_bUsesAmmo && G::AmmoInSlot[m_iCurrentSlot].m_iClip && (m_tClosestEnemy.m_flDist <= 800.f && iPlayerLowHp > 1))
			break;
		else if (G::AmmoInSlot[SLOT_PRIMARY].m_iClip)
			m_iBestSlot = SLOT_PRIMARY;
		break;
	}
	case TF_CLASS_PYRO:
	{
		if (!G::AmmoInSlot[SLOT_PRIMARY].m_iClip && (!G::AmmoInSlot[SLOT_SECONDARY].m_iClip && G::AmmoInSlot[SLOT_SECONDARY].m_bUsesAmmo &&
			G::AmmoInSlot[SLOT_SECONDARY].m_iReserve <= G::AmmoInSlot[SLOT_SECONDARY].m_iMaxReserve / 4) &&
			(m_tClosestEnemy.m_pPlayer && m_tClosestEnemy.m_flDist <= 300.f))
			m_iBestSlot = SLOT_MELEE;
		else if (G::AmmoInSlot[SLOT_PRIMARY].m_iClip && (m_tClosestEnemy.m_flDist <= 550.f || !m_tClosestEnemy.m_pPlayer))
			m_iBestSlot = SLOT_PRIMARY;
		else if (G::AmmoInSlot[SLOT_SECONDARY].m_iClip)
			m_iBestSlot = SLOT_SECONDARY;
		break;
	}
	case TF_CLASS_SOLDIER:
	{
		auto pEnemyWeapon = m_tClosestEnemy.m_pPlayer ? m_tClosestEnemy.m_pPlayer->m_hActiveWeapon().Get()->As<CTFWeaponBase>() : nullptr;
		bool bEnemyCanAirblast = pEnemyWeapon && pEnemyWeapon->GetWeaponID() == TF_WEAPON_FLAMETHROWER && pEnemyWeapon->m_iItemDefinitionIndex() != Pyro_m_ThePhlogistinator;
		bool bEnemyClose = m_tClosestEnemy.m_pPlayer && m_tClosestEnemy.m_flDist <= 250.f;
		if ((m_iCurrentSlot != SLOT_PRIMARY || G::AmmoInSlot[SLOT_PRIMARY].m_bUsesAmmo && !G::AmmoInSlot[SLOT_PRIMARY].m_iClip && !G::AmmoInSlot[SLOT_PRIMARY].m_iReserve) && bEnemyClose && (m_tClosestEnemy.m_pPlayer->m_iHealth() < 80 ? !G::AmmoInSlot[SLOT_SECONDARY].m_iClip : m_tClosestEnemy.m_pPlayer->m_iHealth() >= 150 || G::AmmoInSlot[SLOT_SECONDARY].m_iClip < 2))
			m_iBestSlot = SLOT_MELEE;
		else if ((!G::AmmoInSlot[SLOT_PRIMARY].m_bUsesAmmo || G::AmmoInSlot[SLOT_SECONDARY].m_iClip) && (bEnemyCanAirblast || (m_tClosestEnemy.m_flDist <= 350.f && m_tClosestEnemy.m_pPlayer && m_tClosestEnemy.m_pPlayer->m_iHealth() <= 125)))
			m_iBestSlot = SLOT_SECONDARY;
		else if (!G::AmmoInSlot[SLOT_PRIMARY].m_bUsesAmmo || G::AmmoInSlot[SLOT_PRIMARY].m_iClip)
			m_iBestSlot = SLOT_PRIMARY;
		break;
	}
	case TF_CLASS_DEMOMAN:
	{
		if (!G::AmmoInSlot[SLOT_PRIMARY].m_iClip && (!G::AmmoInSlot[SLOT_SECONDARY].m_bUsesAmmo || !G::AmmoInSlot[SLOT_SECONDARY].m_iClip) && (m_tClosestEnemy.m_pPlayer && m_tClosestEnemy.m_flDist <= 200.f))
			m_iBestSlot = SLOT_MELEE;
		else if (G::AmmoInSlot[SLOT_PRIMARY].m_iClip && (m_tClosestEnemy.m_flDist <= 800.f))
			m_iBestSlot = SLOT_PRIMARY;
		else if (G::AmmoInSlot[SLOT_SECONDARY].m_bUsesAmmo && (G::AmmoInSlot[SLOT_SECONDARY].m_iClip || G::AmmoInSlot[SLOT_SECONDARY].m_iReserve >= G::AmmoInSlot[SLOT_SECONDARY].m_iMaxReserve / 2))
			m_iBestSlot = SLOT_SECONDARY;
		break;
	}
	case TF_CLASS_ENGINEER:
	{
		if (G::AmmoInSlot[SLOT_PRIMARY].m_bUsesAmmo && !G::AmmoInSlot[SLOT_PRIMARY].m_iClip && !G::AmmoInSlot[SLOT_SECONDARY].m_iClip && (m_tClosestEnemy.m_pPlayer && m_tClosestEnemy.m_flDist <= 200.f))
			m_iBestSlot = SLOT_MELEE;
		else if ((!G::AmmoInSlot[SLOT_PRIMARY].m_bUsesAmmo || G::AmmoInSlot[SLOT_PRIMARY].m_iClip || G::AmmoInSlot[SLOT_PRIMARY].m_iReserve) && (m_tClosestEnemy.m_pPlayer && m_tClosestEnemy.m_flDist <= 500.f))
			m_iBestSlot = SLOT_PRIMARY;
		else if (!G::AmmoInSlot[SLOT_PRIMARY].m_bUsesAmmo || G::AmmoInSlot[SLOT_SECONDARY].m_iClip || G::AmmoInSlot[SLOT_SECONDARY].m_iReserve)
			m_iBestSlot = SLOT_SECONDARY;
		break;
	}
	default:
		break;
	}
}

void CBotUtils::SetSlot(CTFPlayer* pLocal, int iSlot)
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
	if (SmoothAimHasPriority())
	{
		m_vLastAngles = I::EngineClient->GetViewAngles();
		return;
	}

	Vec3 vWishAng{ vDest.x, vDest.y, vLocalEyePos.z };
	vWishAng = Math::CalcAngle(vLocalEyePos, vWishAng);

	DoSlowAim(vWishAng, static_cast<float>(Vars::Misc::Movement::BotUtils::LookAtPathSpeed.Value), m_vLastAngles);
	if (bSilent)
		pCmd->viewangles = vWishAng;
	else
		I::EngineClient->SetViewAngles(vWishAng);
	m_vLastAngles = vWishAng;
}

void CBotUtils::LookAtPath(CUserCmd* pCmd, Vec3 vWishAngles, Vec3 vLocalEyePos, bool bSilent, bool bSmooth)
{
	if (SmoothAimHasPriority())
	{
		m_vLastAngles = I::EngineClient->GetViewAngles();
		return;
	}

	if (bSmooth)
		DoSlowAim(vWishAngles, 25.f, m_vLastAngles);

	if (bSilent)
		pCmd->viewangles = vWishAngles;
	else
		I::EngineClient->SetViewAngles(vWishAngles);
	m_vLastAngles = vWishAngles;
}

void CBotUtils::LookLegit(CTFPlayer* pLocal, CUserCmd* pCmd, const Vec3& vDest, bool bSilent)
{
	if (!pLocal)
		return;

	if (SmoothAimHasPriority())
	{
		Vec3 vCurrent = I::EngineClient->GetViewAngles();
		m_vLastAngles = vCurrent;
		auto& tState = m_tLLAP;
		if (tState.m_bInitialized)
			tState.m_vAnchor = vCurrent;
		return;
	}

	Vec3 vEye = pLocal->GetEyePosition();
	Vec3 vLook = vDest;
	bool bEnemyLock = false;

	// 1. look at visible enemies
	static int iLastTarget = -1;
	static float flLastSeen = 0.f;
	static Vec3 vLastPos = {};
	
	CBaseEntity* pBestEnemy = nullptr;
	float flBestDist = FLT_MAX;
	auto pWeapon = pLocal->m_hActiveWeapon().Get()->As<CTFWeaponBase>();

	if (G::AimTarget.m_iEntIndex)
	{
		if (auto pTarget = I::ClientEntityList->GetClientEntity(G::AimTarget.m_iEntIndex)->As<CBaseEntity>())
		{
			pBestEnemy = pTarget;
			flBestDist = -1.f;
		}
	}

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
	{
		auto pEnemy = pEntity->As<CTFPlayer>();
		if (!pEnemy || !pEnemy->IsAlive() || pEnemy->IsDormant())
			continue;

		if (ShouldTarget(pLocal, pWeapon, pEnemy->entindex()) == ShouldTargetEnum::DontTarget)
			continue;

		Vec3 vEnemyEye = pEnemy->GetEyePosition();
		if (SDK::VisPos(pLocal, pEnemy, vEye, vEnemyEye))
		{
			float flDist = vEye.DistTo(vEnemyEye);
			if (flDist < flBestDist)
			{
				flBestDist = flDist;
				pBestEnemy = pEnemy;
			}
		}
	}

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::BuildingEnemy))
	{
		auto pBuilding = pEntity->As<CBaseObject>();
		if (!pBuilding || pBuilding->m_iHealth() <= 0 || pBuilding->IsDormant())
			continue;

		if (ShouldTargetBuilding(pLocal, pBuilding->entindex()) == ShouldTargetEnum::DontTarget)
			continue;

		Vec3 vBuildingCenter = pBuilding->GetCenter();
		if (SDK::VisPos(pLocal, pBuilding, vEye, vBuildingCenter))
		{
			float flDist = vEye.DistTo(vBuildingCenter);
			if (flDist < flBestDist)
			{
				flBestDist = flDist;
				pBestEnemy = pBuilding;
			}
		}
	}

	if (pBestEnemy)
	{
		if (pBestEnemy->IsPlayer())
		{
			vLook = pBestEnemy->As<CTFPlayer>()->GetEyePosition();
			// look slightly below head (chest/neck)
			vLook.z -= 10.f;
		}
		else
		{
			vLook = pBestEnemy->GetCenter();
		}

		iLastTarget = pBestEnemy->entindex();
		flLastSeen = I::GlobalVars->curtime;
		vLastPos = vLook;
		bEnemyLock = true;
	}
	else if ((I::GlobalVars->curtime - flLastSeen) < 1.5f && !vLastPos.IsZero())
	{
		// look at last known position for a bit
		vLook = vLastPos;
		bEnemyLock = true;
	}
	else
	{
		// 2. movement direction
		// look ahead based on velocity
		const Vec3 vVelocity = pLocal->m_vecVelocity();
		const float flSpeed = vVelocity.Length2D();
		if (flSpeed > 25.f)
		{
			Vec3 vForward = vVelocity;
			vForward.Normalize();
			vLook = vEye + (vForward * 500.f);
		}
		else if (vLook.IsZero())
		{
			Vec3 vForward;
			Math::AngleVectors(I::EngineClient->GetViewAngles(), &vForward, nullptr, nullptr);
			vLook = vEye + vForward * 64.f;
		}
	}

	Vec3 vFocus;
	if (bEnemyLock)
	{
		// If looking at an enemy/spot, look directly there
		vFocus = vLook;
	}
	else
	{
		// If pathing, use"terrain" following logic
		const float flHeightDelta = std::clamp(vLook.z - vEye.z, -72.f, 96.f);
		const float flPitchFactor = flHeightDelta >= 0.f ? 0.55f : 0.22f;
		vFocus = { vLook.x, vLook.y, vEye.z + flHeightDelta * flPitchFactor + 6.f };
	}

	Vec3 vDesired = Math::CalcAngle(vEye, vFocus);
	Math::ClampAngles(vDesired);

	auto& tState = m_tLLAP;
	const float flTargetDelta = tState.m_vLastTarget.IsZero() ? FLT_MAX : tState.m_vLastTarget.DistToSqr(vFocus);
	if (!tState.m_bInitialized || !std::isfinite(flTargetDelta) || flTargetDelta > 4096.f)
	{
		tState.m_bInitialized = true;
		tState.m_vAnchor = vDesired;
		tState.m_vOffset = {};
		tState.m_vOffsetGoal = {};
		tState.m_vLastTarget = vFocus;
		tState.m_vGlanceCurrent = {};
		tState.m_vGlanceGoal = {};
		tState.m_flNextOffset = SDK::RandomFloat(0.6f, 1.8f);
		tState.m_flPhase = SDK::RandomFloat(0.f, 6.2831853f);
		tState.m_flNextGlance = SDK::RandomFloat(1.4f, 3.0f);
		tState.m_flGlanceDuration = SDK::RandomFloat(0.3f, 0.55f);
		tState.m_bGlancing = false;
		tState.m_tOffsetTimer.Update();
		tState.m_tGlanceTimer.Update();
		tState.m_tGlanceCooldown.Update();
	}
	else
		tState.m_vLastTarget = vFocus;

	float flAnchorDelta = Math::CalcFov(tState.m_vAnchor, vDesired);
	if (!std::isfinite(flAnchorDelta) || flAnchorDelta > 120.f)
		tState.m_vAnchor = vDesired;
	else
	{
		float flAnchorBlend = std::clamp(flAnchorDelta / 90.f, 0.05f, 0.3f);
		tState.m_vAnchor = tState.m_vAnchor.LerpAngle(vDesired, flAnchorBlend);
	}

	const float flVelocity2D = pLocal->m_vecVelocity().Length2D();
	if (tState.m_tOffsetTimer.Run(tState.m_flNextOffset))
	{
		float flYawScale = std::clamp(flVelocity2D / 220.f, 0.3f, 0.95f);
		float flPitchScale = std::clamp(flVelocity2D / 320.f, 0.18f, 0.75f);
		tState.m_vOffsetGoal.y = SDK::RandomFloat(-28.f, 28.f) * flYawScale;
		tState.m_vOffsetGoal.x = SDK::RandomFloat(-3.f, 4.f) * flPitchScale;
		tState.m_flNextOffset = SDK::RandomFloat(0.65f, 1.95f);
	}

	tState.m_vOffset = tState.m_vOffset.LerpAngle(tState.m_vOffsetGoal, 0.1f);
	if (tState.m_bGlancing)
	{
		if (tState.m_tGlanceTimer.Run(tState.m_flGlanceDuration))
		{
			tState.m_bGlancing = false;
			tState.m_vGlanceGoal = {};
			tState.m_flNextGlance = SDK::RandomFloat(1.6f, 3.4f);
			tState.m_tGlanceCooldown.Update();
		}
	}
	else if (tState.m_tGlanceCooldown.Run(tState.m_flNextGlance))
	{
		tState.m_bGlancing = true;
		tState.m_flGlanceDuration = SDK::RandomFloat(0.28f, 0.52f);
		float flYawGlance = SDK::RandomFloat(16.f, 38.f) * (SDK::RandomInt(0, 1) == 0 ? -1.f : 1.f);
		tState.m_vGlanceGoal = { SDK::RandomFloat(-3.5f, 4.5f), flYawGlance, 0.f };
		tState.m_tGlanceTimer.Update();
	}

	tState.m_vGlanceCurrent = tState.m_vGlanceCurrent.LerpAngle(tState.m_vGlanceGoal, tState.m_bGlancing ? 0.2f : 0.12f);

	float flPhaseSpeed = std::clamp(flVelocity2D / 240.f, 0.25f, 1.0f);
	tState.m_flPhase += I::GlobalVars->interval_per_tick * (0.9f + flPhaseSpeed);
	if (tState.m_flPhase > 8192.f)
		tState.m_flPhase = std::fmod(tState.m_flPhase, 8192.f);

	float flMicroScale = std::clamp(flVelocity2D / 320.f, 0.12f, 0.4f);
	Vec3 vMicro = {
		std::sin(tState.m_flPhase * 0.92f) * 0.6f * flMicroScale,
		std::sin(tState.m_flPhase * 0.55f + 1.4f) * 0.8f * flMicroScale,
		0.f
	};

	Vec3 vGoal = tState.m_vAnchor + tState.m_vOffset + tState.m_vGlanceCurrent + vMicro;
	Math::ClampAngles(vGoal);
	vGoal.x = std::clamp(vGoal.x, -8.f, 22.f);

	float flSpeedVal = std::max(1.f, static_cast<float>(Vars::Misc::Movement::BotUtils::LookAtPathSpeed.Value));
	Vec3 vWish = vGoal;
	DoSlowAim(vWish, flSpeedVal, m_vLastAngles);

	if (Vars::Misc::Movement::BotUtils::LookAtPathDebug.Value)
	{
		G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(vLook - Vec3(10, 0, 0), vLook + Vec3(10, 0, 0)), I::GlobalVars->curtime + 0.1f, Color_t{ 255, 0, 0, 255 }, false);
		G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(vLook - Vec3(0, 10, 0), vLook + Vec3(0, 10, 0)), I::GlobalVars->curtime + 0.1f, Color_t{ 0, 255, 0, 255 }, false);
		G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(vLook - Vec3(0, 0, 10), vLook + Vec3(0, 0, 10)), I::GlobalVars->curtime + 0.1f, Color_t{ 0, 0, 255, 255 }, false);
	}

	if (bSilent)
		pCmd->viewangles = vWish;
	else
		I::EngineClient->SetViewAngles(vWish);

	m_vLastAngles = vWish;
}

void CBotUtils::InvalidateLLAP()
{
	m_tLLAP = {};
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
	auto pCrumbs = F::NavEngine.GetCrumbs();
	if (pCrumbs->size() > 4)
		pCurrentDestinationArea = pCrumbs->at(4).m_pNavArea;

	auto vLocalOrigin = pLocal->GetAbsOrigin();
	auto pLocalNav = pCurrentDestinationArea ? pCurrentDestinationArea : F::NavEngine.FindClosestNavArea(vLocalOrigin);
	if (!pLocalNav)
		return;

	Vector vFrom = pLocalNav->m_vCenter;
	vFrom.z += PLAYER_JUMP_HEIGHT;

	std::vector<std::pair<CBaseEntity*, float>> vEnemiesSorted;
	for (auto pEnemy : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
	{
		if (pEnemy->IsDormant())
			continue;

		if (!ShouldTarget(pLocal, pWeapon, pEnemy->entindex()))
			continue;

		vEnemiesSorted.emplace_back(pEnemy, pEnemy->GetAbsOrigin().DistToSqr(vLocalOrigin));
	}

	for (auto pEnemyBuilding : H::Entities.GetGroup(EntityEnum::BuildingEnemy))
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
	MoveStorage tStorage;
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

		auto pTargetNav = F::NavEngine.FindClosestNavArea(vPredictedPos, false);
		if (pTargetNav)
		{
			Vector vTo = pTargetNav->m_vCenter;

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
	m_eJumpState = STATE_AWAITING_JUMP;
	m_vPredictedJumpPos = {};
	m_vJumpPeakPos = {};
	InvalidateLLAP();
}

bool CBotUtils::IsSurfaceWalkable(const Vector& vNormal)
{
	static const Vector vUp = { 0.f, 0.f, 1.f };
	float flAngle = RAD2DEG(std::acos(vNormal.Dot(vUp)));
	return flAngle < 45.f; // MAX_WALKABLE_ANGLE
}

bool CBotUtils::IsWalkable(CTFPlayer* pLocal, const Vector& vStart, const Vector& vEnd)
{
	if (!pLocal) return false;

	m_vWalkableSegments.clear();
	const float flStepHeight = pLocal->m_flStepSize();
	const float flMaxFallDistance = 250.f;
	const Vector vStepHeight = { 0.f, 0.f, flStepHeight };
	const Vector vMaxFallDistance = { 0.f, 0.f, flMaxFallDistance };
	const Vector vHullMin = { -20.f, -20.f, 0.f };
	const Vector vHullMax = { 20.f, 20.f, 72.f };

	auto PerformTraceHull = [&](const Vector& vS, const Vector& vE) -> CGameTrace
	{
		CGameTrace trace = {};
		CTraceFilterCollideable filter = {};
		filter.pSkip = pLocal;
		filter.iPlayer = PLAYER_NONE;
		filter.iObject = OBJECT_ALL;
		SDK::TraceHull(vS, vE, vHullMin, vHullMax, MASK_PLAYERSOLID, &filter, &trace);
		return trace;
	};

	auto AdjustDirectionToSurface = [&](Vector vDir, const Vector& vNormal) -> Vector
	{
		vDir.Normalize();
		if (!IsSurfaceWalkable(vNormal)) return vDir;

		float flDot = vDir.Dot(vNormal);
		vDir.z -= vNormal.z * flDot;
		vDir.Normalize();
		return vDir;
	};

	CGameTrace groundTrace = PerformTraceHull(vStart + vStepHeight, vStart - vMaxFallDistance);
	Vector vCurrentPos = groundTrace.endpos;
	Vector vLastPos = vCurrentPos;
	Vector vGoalDir = vEnd - vCurrentPos;
	Vector vLastDir = AdjustDirectionToSurface(vGoalDir, groundTrace.plane.normal);

	float flMaxDist = vEnd.DistTo2D(vStart);
	float flMinStepSize = 10.f;

	for (int i = 0; i < 64; i++) 
	{
		Vector vIterStartPos = vCurrentPos;
		float flDistToGoal = (vEnd - vCurrentPos).Length();
		Vector vNextPos = vCurrentPos + vLastDir * std::min(flDistToGoal, 32.f);

		CGameTrace wallTrace = PerformTraceHull(vCurrentPos + vStepHeight, vNextPos + vStepHeight);
		Vector vPreGroundPos = wallTrace.endpos;

		if (wallTrace.startsolid) return false;

		float flTotalDist = (vPreGroundPos - vIterStartPos).Length();
		
		int iNumSegments = std::max(1, static_cast<int>(std::floor(flTotalDist / flMinStepSize)));

		bool bFoundGround = false;
		for (int s = 1; s <= iNumSegments; s++)
		{
			float t = static_cast<float>(s) / iNumSegments;
			Vector vSegmentPos = vIterStartPos + (vPreGroundPos - vIterStartPos) * t;
			CGameTrace segGroundTrace = PerformTraceHull(vSegmentPos + vStepHeight, vSegmentPos - vMaxFallDistance);

			m_vWalkableSegments.push_back({ vCurrentPos, segGroundTrace.endpos });

			if (segGroundTrace.fraction == 1.0f) return false; // Pit or ledge too high

			if (IsSurfaceWalkable(segGroundTrace.plane.normal))
			{
				vLastDir = AdjustDirectionToSurface(vLastDir, segGroundTrace.plane.normal);
				vCurrentPos = segGroundTrace.endpos;
				bFoundGround = true;
			}
			else return false; // Too steep
		}

		if (!bFoundGround) return false;

		float flCurrentDist = vCurrentPos.DistTo2D(vEnd);
		if (flCurrentDist < 16.f)
		{
			if (std::abs(vEnd.z - vCurrentPos.z) < flStepHeight + 2.f) return true;
		}

		if (vCurrentPos.DistTo(vIterStartPos) < 1.f) return false; // Infinite loopfix
	}

	return false;
}

bool CBotUtils::SmartJump(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!pLocal || !pLocal->IsAlive() || !Vars::Misc::Movement::NavBot::SmartJump.Value) return false;

	if (pLocal->OnSolid())
	{
		Vector vVelocity = pLocal->m_vecVelocity();
		Vector vMoveInput = { pCmd->forwardmove, -pCmd->sidemove, 0.f };
		if (vMoveInput.Length() > 0.f)
		{
			Vector vViewAngles = I::EngineClient->GetViewAngles();
			Vector vForward, vRight;
			Math::AngleVectors(vViewAngles, &vForward, &vRight, nullptr);
			vForward.z = vRight.z = 0.f;
			vForward.Normalize();
			vRight.Normalize();

			Vector vRotatedMoveDir = vForward * vMoveInput.x + vRight * vMoveInput.y;
			vVelocity = vRotatedMoveDir.Normalized() * std::max(10.f, vVelocity.Length());
		}

		const float flJumpForce = 277.f;
		const float flGravity = 800.f;
		float flTimeToPeak = flJumpForce / flGravity;
		float flDistTravelled = vVelocity.Length2D() * flTimeToPeak;
		Vector vJumpDirection = vVelocity.Normalized();
		Vector vJumpPeakPos = pLocal->GetAbsOrigin() + vJumpDirection * flDistTravelled;
		m_vJumpPeakPos = vJumpPeakPos;

		const Vector vHullMin = { -23.99f, -23.99f, 0.f };
		const Vector vHullMax = { 23.99f, 23.99f, 62.f };
		const Vector vStepHeight = { 0.f, 0.f, 18.f };
		const Vector vMaxJumpHeight = { 0.f, 0.f, 72.f };

		Vector vTraceStart = pLocal->GetAbsOrigin() + vStepHeight;
		Vector vTraceEnd = vTraceStart + vJumpDirection * flDistTravelled;

		CGameTrace forwardTrace = {};
		CTraceFilterHitscan filter = {};
		filter.pSkip = pLocal;
		SDK::TraceHull(vTraceStart, vTraceEnd, vHullMin, vHullMax, MASK_PLAYERSOLID_BRUSHONLY, &filter, &forwardTrace);

		m_vPredictedJumpPos = forwardTrace.endpos;

		if (forwardTrace.fraction < 1.0f)
		{
			CGameTrace downwardTrace = {};
			SDK::TraceHull(forwardTrace.endpos, forwardTrace.endpos - vMaxJumpHeight, vHullMin, vHullMax, MASK_PLAYERSOLID_BRUSHONLY, &filter, &downwardTrace);

			Vector vLandingPos = downwardTrace.endpos + vJumpDirection * 10.f;
			CGameTrace landingTrace = {};
			SDK::TraceHull(vLandingPos + vMaxJumpHeight, vLandingPos, vHullMin, vHullMax, MASK_PLAYERSOLID_BRUSHONLY, &filter, &landingTrace);

			m_vPredictedJumpPos = landingTrace.endpos;

			if (landingTrace.fraction > 0.f && landingTrace.fraction < 0.75f) // JUMP_FRACTION
			{
				if (IsSurfaceWalkable(landingTrace.plane.normal))
					return true;
			}
		}
	}
	else if (pCmd->buttons & IN_JUMP)
		return true;

	return false;
}

void CBotUtils::HandleSmartJump(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!pLocal || !pLocal->IsAlive() || F::AutoRocketJump.IsRunning())
	{
		m_eJumpState = STATE_AWAITING_JUMP;
		return;
	}

	bool bOnGround = pLocal->OnSolid();
	bool bDucking = pLocal->IsDucking();

	if (bOnGround && bDucking)
		m_eJumpState = STATE_AWAITING_JUMP;

	switch (m_eJumpState)
	{
	case STATE_AWAITING_JUMP:
		if (SmartJump(pLocal, pCmd))
			m_eJumpState = (Vars::Misc::Movement::AutoCTap.Value && bOnGround) ? STATE_CTAP : STATE_JUMP;
		break;
	case STATE_CTAP:
		pCmd->buttons |= IN_DUCK;
		pCmd->buttons &= ~IN_JUMP;
		m_eJumpState = STATE_JUMP;
		break;
	case STATE_JUMP:
		pCmd->buttons &= ~IN_DUCK;
		pCmd->buttons |= IN_JUMP;
		m_eJumpState = STATE_ASCENDING;
		break;
	case STATE_ASCENDING:
		pCmd->buttons |= IN_DUCK;
		if (pLocal->m_vecVelocity().z <= 0.f)
			m_eJumpState = STATE_DESCENDING;
		else if (bOnGround)
			m_eJumpState = STATE_AWAITING_JUMP;
		break;
	case STATE_DESCENDING:
		pCmd->buttons &= ~IN_DUCK;
		if (!bOnGround)
		{
			if (SmartJump(pLocal, pCmd))
			{
				pCmd->buttons &= ~IN_DUCK;
				pCmd->buttons |= IN_JUMP;
				m_eJumpState = (Vars::Misc::Movement::AutoCTap.Value && bOnGround) ? STATE_CTAP : STATE_JUMP;
			}
		}
		else
		{
			pCmd->buttons |= IN_DUCK;
			m_eJumpState = STATE_AWAITING_JUMP;
		}
		break;
	}
}