#pragma once
#include "../NavBotCore.h"

class CNavBotSnipe
{
private:
	bool IsAreaValidForSnipe(Vector vEntOrigin, Vector vAreaOrigin, bool bFixSentryZ = true);
	int IsSnipeTargetValid(CTFPlayer* pLocal, int iBuildingIdx);
	bool TryToSnipe(CBaseObject* pBulding);
public:
	bool Run(CTFPlayer* pLocal);
};

ADD_FEATURE(CNavBotSnipe, NavBotSnipe);