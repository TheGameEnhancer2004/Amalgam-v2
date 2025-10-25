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
	
	std::wstring m_sFollowTargetName = {};
	int m_iStayNearTargetIdx = -1;
};

ADD_FEATURE(CNavBotStayNear, NavBotStayNear);