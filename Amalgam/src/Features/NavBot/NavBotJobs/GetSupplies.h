#pragma once
#include "../NavBotCore.h"

class CNavBotSupplies
{
private:
	std::vector<std::pair<bool, Vector>> m_vCachedHealthOrigins;
	std::vector<std::pair<bool, Vector>> m_vCachedAmmoOrigins;
	std::unordered_set<int> m_sCachedHealthIndexes;
	std::unordered_set<int> m_sCachedAmmoIndexes;

	// Get entities of given itemtypes (Used for health/ammo)
	// Use true for health packs, use false for ammo packs
	std::vector<std::pair<bool, Vector>> GetEntities(CTFPlayer* pLocal, bool bHealth = false);
	std::vector<std::pair<bool, Vector>> GetDispensers(CTFPlayer* pLocal);

	bool ShouldSearchHealth(CTFPlayer* pLocal, bool bLowPrio = false);
	bool ShouldSearchAmmo(CTFPlayer* pLocal);
public:
	bool GetHealth(CUserCmd* pCmd, CTFPlayer* pLocal, bool bLowPrio = false);
	bool GetAmmo(CUserCmd* pCmd, CTFPlayer* pLocal, bool bForce = false);
	void Reset();
};

ADD_FEATURE(CNavBotSupplies, NavBotSupplies);