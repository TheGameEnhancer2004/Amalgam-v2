#pragma once
#include "../../../SDK/SDK.h"

#include "../AimbotGlobal/AimbotGlobal.h"

class CAimbotMelee
{
	std::vector<Target_t> GetTargets(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);
	bool AimFriendlyBuilding(CBaseObject* pBuilding);
	std::vector<Target_t> SortTargets(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);

	int GetSwingTime(CTFWeaponBase* pWeapon);
	void SimulatePlayers(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, std::vector<Target_t> vTargets, Vec3& vEyePos,
						 std::unordered_map<int, std::deque<TickRecord>>& mRecordMap,
						 std::unordered_map<int, std::vector<Vec3>>& mPaths);
	bool CanBackstab(CBaseEntity* pTarget, CTFPlayer* pLocal, Vec3 vEyeAngles);
	int CanHit(Target_t& tTarget, CTFPlayer* pLocal, CTFWeaponBase* pWeapon, Vec3 vEyePos, std::deque<TickRecord>& vSimRecords);

	bool Aim(Vec3 vCurAngle, Vec3 vToAngle, Vec3& vOut, int iMethod = Vars::Aimbot::General::AimType.Value);
	void Aim(CUserCmd* pCmd, Vec3& vAngle);

	bool FindNearestBuildPoint(CBaseObject* pBuilding, CTFPlayer* pLocal, Vec3& vPoint);
	bool RunSapper(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);

	bool AimFriendlyBuilding(CTFPlayer* pLocal, CBaseObject* pBuilding);
	std::vector<Target_t> GetTargetBuilding(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);
	bool AutoEngie(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);

	int iDoubletapTicks = 0;
	int iDoubletapMax = 0;

public:
	void Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
};

ADD_FEATURE(CAimbotMelee, AimbotMelee);