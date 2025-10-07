#pragma once
#include "BotUtils.h"

enum slots
{
	primary = 1,
	secondary,
	melee,
	pda1,
	pda2
};

enum building
{
    dispenser = 0,
    sentry    = 2
};

class CNavBot
{
public:
	std::vector<Vector> m_vSlightDangerDrawlistNormal;
	std::vector<Vector> m_vSlightDangerDrawlistDormant;
	bool m_bDefending = false;
	std::wstring m_sFollowTargetName{};
	std::wstring m_sEngineerTask{};
	int m_iStayNearTargetIdx = -1;
	std::optional<Vector> m_vCurrentCaptureSpot;
	std::optional<Vector> m_vCurrentCaptureCenter;
	std::optional<int> m_iCurrentCapturePointIdx;
	std::optional<Vector> m_vLastClaimedCaptureSpot;
	Timer m_tCaptureClaimRefresh;
private:
	// Controls the bot parameters like distance from enemy
	struct bot_class_config
	{
		float m_flMinFullDanger;
		float m_flMinSlightDanger;
		float m_flMax;
		bool m_bPreferFar;
	};

	const bot_class_config CONFIG_SHORT_RANGE = { 140.0f, 400.0f, 600.0f, false };
	const bot_class_config CONFIG_MID_RANGE = { 200.0f, 500.0f, 3000.0f, true };
	const bot_class_config CONFIG_LONG_RANGE = { 300.0f, 500.0f, 4000.0f, true };
	const bot_class_config CONFIG_ENGINEER = { 200.0f, 500.0f, 3000.0f, false };
	const bot_class_config CONFIG_GUNSLINGER_ENGINEER = { 50.0f, 300.0f, 2000.0f, false };
	bot_class_config m_tSelectedConfig;

	std::vector<Vector> m_vSniperSpots;
	std::vector<Vector>  m_vBuildingSpots;
	std::optional<Vector> vCurrentBuildingSpot;
	int m_iMySentryIdx = -1;
	int m_iMyDispenserIdx = -1;
	int m_iBuildAttempts = 0;
	int m_iLastReloadSlot = -1;

	std::vector<std::pair<uint32_t, Vector>> m_vLocalBotPositions;
	int m_iPositionInFormation = -1;
	float m_flFormationDistance = 120.0f; 
	Timer m_tUpdateFormationTimer;

	// Overwrite to return true for payload carts as an example
	bool m_bOverwriteCapture = false;
private:
	bool ShouldAssist(CTFPlayer* pLocal, int iTargetIdx);

	// Get entities of given itemtypes (Used for health/ammo)
	// Use true for health packs, use false for ammo packs
	std::vector<CBaseEntity*> GetEntities(CTFPlayer* pLocal, bool bHealth = false);
	std::vector<CObjectDispenser*> GetDispensers(CTFPlayer* pLocal);

	bool ShouldSearchHealth(CTFPlayer* pLocal, bool bLowPrio = false);
	bool ShouldSearchAmmo(CTFPlayer* pLocal);
	bool GetHealth(CUserCmd* pCmd, CTFPlayer* pLocal, bool bLowPrio = false);
	bool GetAmmo(CUserCmd* pCmd, CTFPlayer* pLocal, bool bForce = false);

	void UpdateEnemyBlacklist(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, int iSlot);
	void RefreshSniperSpots();
	void RefreshBuildingSpots(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, ClosestEnemy_t tClosestEnemy, bool bForce = false);
	void RefreshLocalBuildings(CTFPlayer* pLocal);

	bool IsAreaValidForStayNear(Vector vEntOrigin, CNavArea* pArea, bool bFixLocalZ = true);
	bool IsAreaValidForSnipe(Vector vEntOrigin, Vector vAreaOrigin, bool bFixSentryZ = true);
	int IsStayNearTargetValid(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, int iEntIndex);
	int IsSnipeTargetValid(CTFPlayer* pLocal, int iBuildingIdx);

	// Recursive function to find hiding spot
	std::optional<std::pair<CNavArea*, int>> FindClosestHidingSpot(CNavArea* pArea, std::optional<Vector> vVischeckPoint, int iRecursionCount, int iRecursionIndex = 0);

	bool RunSafeReload(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);
	bool RunReload(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);

	bool StayNearTarget(int iEntIndex);
	bool StayNear(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);

	bool TryToSnipe(CBaseObject* pBulding);
	bool SnipeSentries(CTFPlayer* pLocal);

	// Auto engie
	bool IsEngieMode(CTFPlayer* pLocal);
	bool BuildingNeedsToBeSmacked(CBaseObject* pBulding);
	bool BlacklistedFromBuilding(CNavArea* pArea);
	bool NavToSentrySpot();
	bool BuildBuilding(CUserCmd* pCmd, CTFPlayer* pLocal, ClosestEnemy_t tClosestEnemy, int building);
	bool SmackBuilding(CUserCmd* pCmd, CTFPlayer* pLocal, CBaseObject* pBuilding);
	bool EngineerLogic(CUserCmd* pCmd, CTFPlayer* pLocal, ClosestEnemy_t tClosestEnemy);

	std::optional<Vector> GetCtfGoal(CTFPlayer* pLocal, int iOurTeam, int iEnemyTeam);
	std::optional<Vector> GetPayloadGoal(const Vector vLocalOrigin, int iOurTeam);
	std::optional<Vector> GetControlPointGoal(const Vector vLocalOrigin, int iOurTeam);
	std::optional<Vector> GetDoomsdayGoal(CTFPlayer* pLocal, int iOurTeam, int iEnemyTeam);

	bool CaptureObjectives(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);
	bool Roam(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);
	bool MeleeAttack(CUserCmd* pCmd, CTFPlayer* pLocal, int iSlot, ClosestEnemy_t tClosestEnemy);

	void UpdateLocalBotPositions(CTFPlayer* pLocal);
	bool MoveInFormation(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);
	std::optional<Vector> GetFormationOffset(CTFPlayer* pLocal, int positionIndex);

	// Run away from dangerous areas
	bool EscapeDanger(CTFPlayer* pLocal);
	bool EscapeProjectiles(CTFPlayer* pLocal);
	bool EscapeSpawn(CTFPlayer* pLocal);

	int GetReloadWeaponSlot(CTFPlayer* pLocal, ClosestEnemy_t tClosestEnemy);
	void UpdateSlot(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, ClosestEnemy_t tClosestEnemy);
	void ClaimCaptureSpot(const Vector& vSpot, int iPointIdx);
	void ReleaseCaptureSpotClaim();
public:
	void Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void Reset();
	void Draw(CTFPlayer* pLocal);
};

ADD_FEATURE(CNavBot, NavBot);