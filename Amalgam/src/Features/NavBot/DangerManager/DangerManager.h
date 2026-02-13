#pragma once
#include "../../../SDK/SDK.h"
#include "../NavEngine/Map.h"
#include <unordered_map>

inline constexpr float DANGER_SCORE_SENTRY = 3500.f;
inline constexpr float DANGER_SCORE_ENEMY_INVULN = 1500.f;
inline constexpr float DANGER_SCORE_STICKY = 1000.f;
inline constexpr float DANGER_SCORE_ENEMY_NORMAL = 300.f;
inline constexpr float DANGER_SCORE_ENEMY_DORMANT = 200.f;
inline constexpr float DANGER_SCORE_AVOID = 100.f;

enum class DangerType_t
{
	None,
	Enemy,
	Sentry,
	Projectile,
	Trap,
	Static
};

struct DangerData_t
{
	float m_flScore = 0.0f;
	int m_iLastUpdateTick = 0;
	Vector m_vOrigin = {};
	DangerType_t m_eType = DangerType_t::None;
	BlacklistReason_t m_LegacyReason = {}; // legacy again
};

class CDangerManager
{
private:
	std::unordered_map<CNavArea*, DangerData_t> m_mDangerMap;
	int m_iLastUpdateTick = 0;

	float m_flMinFullDanger = 0.f;
	float m_flMinSlightDanger = 0.f;

	void UpdatePlayers(CTFPlayer* pLocal);
	void UpdateBuildings(CTFPlayer* pLocal);
	void UpdateProjectiles(CTFPlayer* pLocal);
	void UpdateStatic(CTFPlayer* pLocal);

public:
	void Update(CTFPlayer* pLocal);
	void Reset();
	float GetDanger(CNavArea* pArea);
	float GetCost(CNavArea* pArea);
	
	// legacy
	bool IsBlacklisted(CNavArea* pArea);
	BlacklistReason_t GetBlacklistReason(CNavArea* pArea);

	void Render();
	const std::unordered_map<CNavArea*, DangerData_t>& GetDangerMap() const { return m_mDangerMap; }
};

ADD_FEATURE(CDangerManager, DangerManager);
