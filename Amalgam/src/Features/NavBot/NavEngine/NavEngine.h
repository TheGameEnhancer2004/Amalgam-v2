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

struct CachedCrumb_t
{
	Vector m_vPos = {};
	bool m_bRequiresDrop = false;
	float m_flDropHeight = 0.f;
	float m_flApproachDistance = 0.f;
	Vector m_vApproachDir = {};
};

struct RespawnRoom_t
{
	int m_iTeam = 0;
	TriggerData_t tData = {};
};

class CNavEngine
{
private:
	static constexpr int kCrumbCacheVersion = 1;
	static constexpr float kConnectionSegmentLength = 95.f;
	static constexpr int kMaxConnectionIntermediateCrumbs = 24;
	static constexpr float kMinAdaptiveSpacing = 72.f;
	static constexpr float kMaxAdaptiveSpacing = 150.f;

	std::unique_ptr<CMap> m_pMap;
	std::vector<Crumb_t> m_vCrumbs;
	std::vector<RespawnRoom_t> m_vRespawnRooms;
	std::vector<CNavArea*> m_vRespawnRoomExitAreas;
	CNavArea* m_pLocalArea;

	Timer m_tTimeSpentOnCrumbTimer = {};
	Timer m_tInactivityTimer = {};
	Timer m_tOffMeshTimer = {};
	Vector m_vOffMeshTarget = {};

	bool m_bCurrentNavToLocal = false;
	bool m_bRepathOnFail = false;
	bool m_bPathing = false;
	bool m_bUpdatedRespawnRooms = false;
	bool m_bRepathRequested = false;
	int m_iNextRepathTick = 0;
	int m_iLastBlacklistAbandonTick = 0;
	Vector m_vLastStrictFailDestination = {};
	int m_iStrictFailTick = 0;
	int m_iStrictFailCount = 0;
	std::unordered_map<uint64_t, std::vector<CachedCrumb_t>> m_mConnectionCrumbCache;
	bool m_bCrumbCacheReady = false;
	bool m_bCrumbCacheDirty = false;
	std::string m_sCrumbCachePath = {};
	std::array<float, 10> m_flRecentFallSpeeds = {};
	size_t m_iRecentFallSpeedIndex = 0;
	size_t m_nRecentFallSpeedCount = 0;

	void BuildIntraAreaCrumbs(const Vector& vStart, const Vector& vDestination, CNavArea* pArea);
	void BuildAdaptiveAreaCrumbs(const NavPoints_t& tPoints, const DropdownHint_t& tDrop, CNavArea* pArea, std::vector<CachedCrumb_t>& vOut) const;
	uint64_t MakeConnectionKey(uint32_t uFromId, uint32_t uToId) const;
	std::string BuildCrumbCachePath() const;
	bool LoadCrumbCache();
	bool SaveCrumbCache() const;
	void BuildCrumbCache();
	std::vector<CachedCrumb_t> BuildConnectionCacheEntry(CNavArea* pArea, CNavArea* pNextArea);
	const std::vector<CachedCrumb_t>* FindConnectionCacheEntry(CNavArea* pArea, CNavArea* pNextArea) const;
	void AppendCachedCrumbs(CNavArea* pArea, const std::vector<CachedCrumb_t>& vCachedCrumbs);
	void ConsumeFrontCrumbs(size_t nCount);

	// Use when something unexpected happens, e.g. vischeck fails
	void AbandonPath(const std::string& sReason);
	void UpdateRespawnRooms();
	// void CheckPathValidity(CTFPlayer* pLocal);
public:
	std::string m_sLastFailureReason = "";
	bool m_bIgnoreTraces = false;
	std::vector<std::pair<Vector, Vector>> m_vPossiblePaths = {};
	std::vector<std::pair<Vector, Vector>> m_vDebugWalkablePaths = {};
	std::vector<std::pair<Vector, Vector>> m_vRejectedPaths = {};
	bool IsSetupTime();

	// Vischeck
	bool IsVectorVisibleNavigation(const Vector vFrom, const Vector vTo, unsigned int nMask = MASK_SHOT);
	// Checks if player can walk from one position to another without bumping into anything
	bool IsPlayerPassableNavigation(CTFPlayer* pLocal, const Vector vFrom, Vector vTo, unsigned int nMask = MASK_PLAYERSOLID);

	// Are we currently pathing?
	bool IsPathing() { return !m_vCrumbs.empty(); }

	// Helper for external checks
	bool IsNavMeshLoaded() const { return m_pMap && m_pMap->m_eState == NavStateEnum::Active; }
	std::string GetNavFilePath() const { return m_pMap ? m_pMap->m_sMapName : ""; }
	bool HasRespawnRooms() const { return !m_vRespawnRooms.empty(); }

	void ClearRespawnRooms() { m_vRespawnRooms.clear(); }
	void AddRespawnRoom(int iTeam, TriggerData_t tTrigger) { m_vRespawnRooms.emplace_back(iTeam, tTrigger); }

	std::vector<CNavArea*>* GetRespawnRoomExitAreas() { return &m_vRespawnRoomExitAreas; }

	CNavArea* FindClosestNavArea(const Vector vOrigin, bool bLocalOrigin = true) { return m_pMap->FindClosestNavArea(vOrigin, bLocalOrigin); }
	CNavArea* GetLocalNavArea() const { return m_pLocalArea; }
	CNavFile* GetNavFile() { return &m_pMap->m_navfile; }
	CMap* GetNavMap() { return m_pMap.get(); }
	CNavArea* GetLocalNavArea() { return m_pLocalArea; }

	// Get the path nodes
	std::vector<Crumb_t>* GetCrumbs() { return &m_vCrumbs; }

	// Get whole blacklist or with matching category
	std::unordered_map<CNavArea*, BlacklistReason_t>* GetFreeBlacklist() { return &m_pMap->m_mFreeBlacklist; }
	std::unordered_map<CNavArea*, BlacklistReason_t> GetFreeBlacklist(BlacklistReason_t tReason)
	{
		std::unordered_map<CNavArea*, BlacklistReason_t> mReturnMap;
		for (auto& [pNav, tBlacklist] : m_pMap->m_mFreeBlacklist)
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
	Vector m_vCurrentPathDir;
	Vector m_vLastDestination;
	Vector m_vLastLookTarget;

public:
	void FollowCrumbs(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void VischeckPath();
	void CheckBlacklist(CTFPlayer* pLocal);
	void UpdateStuckTime(CTFPlayer* pLocal, CUserCmd* pCmd);

	// Make sure to update m_pLocalArea with GetLocalNavArea before running
	bool NavTo(const Vector& vDestination, PriorityListEnum::PriorityListEnum ePriority = PriorityListEnum::Patrol, bool bShouldRepath = true, bool bNavToLocal = true, bool bIgnoreTraces = false);

	float GetPathCost(const Vector& vLocalOrigin, const Vector& vDestination);

	CNavArea* GetLocalNavArea(const Vector& vLocalOrigin);
	const Vector& GetCurrentPathDir() const { return m_vCurrentPathDir; }

	void Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void Reset(bool bForced = false);
	void FlushCrumbCache();
	void Render();
};

ADD_FEATURE(CNavEngine, NavEngine);
