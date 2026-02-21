#pragma once
#include "../NavBotCore.h"

class CNavBotRoam
{
private:
	std::vector<CNavArea*> m_vVisitedAreas;
	std::unordered_set<CNavArea*> m_sConnectedAreas;

	CNavArea* m_pCurrentTargetArea = nullptr;
	CNavArea* m_pLastConnectedSeed = nullptr;
	void* m_pLastMap = nullptr;

	int m_iConsecutiveFails = 0;
public:
	bool Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);

	void Reset();
	bool m_bDefending = false;
};

ADD_FEATURE(CNavBotRoam, NavBotRoam);