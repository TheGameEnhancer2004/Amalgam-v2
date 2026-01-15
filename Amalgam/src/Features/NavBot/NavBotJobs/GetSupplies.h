#pragma once
#include "../NavBotCore.h"

Enum(GetSupply,
	Health = 1 << 0,
	Ammo = 1 << 1,
	Forced = 1 << 2,
	LowPrio = 1 << 3
);

struct SupplyData_t
{
	bool m_bDispenser = false;
	float m_flRespawnTime = 0.f;
	Vector m_vOrigin = {};

	// The lazy way
	SupplyData_t* m_pOriginalSelfPtr = nullptr;
};

class CNavBotSupplies
{
private:
	std::vector<SupplyData_t> m_vCachedHealthOrigins;
	std::vector<SupplyData_t> m_vCachedAmmoOrigins;
	std::vector<SupplyData_t> m_vTempDispensers;
	std::vector<SupplyData_t> m_vTempMain;

	// Get entities of given itemtypes (Used for health/ammo)
	// Use true for health packs, use false for ammo packs
	bool GetSuppliesData(CTFPlayer* pLocal, bool& bClosestTaken, bool bHealth = false);
	bool GetDispensersData(CTFPlayer* pLocal);

	bool ShouldSearchHealth(CTFPlayer* pLocal, bool bLowPrio = false);
	bool ShouldSearchAmmo(CTFPlayer* pLocal);
	bool GetSupply(CUserCmd* pCmd, CTFPlayer* pLocal, Vector vLocalOrigin, SupplyData_t* pSupplyData, const int iPriority);

	void UpdateTakenState();
public:
	bool Run(CUserCmd* pCmd, CTFPlayer* pLocal, int iFlags);

	void AddCachedSupplyOrigin(Vector vOrigin, bool bHealth);
	void ResetCachedOrigins();
	void ResetTemp();
};

ADD_FEATURE(CNavBotSupplies, NavBotSupplies);