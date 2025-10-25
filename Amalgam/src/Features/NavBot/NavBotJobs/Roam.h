#pragma once
#include "../NavBotCore.h"

class CNavBotRoam
{
public:
	bool Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);

	bool m_bDefending = false;
};

ADD_FEATURE(CNavBotRoam, NavBotRoam);