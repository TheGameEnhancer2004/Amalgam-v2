#pragma once
#include "../NavBotCore.h"

class CNavBotSnipe
{
private:
	bool IsAreaValidForSnipe(Vector vEntOrigin, Vector vAreaOrigin, bool bShortRangeClass, bool bFixSentryZ = true);
	bool TryToSnipe(int iEntIdx, bool bShortRangeClass);
public:
	bool Run(CTFPlayer* pLocal);

	int m_iTargetIdx = -1;
};

ADD_FEATURE(CNavBotSnipe, NavBotSnipe);