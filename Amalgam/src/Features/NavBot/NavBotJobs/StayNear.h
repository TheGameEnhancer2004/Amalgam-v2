#pragma once
#include "../NavBotCore.h"

class CNavBotStayNear
{
private:
	bool StayNearTarget(int iEntIndex);
	bool IsAreaValidForStayNear(Vector vEntOrigin, CNavArea* pArea, bool bFixLocalZ = true);
	int IsStayNearTargetValid(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, int iEntIndex);
public:
	bool Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);

	int m_iStayNearTargetIdx = -1;
	std::wstring m_sFollowTargetName = {};
};

ADD_FEATURE(CNavBotStayNear, NavBotStayNear);