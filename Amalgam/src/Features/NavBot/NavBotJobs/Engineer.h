#pragma once
#include "../NavBotCore.h"

class CNavBotEngineer
{
private:
	int m_iBuildAttempts = 0;
	std::vector<Vector>  m_vBuildingSpots;
private:
	bool BuildingNeedsToBeSmacked(CBaseObject* pBuilding);
	bool BlacklistedFromBuilding(CNavArea* pArea);
	bool NavToSentrySpot();
	bool BuildBuilding(CUserCmd* pCmd, CTFPlayer* pLocal, ClosestEnemy_t tClosestEnemy, bool bDispenser);
	bool SmackBuilding(CUserCmd* pCmd, CTFPlayer* pLocal, CBaseObject* pBuilding);
public:
	bool IsEngieMode(CTFPlayer* pLocal);
	bool Run(CUserCmd* pCmd, CTFPlayer* pLocal, ClosestEnemy_t tClosestEnemy);

	void RefreshBuildingSpots(CTFPlayer* pLocal, ClosestEnemy_t tClosestEnemy, bool bForce = false);
	void RefreshLocalBuildings(CTFPlayer* pLocal);
	void Reset();

	std::wstring m_sEngineerTask{};
	Vector m_vCurrentBuildingSpot = {};
	CObjectSentrygun* m_pMySentryGun;
	CObjectDispenser* m_pMyDispenser;
};

ADD_FEATURE(CNavBotEngineer, NavBotEngineer);