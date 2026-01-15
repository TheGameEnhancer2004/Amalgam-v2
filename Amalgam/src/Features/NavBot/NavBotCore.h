#pragma once
#include "BotUtils.h"

class CNavArea;
class CNavBotCore
{
public:
	// Controls the bot parameters like distance from enemy
	struct BotClassConfig_t
	{
		float m_flMinFullDanger;
		float m_flMinSlightDanger;
		float m_flMax;
		bool m_bPreferFar;
	};

	const BotClassConfig_t CONFIG_SHORT_RANGE = { 140.0f, 400.0f, 600.0f, false };
	const BotClassConfig_t CONFIG_MID_RANGE = { 200.0f, 500.0f, 3000.0f, true };
	const BotClassConfig_t CONFIG_LONG_RANGE = { 300.0f, 500.0f, 4000.0f, true };
	const BotClassConfig_t CONFIG_ENGINEER = { 200.0f, 500.0f, 3000.0f, false };
	const BotClassConfig_t CONFIG_GUNSLINGER_ENGINEER = { 50.0f, 300.0f, 2000.0f, false };
	BotClassConfig_t m_tSelectedConfig;
private:
	void UpdateEnemyBlacklist(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, int iSlot);
	void UpdateSlot(CTFPlayer* pLocal, ClosestEnemy_t tClosestEnemy);
public:
	// Recursive function to find hiding spot
	bool FindClosestHidingSpot(CNavArea* pArea, Vector vVischeckPoint, int iRecursionCount, std::pair<CNavArea*, int>& tOut, bool bVischeck = true, int iRecursionIndex = 0);

	void Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void Reset();
	void Draw(CTFPlayer* pLocal);

private:
	Timer m_tIdleTimer = {};
	Timer m_tAntiStuckTimer = {};
	float m_flNextStuckAngleChange = 0.f;
	float m_flNextIdleTime = 0.f;
	Vec3 m_vStuckAngles = {};
};

ADD_FEATURE(CNavBotCore, NavBotCore);