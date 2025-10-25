#pragma once
#include "../NavBotCore.h"

class CNavBotDanger
{
public:
	// Run away from dangerous areas
	bool EscapeDanger(CTFPlayer* pLocal);
	bool EscapeProjectiles(CTFPlayer* pLocal);
	bool EscapeSpawn(CTFPlayer* pLocal);
};

ADD_FEATURE(CNavBotDanger, NavBotDanger);