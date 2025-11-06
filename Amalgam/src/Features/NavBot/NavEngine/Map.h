#pragma once
#include "FileReader/CNavFile.h"
#include "MicroPather/micropather.h"
#include <boost/container_hash/hash.hpp>

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

struct CachedConnection_t
{
	int m_iExpireTick;
	VischeckStateEnum::VischeckStateEnum m_eVischeckState;
};

struct CachedStucktime_t
{
	int m_iExpireTick;
	int m_iTimeStuck;
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
	micropather::MicroPather m_pather{ this, 3000, 6, true };

	std::unordered_map<std::pair<CNavArea*, CNavArea*>, CachedConnection_t, boost::hash<std::pair<CNavArea*, CNavArea*>>> m_mVischeckCache;
	std::unordered_map<std::pair<CNavArea*, CNavArea*>, CachedStucktime_t, boost::hash<std::pair<CNavArea*, CNavArea*>>> m_mConnectionStuckTime;

	// This is a pure blacklist that does not get cleared and is for free usage internally and externally, e.g. blacklisting where enemies are standing
	// This blacklist only gets cleared on map change, and can be used time independently.
	// the enum is the Blacklist reason, so you can easily edit it
	std::unordered_map<CNavArea*, BlacklistReason_t> m_mFreeBlacklist;

	// When the local player stands on one of the nav squares the free blacklist should NOT run
	bool m_bFreeBlacklistBlocked = false;

	float LeastCostEstimate(void* pStartArea, void* pEndArea) override { return reinterpret_cast<CNavArea*>(pStartArea)->m_vCenter.DistTo(reinterpret_cast<CNavArea*>(pEndArea)->m_vCenter); }
	void AdjacentCost(void* pArea, std::vector<micropather::StateCost>* pAdjacent) override;
	
	DropdownHint_t HandleDropdown(const Vector& vCurrentPos, const Vector& vNextPos);
	NavPoints_t DeterminePoints(CNavArea* pCurrentArea, CNavArea* pNextArea);

	// Get closest nav area to target vector
	CNavArea* FindClosestNavArea(const Vector& vPos, bool bLocalOrigin);

	std::vector<void*> FindPath(CNavArea* pLocalArea, CNavArea* pDestArea)
	{
		if (m_eState != NavStateEnum::Active)
			return {};

		float flCost;
		std::vector<void*> vPath;
		if (m_pather.Solve(reinterpret_cast<void*>(pLocalArea), reinterpret_cast<void*>(pDestArea), &vPath, &flCost) == micropather::MicroPather::START_END_SAME)
			return { reinterpret_cast<void*>(pLocalArea) };

		return vPath;
	}

	void UpdateIgnores(CTFPlayer* pLocal);

	void Reset()
	{
		m_mVischeckCache.clear();
		m_mConnectionStuckTime.clear();
		m_mFreeBlacklist.clear();
		m_pather.Reset();
	}

	// Unnecessary thing that is sadly necessary
	void PrintStateInfo(void*) {}
};