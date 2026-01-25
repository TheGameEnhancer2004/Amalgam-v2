#include "TraceFilters.h"

#include "../../SDK.h"

bool CTraceFilterHitscan::ShouldHitEntity(IHandleEntity* pServerEntity, int nContentsMask)
{
	if (!pServerEntity || pServerEntity == m_pSkip)
		return false;

	auto pEntity = reinterpret_cast<CBaseEntity*>(pServerEntity);
	const auto nClassID = pEntity->GetClassID();

	switch (nClassID)
	{
	case ETFClassID::CTFAmmoPack:
	case ETFClassID::CFuncAreaPortalWindow:
	case ETFClassID::CFuncRespawnRoomVisualizer:
	case ETFClassID::CTFReviveMarker:
		return false;
	case ETFClassID::CBaseDoor:
	case ETFClassID::CBasePropDoor:
		return !m_bIgnoreDoors;
	case ETFClassID::CObjectCartDispenser:
	case ETFClassID::CFuncTrackTrain:
		return !m_bIgnoreCart;
	}

	if (m_iTeam == -1)
		m_iTeam = m_pSkip ? m_pSkip->m_iTeamNum() : TEAM_UNASSIGNED;

	if (nClassID == ETFClassID::CTFPlayer ||
		nClassID == ETFClassID::CBaseObject ||
		nClassID == ETFClassID::CObjectSentrygun ||
		nClassID == ETFClassID::CObjectDispenser)
	{
		if (m_iType != SKIP_CHECK && !m_vWeapons.empty())
		{
			if (m_pSkip && m_pSkip->IsPlayer())
			{
				auto pPlayer = m_pSkip->As<CTFPlayer>();
				if (auto pWeapon = pPlayer->m_hActiveWeapon()->As<CTFWeaponBase>())
				{
					int iActiveWeaponID = pWeapon->GetWeaponID();
					bool bFound = false;
					for (int iWeaponId : m_vWeapons)
					{
						if (iWeaponId == iActiveWeaponID) { bFound = true; break; }
					}
					m_bWeapon = bFound;
				}
			}
			m_vWeapons.clear();
		}

		if (m_iType != SKIP_CHECK && (m_iWeapon == WEAPON_INCLUDE ? m_bWeapon : !m_bWeapon))
			return m_iType == FORCE_HIT;

		return pEntity->m_iTeamNum() != m_iTeam;
	}
	if (nClassID == ETFClassID::CObjectTeleporter)
		return true;
	if (nClassID == ETFClassID::CTFMedigunShield)
		return pEntity->m_iTeamNum() != m_iTeam;

	return true;
}
TraceType_t CTraceFilterHitscan::GetTraceType() const
{
	return TRACE_EVERYTHING;
}

bool CTraceFilterCollideable::ShouldHitEntity(IHandleEntity* pServerEntity, int nContentsMask)
{
	if (!pServerEntity || pServerEntity == m_pSkip)
		return false;

	auto pEntity = reinterpret_cast<CBaseEntity*>(pServerEntity);
	const auto nClassID = pEntity->GetClassID();

	if (m_iTeam == -1) 
		m_iTeam = m_pSkip ? m_pSkip->m_iTeamNum() : TEAM_UNASSIGNED;

	switch (nClassID)
	{
	case ETFClassID::CBaseEntity:
	case ETFClassID::CDynamicProp:
	case ETFClassID::CPhysicsProp:
	case ETFClassID::CPhysicsPropMultiplayer:
	case ETFClassID::CFunc_LOD:
	case ETFClassID::CFuncConveyor:
	case ETFClassID::CTFGenericBomb: 
	case ETFClassID::CTFPumpkinBomb:
		return true;
	case ETFClassID::CBaseDoor:
	case ETFClassID::CBasePropDoor:
		return !m_bIgnoreDoors;
	case ETFClassID::CObjectCartDispenser:
	case ETFClassID::CFuncTrackTrain:
		return !m_bIgnoreCart;
	case ETFClassID::CTFPlayer:
	{
		if (m_iPlayer == PLAYER_ALL) return true;
		if (m_iPlayer == PLAYER_NONE) return false;

		if (m_iType != SKIP_CHECK && !m_vWeapons.empty())
		{
			if (m_pSkip && m_pSkip->IsPlayer())
			{
				auto pPlayer = m_pSkip->As<CTFPlayer>();
				if (auto pWeapon = pPlayer->m_hActiveWeapon()->As<CTFWeaponBase>())
				{
					int iActiveWeaponID = pWeapon->GetWeaponID();
					bool bFound = false;
					for (int iWeaponId : m_vWeapons)
					{
						if (iWeaponId == iActiveWeaponID) { bFound = true; break; }
					}
					m_bWeapon = bFound;
				}
			}
			m_vWeapons.clear();
		}

		if (m_iType != SKIP_CHECK && (m_iWeapon == WEAPON_INCLUDE ? m_bWeapon : !m_bWeapon))
			return m_iType == FORCE_HIT;

		return pEntity->m_iTeamNum() != m_iTeam;
	}
	case ETFClassID::CBaseObject:
	case ETFClassID::CObjectSentrygun:
	case ETFClassID::CObjectDispenser:
	case ETFClassID::CObjectTeleporter:
		return m_iObject == OBJECT_ALL ? true : m_iObject == OBJECT_NONE ? false : pEntity->m_iTeamNum() != m_iTeam;
	case ETFClassID::CTFBaseBoss:
	case ETFClassID::CTFTankBoss:
	case ETFClassID::CMerasmus:
	case ETFClassID::CEyeballBoss:
	case ETFClassID::CHeadlessHatman:
	case ETFClassID::CZombie:
		return m_bMisc;
	case ETFClassID::CTFGrenadePipebombProjectile:
		return m_bMisc && pEntity->As<CTFGrenadePipebombProjectile>()->m_iType() == TF_GL_MODE_REMOTE_DETONATE;
	case ETFClassID::CFuncRespawnRoomVisualizer:
		return (nContentsMask & CONTENTS_PLAYERCLIP) && pEntity->m_iTeamNum() != m_iTeam;
	case ETFClassID::CTFMedigunShield:
		return !(nContentsMask & CONTENTS_PLAYERCLIP) && pEntity->m_iTeamNum() != m_iTeam;
	}

	return false;
}
TraceType_t CTraceFilterCollideable::GetTraceType() const
{
	return TRACE_EVERYTHING;
}

bool CTraceFilterWorldAndPropsOnly::ShouldHitEntity(IHandleEntity* pServerEntity, int nContentsMask)
{
	if (!pServerEntity || pServerEntity == m_pSkip)
		return false;
	if (pServerEntity->GetRefEHandle().GetSerialNumber() == (1 << 15))
		return I::ClientEntityList->GetClientEntity(0) != m_pSkip;

	auto pEntity = reinterpret_cast<CBaseEntity*>(pServerEntity);
	const auto nClassID = pEntity->GetClassID();

	if (m_iTeam == -1) 
		m_iTeam = m_pSkip ? m_pSkip->m_iTeamNum() : TEAM_UNASSIGNED;

	switch (pEntity->GetClassID())
	{
	case ETFClassID::CBaseEntity:
	case ETFClassID::CBaseDoor:
	case ETFClassID::CDynamicProp:
	case ETFClassID::CPhysicsProp:
	case ETFClassID::CPhysicsPropMultiplayer:
	case ETFClassID::CFunc_LOD:
	case ETFClassID::CObjectCartDispenser:
	case ETFClassID::CFuncTrackTrain:
	case ETFClassID::CFuncConveyor:
		return true;
	case ETFClassID::CFuncRespawnRoomVisualizer:
		return nContentsMask & CONTENTS_PLAYERCLIP && pEntity->m_iTeamNum() != m_iTeam;
	}

	return false;
}
TraceType_t CTraceFilterWorldAndPropsOnly::GetTraceType() const
{
	return TRACE_EVERYTHING_FILTER_PROPS;
}

#define MOVEMENT_COLLISION_GROUP 8
#define RED_CONTENTS_MASK 0x800
#define BLU_CONTENTS_MASK 0x1000

bool CTraceFilterNavigation::ShouldHitEntity(IHandleEntity* pServerEntity, int nContentsMask)
{
	if (!pServerEntity)
		return false;

	auto pEntity = reinterpret_cast<CBaseEntity*>(pServerEntity);
	if (pEntity->entindex() == 0)
		return true;

	const auto nClassID = pEntity->GetClassID();
	if (nClassID == ETFClassID::CBaseEntity)
		return true;

	if (nClassID == ETFClassID::CObjectTeleporter)
		return (nContentsMask & CONTENTS_PLAYERCLIP) || m_iObject != OBJECT_NONE;

	if (m_iTeam == -1) 
		m_iTeam = m_pSkip ? m_pSkip->m_iTeamNum() : TEAM_UNASSIGNED;

	if (nClassID == ETFClassID::CTFPlayer)
	{
		return false;
	}

	switch (nClassID)
	{
	case ETFClassID::CBaseDoor:
	case ETFClassID::CPhysicsProp:
	case ETFClassID::CPhysicsPropMultiplayer:
	case ETFClassID::CFunc_LOD:
	case ETFClassID::CObjectCartDispenser:
	case ETFClassID::CFuncTrackTrain:
	case ETFClassID::CFuncConveyor:
	case ETFClassID::CObjectSentrygun:
	case ETFClassID::CObjectDispenser:
	case ETFClassID::CObjectTeleporter:
	case ETFClassID::CBaseProjectile:
	case ETFClassID::CBaseGrenade:
	case ETFClassID::CTFWeaponBaseGrenadeProj:
	case ETFClassID::CTFWeaponBaseMerasmusGrenade:
	case ETFClassID::CTFGrenadePipebombProjectile:
	case ETFClassID::CTFStunBall:
	case ETFClassID::CTFBall_Ornament:
	case ETFClassID::CTFProjectile_Jar:
	case ETFClassID::CTFProjectile_Cleaver:
	case ETFClassID::CTFProjectile_JarGas:
	case ETFClassID::CTFProjectile_JarMilk:
	case ETFClassID::CTFProjectile_SpellBats:
	case ETFClassID::CTFProjectile_SpellKartBats:
	case ETFClassID::CTFProjectile_SpellMeteorShower:
	case ETFClassID::CTFProjectile_SpellMirv:
	case ETFClassID::CTFProjectile_SpellPumpkin:
	case ETFClassID::CTFProjectile_SpellSpawnBoss:
	case ETFClassID::CTFProjectile_SpellSpawnHorde:
	case ETFClassID::CTFProjectile_SpellSpawnZombie:
	case ETFClassID::CTFProjectile_SpellTransposeTeleport:
	case ETFClassID::CTFProjectile_Throwable:
	case ETFClassID::CTFProjectile_ThrowableBreadMonster:
	case ETFClassID::CTFProjectile_ThrowableBrick:
	case ETFClassID::CTFProjectile_ThrowableRepel:
	case ETFClassID::CTFBaseRocket:
	case ETFClassID::CTFFlameRocket:
	case ETFClassID::CTFProjectile_Arrow:
	case ETFClassID::CTFProjectile_GrapplingHook:
	case ETFClassID::CTFProjectile_HealingBolt:
	case ETFClassID::CTFProjectile_Rocket:
	case ETFClassID::CTFProjectile_BallOfFire:
	case ETFClassID::CTFProjectile_MechanicalArmOrb:
	case ETFClassID::CTFProjectile_SentryRocket:
	case ETFClassID::CTFProjectile_SpellFireball:
	case ETFClassID::CTFProjectile_SpellLightningOrb:
	case ETFClassID::CTFProjectile_SpellKartOrb:
	case ETFClassID::CTFProjectile_EnergyBall:
	case ETFClassID::CTFProjectile_Flare:
	case ETFClassID::CTFBaseProjectile:
	case ETFClassID::CTFProjectile_EnergyRing:
		return false;
	case ETFClassID::CFuncRespawnRoomVisualizer:
	{
		auto pLocal = H::Entities.GetLocal();
		const int iTargetTeam = pEntity->m_iTeamNum(), iLocalTeam = pLocal ? pLocal->m_iTeamNum() : iTargetTeam;

		if (pEntity->ShouldCollide(MOVEMENT_COLLISION_GROUP, iLocalTeam == TF_TEAM_RED ? RED_CONTENTS_MASK : BLU_CONTENTS_MASK))
			return true;
		break;
	}
	}

	if (nClassID != ETFClassID::CBaseEntity)
		return false;

	return false;
}

TraceType_t CTraceFilterNavigation::GetTraceType() const
{
	return TRACE_EVERYTHING;
}