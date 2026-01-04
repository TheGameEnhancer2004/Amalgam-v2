#include "TraceFilters.h"

#include "../../SDK.h"

bool CTraceFilterHitscan::ShouldHitEntity(IHandleEntity* pServerEntity, int nContentsMask)
{
	if (!pServerEntity || pServerEntity == pSkip)
		return false;

	auto pEntity = reinterpret_cast<CBaseEntity*>(pServerEntity);
	if (iTeam == -1) iTeam = pSkip ? pSkip->m_iTeamNum() : 0;
	if (iType != SKIP_CHECK && !vWeapons.empty())
	{
		if (auto pWeapon = pSkip && pSkip->IsPlayer() ? pSkip->As<CTFPlayer>()->m_hActiveWeapon()->As<CTFWeaponBase>() : nullptr)
		{
			int iWeaponID = pWeapon->GetWeaponID();
			bWeapon = std::find(vWeapons.begin(), vWeapons.end(), iWeaponID) != vWeapons.end();
		}
		vWeapons.clear();
	}

	switch (pEntity->GetClassID())
	{
	case ETFClassID::CTFAmmoPack:
	case ETFClassID::CFuncAreaPortalWindow:
	case ETFClassID::CFuncRespawnRoomVisualizer:
	case ETFClassID::CTFReviveMarker: return false;
	case ETFClassID::CBaseDoor:
	case ETFClassID::CBasePropDoor: return !bIgnoreDoors;
	case ETFClassID::CObjectCartDispenser:
	case ETFClassID::CFuncTrackTrain:
	case ETFClassID::CTFGenericBomb: return !bIgnoreCart;
	case ETFClassID::CTFMedigunShield: return pEntity->m_iTeamNum() != iTeam;
	case ETFClassID::CTFPlayer:
	case ETFClassID::CBaseObject:
	case ETFClassID::CObjectSentrygun:
	case ETFClassID::CObjectDispenser:
	case ETFClassID::CObjectTeleporter: 
		if (iType != SKIP_CHECK && (iWeapon == WEAPON_INCLUDE ? bWeapon : !bWeapon))
			return iType == FORCE_HIT ? true : false;
		return pEntity->m_iTeamNum() != iTeam;
	}

	return true;
}
TraceType_t CTraceFilterHitscan::GetTraceType() const
{
	return TRACE_EVERYTHING;
}

bool CTraceFilterCollideable::ShouldHitEntity(IHandleEntity* pServerEntity, int nContentsMask)
{
	if (!pServerEntity || pServerEntity == pSkip)
		return false;

	auto pEntity = reinterpret_cast<CBaseEntity*>(pServerEntity);
	if (iTeam == -1) iTeam = pSkip ? pSkip->m_iTeamNum() : 0;
	if (iType != SKIP_CHECK && !vWeapons.empty())
	{
		if (auto pWeapon = pSkip && pSkip->IsPlayer() ? pSkip->As<CTFPlayer>()->m_hActiveWeapon()->As<CTFWeaponBase>() : nullptr)
		{
			int iWeaponID = pWeapon->GetWeaponID();
			bWeapon = std::find(vWeapons.begin(), vWeapons.end(), iWeaponID) != vWeapons.end();
		}
		vWeapons.clear();
	}

	switch (pEntity->GetClassID())
	{
	case ETFClassID::CBaseEntity:
	case ETFClassID::CBaseDoor:
	case ETFClassID::CBasePropDoor:
		return !bIgnoreDoors;
	case ETFClassID::CDynamicProp:
	case ETFClassID::CPhysicsProp:
	case ETFClassID::CPhysicsPropMultiplayer:
	case ETFClassID::CFunc_LOD:
	case ETFClassID::CFuncConveyor:
	case ETFClassID::CTFPumpkinBomb: return true;
	case ETFClassID::CObjectCartDispenser:
	case ETFClassID::CFuncTrackTrain:
	case ETFClassID::CTFGenericBomb: return !bIgnoreCart;
	case ETFClassID::CFuncRespawnRoomVisualizer:
		if (nContentsMask & CONTENTS_PLAYERCLIP)
			return pEntity->m_iTeamNum() != iTeam;
		break;
	case ETFClassID::CTFMedigunShield:
		if (!(nContentsMask & CONTENTS_PLAYERCLIP))
			return pEntity->m_iTeamNum() != iTeam;
		break;
	case ETFClassID::CTFPlayer:
		if (iPlayer == PLAYER_ALL)
			return true;
		if (iPlayer == PLAYER_NONE)
			return false;
		if (iType != SKIP_CHECK && (iWeapon == WEAPON_INCLUDE ? bWeapon : !bWeapon))
			return iType == FORCE_HIT ? true : false;
		return pEntity->m_iTeamNum() != iTeam;
	case ETFClassID::CBaseObject:
	case ETFClassID::CObjectSentrygun:
	case ETFClassID::CObjectDispenser: return iObject == OBJECT_ALL ? true : iObject == OBJECT_NONE ? false : pEntity->m_iTeamNum() != iTeam;
	case ETFClassID::CObjectTeleporter: return true;
	case ETFClassID::CTFBaseBoss:
	case ETFClassID::CTFTankBoss:
	case ETFClassID::CMerasmus:
	case ETFClassID::CEyeballBoss:
	case ETFClassID::CHeadlessHatman:
	case ETFClassID::CZombie: return bMisc;
	case ETFClassID::CTFGrenadePipebombProjectile: return bMisc && pEntity->As<CTFGrenadePipebombProjectile>()->m_iType() == TF_GL_MODE_REMOTE_DETONATE;
	}

	return false;
}
TraceType_t CTraceFilterCollideable::GetTraceType() const
{
	return TRACE_EVERYTHING;
}

bool CTraceFilterWorldAndPropsOnly::ShouldHitEntity(IHandleEntity* pServerEntity, int nContentsMask)
{
	if (!pServerEntity || pServerEntity == pSkip)
		return false;
	if (pServerEntity->GetRefEHandle().GetSerialNumber() == (1 << 15))
		return I::ClientEntityList->GetClientEntity(0) != pSkip;

	auto pEntity = reinterpret_cast<CBaseEntity*>(pServerEntity);
	if (iTeam == -1) iTeam = pSkip ? pSkip->m_iTeamNum() : 0;

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
	case ETFClassID::CFuncConveyor: return true;
	case ETFClassID::CFuncRespawnRoomVisualizer: return nContentsMask & CONTENTS_PLAYERCLIP && pEntity->m_iTeamNum() != iTeam;
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
	if (pEntity->entindex() != 0 && pEntity->GetClassID() != ETFClassID::CBaseEntity)
	{
		if (pEntity->GetClassID() == ETFClassID::CFuncRespawnRoomVisualizer)
		{
			auto pLocal = H::Entities.GetLocal();
			const int iTargetTeam = pEntity->m_iTeamNum(), iLocalTeam = pLocal ? pLocal->m_iTeamNum() : iTargetTeam;

			// Cant we just check for the teamnum here???

			// If we can't collide, hit it
			if (!pEntity->ShouldCollide(MOVEMENT_COLLISION_GROUP, iLocalTeam == TF_TEAM_RED ? RED_CONTENTS_MASK : BLU_CONTENTS_MASK))
				return true;
		}
		return false;
	}
	return true;
}

TraceType_t CTraceFilterNavigation::GetTraceType() const
{
	return TRACE_EVERYTHING;
}