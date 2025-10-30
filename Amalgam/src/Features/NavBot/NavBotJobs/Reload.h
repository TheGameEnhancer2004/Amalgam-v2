#pragma once
#include "../NavBotCore.h"

class CNavBotReload
{
public:
	bool Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);
	bool RunSafe(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);
	int GetReloadWeaponSlot(CTFPlayer* pLocal, ClosestEnemy_t tClosestEnemy);

	int m_iLastReloadSlot = -1;
};

ADD_FEATURE(CNavBotReload, NavBotReload);