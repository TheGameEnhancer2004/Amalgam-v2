#pragma once
#include "../../SDK/SDK.h"
#include "../../Utils/Timer/Timer.h"

struct ClosestEnemy_t
{
	int m_iEntIdx = -1;
	CTFPlayer* m_pPlayer = nullptr;
	float m_flDist = FLT_MAX;
};

Enum(ShouldTarget, Invalid = -1, DontTarget, Target);

class CBotUtils
{
private:
	std::unordered_map<int, bool> m_mAutoScopeCache;
	std::vector<ClosestEnemy_t> m_vCloseEnemies;
	ClosestEnemy_t UpdateCloseEnemies(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);

	bool HasMedigunTargets(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);
	void UpdateBestSlot(CTFPlayer* pLocal);

	struct legitLookState_t
	{
		Vec3 m_vAnchor = {};
		Vec3 m_vOffset = {};
		Vec3 m_vOffsetGoal = {};
		Vec3 m_vLastTarget = {};
		Vec3 m_vGlanceCurrent = {};
		Vec3 m_vGlanceGoal = {};
		int m_iLastTarget = -1;
		float m_flLastSeen = 0.f;
		Vec3 m_vLastPos = {};
		float m_flNextOffset = 1.f;
		float m_flPhase = 0.f;
		float m_flGlanceDuration = 0.f;
		float m_flNextGlance = 0.f;
		bool m_bInitialized = false;
		bool m_bGlancing = false;
		Timer m_tOffsetTimer = {};
		Timer m_tGlanceTimer = {};
		Timer m_tGlanceCooldown = {};
	};

	legitLookState_t m_tLLAP = {};
public:

	int m_iCurrentSlot = -1;
	int m_iBestSlot = -1;
	ClosestEnemy_t m_tClosestEnemy = {};
	Vec3 m_vLastAngles = {};

	bool ShouldAssist(CTFPlayer* pLocal, int iEntIdx);
	ShouldTargetEnum::ShouldTargetEnum ShouldTarget(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, int iEntIdx);
	ShouldTargetEnum::ShouldTargetEnum ShouldTargetBuilding(CTFPlayer* pLocal, int iEntIdx);

	bool GetDormantOrigin(int iIndex, Vector& vOut);

	void SetSlot(CTFPlayer* pLocal, int iSlot);

	void DoSlowAim(Vec3& vWishAngles, float flSpeed, Vec3 vPreviousAngles);
	void LookAtPath(CUserCmd* pCmd, Vec2 vDest, Vec3 vLocalEyePos, bool bSilent);
	void LookAtPath(CUserCmd* pCmd, Vec3 vWishAngles, Vec3 vLocalEyePos, bool bSilent, bool bSmooth = true);
	void LookLegit(CTFPlayer* pLocal, CUserCmd* pCmd, const Vec3& vDest, bool bSilent);
	void InvalidateLLAP();

	void AutoScope(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void Reset();
};

ADD_FEATURE(CBotUtils, BotUtils);