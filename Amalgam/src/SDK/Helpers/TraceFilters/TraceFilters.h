#pragma once
#include "../../Definitions/Interfaces/IEngineTrace.h"

enum
{
	SKIP_CHECK,
	FORCE_PASS,
	FORCE_HIT
};

enum
{
	WEAPON_INCLUDE,
	WEAPON_EXCLUDE
};
enum
{
	PLAYER_DEFAULT,
	PLAYER_NONE,
	PLAYER_ALL
};
enum
{
	OBJECT_DEFAULT,
	OBJECT_NONE,
	OBJECT_ALL
};

class CTraceFilterHitscan : public ITraceFilter
{
public:
	CTraceFilterHitscan() {}
	CTraceFilterHitscan(const IHandleEntity* pPassEntity)
	{
		m_pSkip = const_cast<CBaseEntity*>(reinterpret_cast<const CBaseEntity*>(pPassEntity));
	}

	bool ShouldHitEntity(IHandleEntity* pServerEntity, int nContentsMask) override;
	TraceType_t GetTraceType() const override;

	CBaseEntity* m_pSkip = nullptr;
	int m_iTeam = -1;

	std::vector<int> m_vWeapons = {
		TF_WEAPON_SNIPERRIFLE, TF_WEAPON_SNIPERRIFLE_CLASSIC, TF_WEAPON_SNIPERRIFLE_DECAP,
		TF_WEAPON_COMPOUND_BOW, TF_WEAPON_REVOLVER
	};

	int m_iType = FORCE_HIT;
	int m_iWeapon = WEAPON_EXCLUDE;
	bool m_bWeapon = false;
	bool m_bIgnoreCart = false;
	bool m_bIgnoreDoors = false;
};

class CTraceFilterCollideable : public ITraceFilter
{
public:
	CTraceFilterCollideable() {}
	CTraceFilterCollideable(const IHandleEntity* pPassEntity)
	{
		m_pSkip = const_cast<CBaseEntity*>(reinterpret_cast<const CBaseEntity*>(pPassEntity));
	}

	bool ShouldHitEntity(IHandleEntity* pServerEntity, int nContentsMask) override;
	TraceType_t GetTraceType() const override;

	CBaseEntity* m_pSkip = nullptr;
	int m_iTeam = -1;

	std::vector<int> m_vWeapons = { TF_WEAPON_CROSSBOW, TF_WEAPON_LUNCHBOX, TF_WEAPON_FLAREGUN };

	int m_iType = FORCE_HIT;
	int m_iWeapon = WEAPON_INCLUDE;
	bool m_bWeapon = false;
	int m_iPlayer = PLAYER_DEFAULT;
	int m_iObject = OBJECT_ALL;
	bool m_bMisc = false;
	bool m_bIgnoreCart = false;
	bool m_bIgnoreDoors = false;
};

class CTraceFilterWorldAndPropsOnly : public ITraceFilter
{
public:
	CTraceFilterWorldAndPropsOnly() {}
	CTraceFilterWorldAndPropsOnly(const IHandleEntity* pPassEntity)
	{
		m_pSkip = const_cast<CBaseEntity*>(reinterpret_cast<const CBaseEntity*>(pPassEntity));
	}

	bool ShouldHitEntity(IHandleEntity* pServerEntity, int nContentsMask) override;
	TraceType_t GetTraceType() const override;

	CBaseEntity* m_pSkip = nullptr;
	int m_iTeam = -1;
};


class CTraceFilterNavigation : public ITraceFilter
{
public:
	CTraceFilterNavigation() {}
	CTraceFilterNavigation(const IHandleEntity* pPassEntity)
	{
		m_pSkip = const_cast<CBaseEntity*>(reinterpret_cast<const CBaseEntity*>(pPassEntity));
	}

	bool ShouldHitEntity(IHandleEntity* pServerEntity, int nContentsMask) override;
	TraceType_t GetTraceType() const override;

	CBaseEntity* m_pSkip = nullptr;
	int m_iTeam = -1;

	int m_iPlayer = PLAYER_NONE;
	int m_iObject = OBJECT_NONE;
};