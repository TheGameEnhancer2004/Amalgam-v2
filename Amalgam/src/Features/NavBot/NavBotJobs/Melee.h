#pragma once
#include "../NavBotCore.h"

class CNavBotMelee
{
public:
	bool Run(CUserCmd* pCmd, CTFPlayer* pLocal, int iSlot, ClosestEnemy_t tClosestEnemy);
};

ADD_FEATURE(CNavBotMelee, NavBotMelee);