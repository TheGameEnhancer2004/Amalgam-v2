#pragma once
#include "../NavBotCore.h"

class CNavBotSnipe
{
private:
	bool IsAreaValidForSnipe(Vector vEntOrigin, Vector vAreaOrigin, bool bFixSentryZ = true);
	bool TryToSnipe(int iEntIdx);
public:
	bool Run(CTFPlayer* pLocal);

	int m_iTargetIdx = -1;
};

ADD_FEATURE(CNavBotSnipe, NavBotSnipe);