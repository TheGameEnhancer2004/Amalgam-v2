#pragma once
#include "Map.h"

Enum(PriorityList, None,
	 Patrol = 5,
	 LowPrioGetHealth,
	 StayNear,
	 RunReload, RunSafeReload,
	 SnipeSentry,
	 Capture,
	 GetAmmo,
	 MeleeAttack,
	 Engineer,
	 GetHealth,
	 EscapeSpawn, EscapeDanger,
	 Followbot
)

struct Crumb_t
{
	CNavArea* m_pNavArea = nullptr;
	Vector m_vPos = {};
	bool m_bRequiresDrop = false;
	float m_flDropHeight = 0.f;
	float m_flApproachDistance = 0.f;
	Vector m_vApproachDir = {};
};

class CNavEngine
{
private:
	std::unique_ptr<CMap> m_pMap;
	std::vector<Crumb_t> m_vCrumbs;
	CNavArea* m_pLocalArea = nullptr;

	Timer m_tTimeSpentOnCrumbTimer = {};
	Timer m_tInactivityTimer = {};

	bool m_bCurrentNavToLocal = false;
	bool m_bRepathOnFail = false;
	bool m_bPathing = false;

	bool IsSetupTime();

	// Use when something unexpected happens, e.g. vischeck fails
	void AbandonPath();
public:
	
	// Vischeck
	bool IsVectorVisibleNavigation(const Vector vFrom, const Vector vTo, unsigned int nMask = MASK_SHOT_HULL);
	// Checks if player can walk from one position to another without bumping into anything
	bool IsPlayerPassableNavigation(const Vector vFrom, Vector vTo, unsigned int nMask = MASK_PLAYERSOLID);

	// Are we currently pathing?
	bool IsPathing() { return !m_vCrumbs.empty(); }

	// Helper for external checks
	bool IsNavMeshLoaded() const { return m_pMap && m_pMap->m_eState == NavStateEnum::Active; }
	void UpdateRespawnRooms() const { if (m_pMap) m_pMap->UpdateRespawnRooms(); }

	CNavArea* FindClosestNavArea(const Vector vOrigin, bool bLocalOrigin = true) { return m_pMap->FindClosestNavArea(vOrigin, bLocalOrigin); }
	CNavFile* GetNavFile() { return &m_pMap->m_navfile; }

	// Get the path nodes
	std::vector<Crumb_t>* GetCrumbs() { return &m_vCrumbs; }

	// Get whole blacklist or with matching category
	std::unordered_map<CNavArea*, BlacklistReason_t>* GetFreeBlacklist() { return &m_pMap->m_mFreeBlacklist; }
	std::unordered_map<CNavArea*, BlacklistReason_t> GetFreeBlacklist(BlacklistReason_t tReason)
	{
		std::unordered_map<CNavArea*, BlacklistReason_t> mReturnMap;
		for (auto&[pNav, tBlacklist] : m_pMap->m_mFreeBlacklist)
		{
			// Category matches
			if (tBlacklist.m_eValue == tReason.m_eValue)
				mReturnMap[pNav] = tBlacklist;
		}
		return mReturnMap;
	}

	// Clear whole blacklist or with matching category
	void ClearFreeBlacklist() const { m_pMap->m_mFreeBlacklist.clear(); }
	void ClearFreeBlacklist(BlacklistReason_t tReason)
	{
		std::erase_if(m_pMap->m_mFreeBlacklist, [&tReason](const auto& entry)
					  {
						  return entry.second.m_eValue == tReason.m_eValue;
					  });
	}

	// Is the Nav engine ready to run?
	bool IsReady(bool bRoundCheck = false);
	bool IsBlacklistIrrelevant();

	// Use to cancel pathing completely
	void CancelPath();

	PriorityListEnum::PriorityListEnum m_eCurrentPriority = PriorityListEnum::None;
	Crumb_t m_tCurrentCrumb;
	Crumb_t m_tLastCrumb;
	Vector m_vLastDestination;

public:
	void FollowCrumbs(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void VischeckPath();
	void CheckBlacklist(CTFPlayer* pLocal);
	void UpdateStuckTime(CTFPlayer* pLocal);

	// Make sure to update m_pLocalArea with GetLocalNavArea before running
	bool NavTo(const Vector& vDestination, PriorityListEnum::PriorityListEnum ePriority = PriorityListEnum::Patrol, bool bShouldRepath = true, bool bNavToLocal = true, bool bIsRepath = true);

	float GetPathCost(const Vector& vLocalOrigin, const Vector& vDestination);
	CNavArea* GetLocalNavArea() const { return m_pLocalArea; }
	CNavArea* GetLocalNavArea(const Vector& vLocalOrigin);

	void Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void Reset(bool bForced = false);
	void Render();
};

ADD_FEATURE(CNavEngine, NavEngine);