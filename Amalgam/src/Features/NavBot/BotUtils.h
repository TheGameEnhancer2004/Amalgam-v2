#pragma once
#include "NavEngine/NavEngine.h"

struct ClosestEnemy_t
{	
	int m_iEntIdx = -1;
	CTFPlayer* m_pPlayer = nullptr;
	float m_flDist = -1.f;
};

enum EShouldTargetState
{
	INVALID = -1,
	DONT_TARGET,
	TARGET
};

class CBotUtils
{
private:
	std::unordered_map<int, bool> m_mAutoScopeCache;
	std::vector<ClosestEnemy_t> m_vCloseEnemies;
	bool HasMedigunTargets(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);
public:
	int m_iCurrentSlot = -1;
	int m_iBestSlot = -1;
	ClosestEnemy_t m_tClosestEnemy = {};
	Vec3 m_vLastAngles = {};

	EShouldTargetState ShouldTarget(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, int iPlayerIdx);
	EShouldTargetState ShouldTargetBuilding(CTFPlayer* pLocal, int iEntIdx);

	ClosestEnemy_t UpdateCloseEnemies(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);
	void UpdateBestSlot(CTFPlayer* pLocal);
	void SetSlot(CTFPlayer* pLocal, int iSlot);

	void DoSlowAim(Vec3& vWishAngles, float flSpeed , Vec3 vPreviousAngles);
	void LookAtPath(CUserCmd* pCmd, Vec2 vDest, Vec3 vLocalEyePos, bool bSilent);
	void LookAtPath(CUserCmd* pCmd, Vec3 vWishAngles, Vec3 vLocalEyePos, bool bSilent, bool bSmooth = true);

	void AutoScope(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void Reset();
};

ADD_FEATURE(CBotUtils, BotUtils);