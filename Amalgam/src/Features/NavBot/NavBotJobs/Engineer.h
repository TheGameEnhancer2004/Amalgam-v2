#pragma once
#include "../NavBotCore.h"

Enum(EngineerTaskStage, None,
	 BuildSentry, BuildDispenser,
	 SmackSentry, SmackDispenser
)

struct BuildingSpot_t
{
	float m_flDistanceToTarget = FLT_MAX;
	Vector m_vPos = {};
};

class CNavBotEngineer
{
private:
	int m_iBuildAttempts = 0;
	float m_flBuildYaw = 0.0f;
	std::vector<BuildingSpot_t>  m_vBuildingSpots;
	std::vector<Vector> m_vFailedSpots;
private:
	bool BuildingNeedsToBeSmacked(CBaseObject* pBuilding);
	bool BlacklistedFromBuilding(CNavArea* pArea);
	bool NavToSentrySpot(Vector vLocalOrigin);
	bool BuildBuilding(CUserCmd* pCmd, CTFPlayer* pLocal, ClosestEnemy_t tClosestEnemy, bool bDispenser);
	bool SmackBuilding(CUserCmd* pCmd, CTFPlayer* pLocal, CBaseObject* pBuilding);
public:
	bool IsEngieMode(CTFPlayer* pLocal);
	bool Run(CUserCmd* pCmd, CTFPlayer* pLocal, ClosestEnemy_t tClosestEnemy);

	void RefreshBuildingSpots(CTFPlayer* pLocal, ClosestEnemy_t tClosestEnemy, bool bForce = false);
	void RefreshLocalBuildings(CTFPlayer* pLocal);
	void Reset();
	void Render();

	BuildingSpot_t m_tCurrentBuildingSpot = {};
	CObjectSentrygun* m_pMySentryGun;
	CObjectDispenser* m_pMyDispenser;
	float m_flDistToSentry = FLT_MAX;
	float m_flDistToDispenser = FLT_MAX;

	EngineerTaskStageEnum::EngineerTaskStageEnum m_eTaskStage = EngineerTaskStageEnum::None;
};

ADD_FEATURE(CNavBotEngineer, NavBotEngineer);