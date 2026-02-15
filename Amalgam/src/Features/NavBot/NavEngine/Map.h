#pragma once
#include "FileReader/CNavFile.h"
#include "MicroPather/micropather.h"
#include <boost/container_hash/hash.hpp>
#include <limits>
#include <queue>
#include <unordered_set>
#include <mutex>

#define PLAYER_WIDTH		49.0f
#define HALF_PLAYER_WIDTH	PLAYER_WIDTH / 2.0f
#define PLAYER_HEIGHT		83.0f
#define PLAYER_CROUCHED_JUMP_HEIGHT	72.0f
#define PLAYER_JUMP_HEIGHT	50.0f
#define TICKCOUNT_TIMESTAMP(seconds) (I::GlobalVars->tickcount + static_cast<int>((seconds) / I::GlobalVars->interval_per_tick))

Enum(NavState, Unavailable, Active)
Enum(VischeckState, NotVisible = -1, NotChecked, Visible)

// Basic Blacklist reasons, you can add your own externally and use them
Enum(BlacklistReason, Init = -1,
	Sentry, SentryMedium, SentryLow,
	Sticky,
	EnemyNormal, EnemyDormant, EnemyInvuln,
	BadBuildSpot
)

struct BlacklistReason_t
{
	BlacklistReasonEnum::BlacklistReasonEnum m_eValue;
	int m_iTime = 0;
	void operator=(BlacklistReasonEnum::BlacklistReasonEnum const& eReason)
	{
		m_eValue = eReason;
	}

	BlacklistReason_t()
	{
		m_eValue = BlacklistReasonEnum::Init;
		m_iTime = 0;
	}

	explicit BlacklistReason_t(BlacklistReasonEnum::BlacklistReasonEnum eReason)
	{
		m_eValue = eReason;
		m_iTime = 0;
	}

	BlacklistReason_t(BlacklistReasonEnum::BlacklistReasonEnum eReason, int iTime)
	{
		m_eValue = eReason;
		m_iTime = iTime;
	}
};

struct PathNode_t
{
	float m_g = std::numeric_limits<float>::max();
	float m_f = std::numeric_limits<float>::max(); // g + h
	CNavArea* m_pParent = nullptr;
	uint32_t m_iQueryId = 0;
	bool m_bInOpen = false;
};

struct NavPoints_t
{
	Vector m_vCurrent;
	Vector m_vCenter;

	// The above but on the "m_vNext" vector, used for height checks.
	Vector m_vCenterNext;
	Vector m_vNext;
};

struct DropdownHint_t
{
	Vector m_vAdjustedPos = {};
	bool m_bRequiresDrop = false;
	float m_flDropHeight = 0.f;
	float m_flApproachDistance = 0.f;
	Vector m_vApproachDir = {};
};

struct CachedConnection_t
{
	int m_iExpireTick = 0;
	VischeckStateEnum::VischeckStateEnum m_eVischeckState = VischeckStateEnum::NotChecked;
	float m_flCachedCost = std::numeric_limits<float>::max();
	DropdownHint_t m_tDropdown = {};
	NavPoints_t m_tPoints = {};
	bool m_bPassable = false;
	bool m_bStuckBlacklist = false;
};

struct CachedStucktime_t
{
	int m_iExpireTick;
	int m_iTimeStuck;
};

class CMap : public micropather::Graph
{
public:
	CNavFile m_navfile;
	std::string m_sMapName;
	NavStateEnum::NavStateEnum m_eState;
	CMap(const char* sMapName)
	{
		m_navfile = CNavFile(sMapName);
		m_sMapName = sMapName;
		m_eState = m_navfile.m_bOK ? NavStateEnum::Active : NavStateEnum::Unavailable;
	}
	// micropather::MicroPather m_pather{ this, 3000, 6, true };
	std::recursive_mutex m_mutex;

	std::unordered_map<std::pair<CNavArea*, CNavArea*>, CachedConnection_t, boost::hash<std::pair<CNavArea*, CNavArea*>>> m_mVischeckCache;
	std::unordered_map<std::pair<CNavArea*, CNavArea*>, CachedStucktime_t, boost::hash<std::pair<CNavArea*, CNavArea*>>> m_mConnectionStuckTime;

	// This is a pure blacklist that does not get cleared and is for free usage internally and externally, e.g. blacklisting where enemies are standing
	// This blacklist only gets cleared on map change, and can be used time independently.
	// the enum is the Blacklist reason, so you can easily edit it
	std::unordered_map<CNavArea*, BlacklistReason_t> m_mFreeBlacklist;

	// When the local player stands on one of the nav squares the free blacklist should NOT run
	bool m_bFreeBlacklistBlocked = false;
	bool m_bIgnoreSentryBlacklist = false;

	std::vector<PathNode_t> m_vPathNodes;
	uint32_t m_iQueryId = 0;
	
	int Solve(CNavArea* pStart, CNavArea* pEnd, std::vector<void*>* path, float* cost);
	
	// legacy
	float LeastCostEstimate(void* pStartArea, void* pEndArea) override { return 0.0f; } 
	void AdjacentCost(void* pArea, std::vector<micropather::StateCost>* pAdjacent) override;
	void GetDirectNeighbors(CNavArea* pArea, std::vector<micropather::StateCost>& neighbors);

	DropdownHint_t HandleDropdown(const Vector& vCurrentPos, const Vector& vNextPos, bool bIsOneWay);
	NavPoints_t DeterminePoints(CNavArea* pCurrentArea, CNavArea* pNextArea, bool bIsOneWay);
	bool IsOneWay(CNavArea* pFrom, CNavArea* pTo) const;
	float GetBlacklistPenalty(const BlacklistReason_t& tReason) const;
	void CollectAreasAround(const Vector& vOrigin, float flRadius, std::vector<CNavArea*>& vOutAreas);

private:
	float EvaluateConnectionCost(CNavArea* pCurrentArea, CNavArea* pNextArea, const NavPoints_t& tPoints, const DropdownHint_t& tDropdown, int iTeam) const;
	bool ShouldOverrideBlacklist(const BlacklistReason_t& tCurrent, const BlacklistReason_t& tIncoming) const;
	void ApplyBlacklistAround(const Vector& vOrigin, float flRadius, const BlacklistReason_t& tReason, unsigned int nMask, bool bRequireLOS);

public:

	// Get closest nav area to target vector
	CNavArea* FindClosestNavArea(const Vector& vPos, bool bLocalOrigin);

	bool IsAreaValid(CNavArea* pArea) const
	{
		if (!pArea || m_navfile.m_vAreas.empty())
			return false;

		const CNavArea* pBegin = &m_navfile.m_vAreas.front();
		const CNavArea* pEnd = &m_navfile.m_vAreas.back();

		return pArea >= pBegin && pArea <= pEnd;
	}

	std::vector<void*> FindPath(CNavArea* pLocalArea, CNavArea* pDestArea, int* pOutResult = nullptr)
	{
		if (m_eState != NavStateEnum::Active)
			return {};

		std::lock_guard lock(m_mutex);
		float flCost;
		std::vector<void*> vPath;
		int iResult = Solve(pLocalArea, pDestArea, &vPath, &flCost);
		if (pOutResult)
			*pOutResult = iResult;

		if (iResult == micropather::MicroPather::START_END_SAME)
			return { reinterpret_cast<void*>(pLocalArea) };

		return vPath;
	}

	void UpdateIgnores(CTFPlayer* pLocal);

	void Reset()
	{
		std::lock_guard lock(m_mutex);
		m_mVischeckCache.clear();
		m_mConnectionStuckTime.clear();
		m_mFreeBlacklist.clear();
		// m_pather.Reset();
	}

	// Unnecessary thing that is sadly necessary
	void PrintStateInfo(void*) {}
};