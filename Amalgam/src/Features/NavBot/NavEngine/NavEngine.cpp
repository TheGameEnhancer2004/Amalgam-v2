#include "NavEngine.h"
#include "../NavBotJobs/Engineer.h"
#include "../../Ticks/Ticks.h"
#include "../../Misc/Misc.h"
#include "../BotUtils.h"
#include "../../FollowBot/FollowBot.h"
#include <limits>
#include <algorithm>
#include <cmath>

bool CNavEngine::IsSetupTime()
{
	static Timer tCheckTimer{};
	static bool bSetupTime = false;
	if (Vars::Misc::Movement::NavEngine::PathInSetup.Value)
		return false;

	auto pLocal = H::Entities.GetLocal();
	if (pLocal && pLocal->IsAlive())
	{
		std::string sLevelName = SDK::GetLevelName();

		// No need to check the round states that quickly.
		if (tCheckTimer.Run(0.5f))
		{
			// Special case for Pipeline which doesn't use standard setup time
			if (sLevelName == "plr_pipeline")
				return false;

			if (auto pGameRules = I::TFGameRules())
			{
				// The round just started, players cant move.
				if (pGameRules->m_iRoundState() == GR_STATE_PREROUND)
					return bSetupTime = true;

				if (pLocal->m_iTeamNum() == TF_TEAM_BLUE)
				{
					if (pGameRules->m_bInSetup() || (pGameRules->m_bInWaitingForPlayers() && (sLevelName.starts_with("pl_") || sLevelName.starts_with("cp_"))))
						return bSetupTime = true;
				}
				bSetupTime = false;
			}
		}
	}
	return bSetupTime;
}

bool CNavEngine::IsVectorVisibleNavigation(const Vector vFrom, const Vector vTo, unsigned int nMask)
{
	CGameTrace trace = {};
	CTraceFilterNavigation filter;
	SDK::Trace(vFrom, vTo, nMask, &filter, &trace);
	return trace.fraction == 1.0f;
}

bool CNavEngine::IsPlayerPassableNavigation(CTFPlayer* pLocal, const Vector vFrom, Vector vTo, unsigned int nMask)
{
	return F::BotUtils.IsWalkable(pLocal, vFrom, vTo);
}

void CNavEngine::BuildIntraAreaCrumbs(const Vector& vStart, const Vector& vDestination, CNavArea* pArea)
{
	if (!pArea)
		return;

	Vector vDelta = vDestination - vStart;
	Vector vPlanar = vDelta;
	vPlanar.z = 0.f;
	const float flPlanarDistance = vPlanar.Length();
	const float flVerticalDistance = std::fabs(vDelta.z);
	const float flEffectiveDistance = std::max(flPlanarDistance, flVerticalDistance);

	if (flEffectiveDistance <= 1.f)
		return;

	constexpr float kMaxSegmentLength = 120.f;
	const int nIntermediate = std::clamp(static_cast<int>(std::ceil(flEffectiveDistance / kMaxSegmentLength)), 1, 8);
	const float flDivider = static_cast<float>(nIntermediate + 1);
	const Vector vStep = vDelta / flDivider;

	Vector vApproachDir = vPlanar;
	const float flApproachLen = vApproachDir.Length();
	if (flApproachLen > 0.01f)
		vApproachDir /= flApproachLen;
	else
		vApproachDir = {};

	for (int i = 1; i <= nIntermediate; ++i)
	{
		Crumb_t tCrumb{};
		tCrumb.m_pNavArea = pArea;
		tCrumb.m_vPos = vStart + vStep * static_cast<float>(i);
		tCrumb.m_vApproachDir = vApproachDir;
		m_vCrumbs.push_back(tCrumb);
	}
}

bool CNavEngine::NavTo(const Vector& vDestination, PriorityListEnum::PriorityListEnum ePriority, bool bShouldRepath, bool bNavToLocal, bool bIgnoreTraces)
{
	if (!m_pMap)
		return false;

	auto pLocalPlayer = H::Entities.GetLocal();
	if (!pLocalPlayer)
		return false;

	m_vLastDestination = vDestination;
	m_bCurrentNavToLocal = bNavToLocal;
	m_bRepathOnFail = bShouldRepath;
	m_bIgnoreTraces = bIgnoreTraces;

	m_sLastFailureReason = "";
	if (F::Ticks.m_bWarp || F::Ticks.m_bDoubletap)
	{
		m_sLastFailureReason = "Warping/Doubletapping";
		return false;
	}

	if (!IsReady())
	{
		m_sLastFailureReason = "Not ready";
		return false;
	}

	// Don't path, priority is too low
	if (ePriority < m_eCurrentPriority)
	{
		m_sLastFailureReason = "Priority too low";
		return false;
	}

	if (!GetLocalNavArea())
	{
		m_sLastFailureReason = "No local nav area";
		return false;
	}

	CNavArea* pDestArea = FindClosestNavArea(vDestination, false);
	if (!pDestArea)
	{
		m_sLastFailureReason = "No destination nav area";
		return false;
	}

	int iPathResult = -1;
	auto vPath = m_pMap->FindPath(m_pLocalArea, pDestArea, &iPathResult);
	bool bSingleAreaPath = false;
	if (vPath.empty())
	{
		if (m_pLocalArea == pDestArea)
			bSingleAreaPath = true;
		else
		{
			switch (iPathResult)
			{
			case micropather::MicroPather::NO_SOLUTION:
			{
				if (!bIgnoreTraces)
				{
					m_sLastFailureReason = "No solution found, attempting fallback";
					m_pMap->m_pather.Reset();
					return NavTo(vDestination, ePriority, bShouldRepath, bNavToLocal, true);
				}

				m_sLastFailureReason = "No solution found (disconnected)";
				if (m_pLocalArea && pDestArea)
				{
					// Check if any connections from local are even possible
					bool bAnyPossible = false;
					bool bAnyExits = false;
					for (auto& tConnect : m_pLocalArea->m_vConnections)
					{
						if (!tConnect.m_pArea) continue;
						bAnyExits = true;

						bool bIsOneWay = m_pMap->IsOneWay(m_pLocalArea, tConnect.m_pArea);
						NavPoints_t tPoints = m_pMap->DeterminePoints(m_pLocalArea, tConnect.m_pArea, bIsOneWay);
						DropdownHint_t tDropdown = m_pMap->HandleDropdown(tPoints.m_vCenter, tPoints.m_vNext, bIsOneWay);
						tPoints.m_vCenter = tDropdown.m_vAdjustedPos;

						if (IsPlayerPassableNavigation(pLocalPlayer, tPoints.m_vCurrent, tPoints.m_vCenter) &&
							IsPlayerPassableNavigation(pLocalPlayer, tPoints.m_vCenter, tPoints.m_vNext))
						{
							bAnyPossible = true;
							break;
						}
					}

					if (!bAnyExits) m_sLastFailureReason += " - Local area has no exits";
					else if (!bAnyPossible) m_sLastFailureReason += " - All local exits blocked by traces";
				}
				break;
			}
			case micropather::MicroPather::START_END_SAME: m_sLastFailureReason = "Start and end are same"; break;
			default: m_sLastFailureReason = "Pathing engine error"; break;
			}
			return false;
		}
	}

	if (!bSingleAreaPath && !bNavToLocal && !vPath.empty())
	{
		vPath.erase(vPath.begin());
		if (vPath.empty())
		{
			if (m_pLocalArea == pDestArea)
				bSingleAreaPath = true;
			else
			{
				m_sLastFailureReason = "Path empty after trim";
				return false;
			}
		}
	}

	m_vCrumbs.clear();
	if (bSingleAreaPath)
	{
		Vector vStart = m_pLocalArea ? m_pLocalArea->m_vCenter : vDestination;
		if (auto pLocalPlayer = H::Entities.GetLocal(); pLocalPlayer && pLocalPlayer->IsAlive())
			vStart = pLocalPlayer->GetAbsOrigin();

		BuildIntraAreaCrumbs(vStart, vDestination, m_pLocalArea);
	}
	else
	{
		for (size_t i = 0; i < vPath.size(); i++)
		{
			auto pArea = reinterpret_cast<CNavArea*>(vPath.at(i));
			if (!pArea)
				continue;

			// All entries besides the last need an extra crumb
			if (i != vPath.size() - 1)
			{
				auto pNextArea = reinterpret_cast<CNavArea*>(vPath.at(i + 1));

				NavPoints_t tPoints{};
				DropdownHint_t tDropdown{};
				const std::pair<CNavArea*, CNavArea*> tKey(pArea, pNextArea);
				if (auto itCache = m_pMap->m_mVischeckCache.find(tKey); itCache != m_pMap->m_mVischeckCache.end() && itCache->second.m_bPassable)
				{
					tPoints = itCache->second.m_tPoints;
					tDropdown = itCache->second.m_tDropdown;
				}
				else
				{
					bool bIsOneWay = m_pMap->IsOneWay(pArea, pNextArea);
					tPoints = m_pMap->DeterminePoints(pArea, pNextArea, bIsOneWay);
					tDropdown = m_pMap->HandleDropdown(tPoints.m_vCenter, tPoints.m_vNext, bIsOneWay);
					tPoints.m_vCenter = tDropdown.m_vAdjustedPos;
				}

				Crumb_t tStartCrumb = {};
				tStartCrumb.m_pNavArea = pArea;
				tStartCrumb.m_vPos = tPoints.m_vCurrent;
				m_vCrumbs.push_back(tStartCrumb);

				Crumb_t tMidCrumb = {};
				tMidCrumb.m_pNavArea = pArea;
				tMidCrumb.m_vPos = tPoints.m_vCenter;
				tMidCrumb.m_bRequiresDrop = tDropdown.m_bRequiresDrop;
				tMidCrumb.m_flDropHeight = tDropdown.m_flDropHeight;
				tMidCrumb.m_flApproachDistance = tDropdown.m_flApproachDistance;
				tMidCrumb.m_vApproachDir = tDropdown.m_vApproachDir;
				m_vCrumbs.push_back(tMidCrumb);
			}
			else
			{
				Crumb_t tEndCrumb = {};
				tEndCrumb.m_pNavArea = pArea;
				tEndCrumb.m_vPos = vDestination;
				m_vCrumbs.push_back(tEndCrumb);
			}
		}
	}

	if (!bIgnoreTraces && !m_vCrumbs.empty())
	{
		// Check if the path we just built is even valid with traces
		// If not, we might want to try again with traces ignored if absolutely necessary
		bool bValid = true;
		const auto iVischeckCacheExpireTimestamp = TICKCOUNT_TIMESTAMP(Vars::Misc::Movement::NavEngine::VischeckCacheTime.Value);

		for (size_t i = 0; i < m_vCrumbs.size() - 1; i++)
		{
			const auto& tCrumb = m_vCrumbs[i];
			const auto& tNextCrumb = m_vCrumbs[i + 1];
			const std::pair<CNavArea*, CNavArea*> tKey(tCrumb.m_pNavArea, tNextCrumb.m_pNavArea);

			// Check if we have a valid cache entry
			if (m_pMap->m_mVischeckCache.count(tKey))
			{
				auto& tEntry = m_pMap->m_mVischeckCache[tKey];
				if (tEntry.m_iExpireTick > I::GlobalVars->tickcount)
				{
					if (!tEntry.m_bPassable)
					{
						bValid = false;
						break;
					}
					continue;
				}
			}

			if (!IsPlayerPassableNavigation(pLocalPlayer, tCrumb.m_vPos, tNextCrumb.m_vPos))
			{
				// Cache failure immediately
				CachedConnection_t tEntry{};
				tEntry.m_iExpireTick = iVischeckCacheExpireTimestamp;
				tEntry.m_eVischeckState = VischeckStateEnum::NotVisible;
				tEntry.m_bPassable = false;
				tEntry.m_flCachedCost = std::numeric_limits<float>::max();
				m_pMap->m_mVischeckCache[tKey] = tEntry;

				bValid = false;
				break;
			}
			else
			{
				// Cache success
				CachedConnection_t& tEntry = m_pMap->m_mVischeckCache[tKey];
				tEntry.m_iExpireTick = iVischeckCacheExpireTimestamp;
				tEntry.m_eVischeckState = VischeckStateEnum::Visible;
				tEntry.m_bPassable = true;
			}
		}

		if (!bValid)
		{
			// If the normal path is blocked, try to find a path ignoring costs and tracelines
			// this shit is absolute fallback if everything fails
			m_sLastFailureReason = "Path blocked by traces, attempting fallback";
			m_vCrumbs.clear();
			return NavTo(vDestination, ePriority, bShouldRepath, bNavToLocal, true);
		}
	}

	m_eCurrentPriority = ePriority;
	return true;
}

float CNavEngine::GetPathCost(const Vector& vLocalOrigin, const Vector& vDestination)
{
	if (!IsNavMeshLoaded())
		return FLT_MAX;

	if (!GetLocalNavArea(vLocalOrigin))
		return FLT_MAX;

	auto pDestArea = FindClosestNavArea(vDestination, false);
	if (!pDestArea)
		return FLT_MAX;

	float flCost;
	std::vector<void*> vPath;
	if (m_pMap->m_pather.Solve(reinterpret_cast<void*>(m_pLocalArea), reinterpret_cast<void*>(pDestArea), &vPath, &flCost) == micropather::MicroPather::START_END_SAME)
		return 0.f;

	return flCost;
}

CNavArea* CNavEngine::GetLocalNavArea(const Vector& pLocalOrigin)
{
	// Update local area only if our origin is no longer in its minmaxs
	if (!m_pLocalArea || (!m_pLocalArea->IsOverlapping(pLocalOrigin) || pLocalOrigin.z < m_pLocalArea->m_flMinZ))
		m_pLocalArea = FindClosestNavArea(pLocalOrigin);
	return m_pLocalArea;
}

void CNavEngine::VischeckPath()
{
	static Timer tVischeckTimer{};
	// No crumbs to check, or vischeck timer should not run yet, bail.
	if (m_vCrumbs.size() < 2 || !tVischeckTimer.Run(Vars::Misc::Movement::NavEngine::VischeckTime.Value))
		return;

	if (m_bIgnoreTraces)
		return;

	auto pLocal = H::Entities.GetLocal();
	if (!pLocal)
		return;

	const auto iVischeckCacheExpireTimestamp = TICKCOUNT_TIMESTAMP(Vars::Misc::Movement::NavEngine::VischeckCacheTime.Value);

	// Iterate all the crumbs
	for (auto it = m_vCrumbs.begin(), next = it + 1; next != m_vCrumbs.end(); it++, next++)
	{
		auto tCrumb = *it;
		auto tNextCrumb = *next;
		auto tKey = std::pair<CNavArea*, CNavArea*>(tCrumb.m_pNavArea, tNextCrumb.m_pNavArea);

		auto vCurrentCenter = tCrumb.m_vPos;
		auto vNextCenter = tNextCrumb.m_vPos;

		// Check if we have a valid cache entry
		if (m_pMap->m_mVischeckCache.count(tKey))
		{
			auto& tEntry = m_pMap->m_mVischeckCache[tKey];
			if (tEntry.m_iExpireTick > I::GlobalVars->tickcount)
			{
				if (!tEntry.m_bPassable)
				{
					AbandonPath("Traceline blocked (cached)");
					break;
				}
				continue;
			}
		}

		// Check if we can pass, if not, abort pathing and mark as bad
		if (!IsPlayerPassableNavigation(pLocal, vCurrentCenter, vNextCenter))
		{
			// Mark as invalid for a while
			CachedConnection_t tEntry{};
			tEntry.m_iExpireTick = iVischeckCacheExpireTimestamp;
			tEntry.m_eVischeckState = VischeckStateEnum::NotVisible;
			tEntry.m_bPassable = false;
			tEntry.m_flCachedCost = std::numeric_limits<float>::max();
			m_pMap->m_mVischeckCache[tKey] = tEntry;
			AbandonPath("Traceline blocked");
			break;
		}
		// Else we can update the cache (if not marked bad before this)
		else
		{
			CachedConnection_t& tEntry = m_pMap->m_mVischeckCache[tKey];
			tEntry.m_iExpireTick = iVischeckCacheExpireTimestamp;
			tEntry.m_eVischeckState = VischeckStateEnum::Visible;
			tEntry.m_bPassable = true;
		}
	}
}

// Check if one of the crumbs is suddenly blacklisted
void CNavEngine::CheckBlacklist(CTFPlayer* pLocal)
{
	static Timer tBlacklistCheckTimer{};
	// Only check every 500ms
	if (!tBlacklistCheckTimer.Run(0.5f) || m_bIgnoreTraces)
		return;

	// Local player is ubered and does not care about the blacklist
	// TODO: Only for damage type things
	if (pLocal->IsInvulnerable())
	{
		m_pMap->m_bFreeBlacklistBlocked = true;
		m_pMap->m_pather.Reset();
		return;
	}

	for (auto& [pArea, _] : m_pMap->m_mFreeBlacklist)
	{
		// Local player is in a blocked area, so temporarily remove the blacklist as else we would be stuck
		if (pArea == m_pLocalArea)
		{
			m_pMap->m_bFreeBlacklistBlocked = true;
			m_pMap->m_pather.Reset();
			return;
		}
	}

	// Local player is not blocking the nav area, so blacklist should not be marked as blocked
	m_pMap->m_bFreeBlacklistBlocked = false;

	// Ignore sentry blacklist if we are trying to snipe one
	m_pMap->m_bIgnoreSentryBlacklist = m_eCurrentPriority == PriorityListEnum::SnipeSentry;

	for (auto& tCrumb : m_vCrumbs)
	{
		auto itBlacklist = m_pMap->m_mFreeBlacklist.find(tCrumb.m_pNavArea);
		if (itBlacklist != m_pMap->m_mFreeBlacklist.end())
		{
			float flPenalty = m_pMap->GetBlacklistPenalty(itBlacklist->second);
			if (flPenalty >= 2500.f) // Only abandon for extreme danger, otherwise let cost-based pathing handle it
			{
				AbandonPath("Blacklisted area");
				return;
			}
		}

		if (tCrumb.m_pNavArea)
		{
			auto tAreaKey = std::pair<CNavArea*, CNavArea*>(tCrumb.m_pNavArea, tCrumb.m_pNavArea);
			auto itVischeck = m_pMap->m_mVischeckCache.find(tAreaKey);
			if (itVischeck != m_pMap->m_mVischeckCache.end() && !itVischeck->second.m_bPassable && (itVischeck->second.m_iExpireTick == 0 || itVischeck->second.m_iExpireTick > I::GlobalVars->tickcount))
			{
				AbandonPath("Area blacklisted (stuck)");
				return;
			}
		}
	}
}


// !!! BETTER WAY OF DOING THIS?! !!!
// the idea is really good i think. but it repaths like 1 bajilion times even when we are slightly offpath and rapes the cpu
// maybe we should timer and radius that will tell if we are too far away from the path?
// void CNavEngine::CheckPathValidity(CTFPlayer* pLocal)
// {
// 	if (m_vCrumbs.empty())
// 		return;

// 	CNavArea* pArea = GetLocalNavArea();
// 	if (!pArea)
// 		return;

// 	bool bValid = false;
// 	if (pArea == m_tLastCrumb.m_pNavArea)
// 		bValid = true;
// 	else
// 	{
// 		for (const auto& tCrumb : m_vCrumbs)
// 		{
// 			if (pArea == tCrumb.m_pNavArea)
// 			{
// 				bValid = true;
// 				break;
// 			}
// 		}
// 	}

// 	if (!bValid)
// 	{
// 		for (const auto& tConnect : pArea->m_vConnections)
// 		{
// 			if (tConnect.m_pArea == m_vCrumbs[0].m_pNavArea)
// 			{
// 				bValid = true;
// 				break;
// 			}
// 		}
// 	}

// 	if (!bValid)
// 	{
// 		if (Vars::Debug::Logging.Value)
// 			SDK::Output("CNavEngine", "we are off the path. repathing", { 255, 131, 131 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
// 		AbandonPath("Off track");
// 	}
// }

void CNavEngine::UpdateStuckTime(CTFPlayer* pLocal)
{
	// No crumbs
	if (m_vCrumbs.empty())
		return;

	const bool bDropCrumb = m_vCrumbs[0].m_bRequiresDrop;
	float flTrigger = Vars::Misc::Movement::NavEngine::StuckTime.Value / 2.f;
	if (bDropCrumb)
		flTrigger = Vars::Misc::Movement::NavEngine::StuckTime.Value;

	// We're stuck, add time to connection
	if (m_tInactivityTimer.Check(flTrigger))
	{
		std::pair<CNavArea*, CNavArea*> tKey = m_tLastCrumb.m_pNavArea ?
			std::pair<CNavArea*, CNavArea*>(m_tLastCrumb.m_pNavArea, m_vCrumbs[0].m_pNavArea) :
			std::pair<CNavArea*, CNavArea*>(m_vCrumbs[0].m_pNavArea, m_vCrumbs[0].m_pNavArea);

		// Expires in 10 seconds
		m_pMap->m_mConnectionStuckTime[tKey].m_iExpireTick = TICKCOUNT_TIMESTAMP(Vars::Misc::Movement::NavEngine::StuckExpireTime.Value);
		// Stuck for one tick
		m_pMap->m_mConnectionStuckTime[tKey].m_iTimeStuck += 1;

		int iDetectTicks = TIME_TO_TICKS(Vars::Misc::Movement::NavEngine::StuckDetectTime.Value);
		if (bDropCrumb)
			iDetectTicks += TIME_TO_TICKS(Vars::Misc::Movement::NavEngine::StuckDetectTime.Value * 0.5f);

		// We are stuck for too long, blacklist node for a while and repath
		if (m_pMap->m_mConnectionStuckTime[tKey].m_iTimeStuck > iDetectTicks)
		{
			const auto iBlacklistExpireTick = TICKCOUNT_TIMESTAMP(Vars::Misc::Movement::NavEngine::StuckBlacklistTime.Value);
			if (Vars::Debug::Logging.Value)
				SDK::Output("CNavEngine", std::format("Stuck for too long, blacklisting the node (expires on tick: {})", iBlacklistExpireTick).c_str(), { 255, 131, 131 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
			m_pMap->m_mVischeckCache[tKey].m_iExpireTick = iBlacklistExpireTick;
			m_pMap->m_mVischeckCache[tKey].m_eVischeckState = VischeckStateEnum::NotVisible;
			m_pMap->m_mVischeckCache[tKey].m_bPassable = false;

			if (m_vCrumbs[0].m_pNavArea)
			{
				auto tAreaKey = std::pair<CNavArea*, CNavArea*>(m_vCrumbs[0].m_pNavArea, m_vCrumbs[0].m_pNavArea);
				m_pMap->m_mVischeckCache[tAreaKey].m_iExpireTick = iBlacklistExpireTick;
				m_pMap->m_mVischeckCache[tAreaKey].m_eVischeckState = VischeckStateEnum::NotVisible;
				m_pMap->m_mVischeckCache[tAreaKey].m_bPassable = false;
			}

			m_pMap->m_mConnectionStuckTime[tKey].m_iTimeStuck = 0;
			m_tInactivityTimer.Update();

			AbandonPath("Stuck");
			return;
		}

		if (m_pMap->m_mConnectionStuckTime[tKey].m_iTimeStuck > iDetectTicks / 2)
		{
			auto pLocalPlayer = H::Entities.GetLocal();
			if (pLocalPlayer && pLocalPlayer->OnSolid())
			{
				G::CurrentUserCmd->buttons |= IN_JUMP;
			}
		}
	}
}

void CNavEngine::Reset(bool bForced)
{
	CancelPath();
	m_bIgnoreTraces = false;
	m_pLocalArea = nullptr;
	m_tOffMeshTimer.Update();
	m_vOffMeshTarget = {};

	static std::string sPath = std::filesystem::current_path().string();
	if (std::string sLevelName = I::EngineClient->GetLevelName(); !sLevelName.empty())
	{
		if (m_pMap)
			m_pMap->Reset();

		if (bForced || !m_pMap || m_pMap->m_sMapName != sLevelName)
		{
			sLevelName.erase(sLevelName.find_last_of('.'));
			std::string sNavPath = std::format("{}\\tf\\{}.nav", sPath, sLevelName);
			if (Vars::Debug::Logging.Value)
				SDK::Output("NavEngine", std::format("Nav File location: {}", sNavPath).c_str(), { 50, 255, 50 }, OUTPUT_CONSOLE | OUTPUT_DEBUG | OUTPUT_TOAST | OUTPUT_MENU);
			m_pMap = std::make_unique<CMap>(sNavPath.c_str());
			m_vRespawnRoomExitAreas.clear();
			m_bUpdatedRespawnRooms = false;
		}
	}
}

bool CNavEngine::IsReady(bool bRoundCheck)
{
	static Timer tRestartTimer{};
	if (!Vars::Misc::Movement::NavEngine::Enabled.Value)
	{
		tRestartTimer.Update();
		return false;
	}
	// Too early, the engine might not fully restart yet.
	if (!tRestartTimer.Check(0.5f))
		return false;

	if (!I::EngineClient->IsInGame())
		return false;

	if (!m_pMap || m_pMap->m_eState != NavStateEnum::Active)
		return false;

	if (!bRoundCheck && IsSetupTime())
		return false;

	return true;
}

bool CNavEngine::IsBlacklistIrrelevant()
{
	static bool bIrrelevant = false;
	static Timer tUpdateTimer{};
	if (tUpdateTimer.Run(0.5f))
	{
		static int iRoundState = GR_STATE_RND_RUNNING;
		if (auto pGameRules = I::TFGameRules())
			iRoundState = pGameRules->m_iRoundState();

		bIrrelevant = iRoundState == GR_STATE_TEAM_WIN ||
			iRoundState == GR_STATE_STALEMATE ||
			iRoundState == GR_STATE_PREROUND ||
			iRoundState == GR_STATE_GAME_OVER;
	}

	return bIrrelevant;
}

void CNavEngine::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	static bool bWasOn = false;
	if (!Vars::Misc::Movement::NavEngine::Enabled.Value)
		bWasOn = false;
	else if (I::EngineClient->IsInGame() && !bWasOn)
	{
		bWasOn = true;
		Reset(true);
	}

	if (Vars::Misc::Movement::NavEngine::DisableOnSpectate.Value && H::Entities.IsSpectated())
		return;

	if (!m_bUpdatedRespawnRooms)
		UpdateRespawnRooms();

	if (!pLocal->IsAlive() || F::FollowBot.m_bActive)
	{
		CancelPath();
		return;
	}

	if ((m_eCurrentPriority == PriorityListEnum::Engineer && ((!Vars::Aimbot::AutoEngie::AutoRepair.Value && !Vars::Aimbot::AutoEngie::AutoUpgrade.Value) || pLocal->m_iClass() != TF_CLASS_ENGINEER)) ||
		(m_eCurrentPriority == PriorityListEnum::Capture && !(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::CaptureObjectives)))
	{
		CancelPath();
		return;
	}

	if (!pCmd || (pCmd->buttons & (IN_FORWARD | IN_BACK | IN_MOVERIGHT | IN_MOVELEFT) && !F::Misc.m_bAntiAFK)
		|| !IsReady(true))
		return;

	// Still in setup. If on fitting team and map, do not path yet.
	if (IsSetupTime())
	{
		CancelPath();
		return;
	}

	const Vector vLocalOrigin = pLocal->GetAbsOrigin();
	CNavArea* pArea = GetLocalNavArea(vLocalOrigin);
	bool bOnNavMesh = pArea && pArea->IsOverlapping(vLocalOrigin) && std::fabs(pArea->GetZ(vLocalOrigin.x, vLocalOrigin.y) - vLocalOrigin.z) < 18.f;

	if (bOnNavMesh || IsPathing())
	{
		m_tOffMeshTimer.Update();
	}
	else if (pArea)
	{
		Vector vTarget = pArea->GetNearestPoint(Vector2D(vLocalOrigin.x, vLocalOrigin.y));
		m_vOffMeshTarget = vTarget;

		CGameTrace trace;
		CTraceFilterNavigation filter(pLocal);
		SDK::Trace(vLocalOrigin, vTarget, MASK_PLAYERSOLID, &filter, &trace);

		if (trace.fraction > 0.01f)
		{
			m_vCrumbs.clear();
			BuildIntraAreaCrumbs(vLocalOrigin, trace.endpos, pArea);
			m_vCrumbs.push_back({ pArea, trace.endpos });
			m_eCurrentPriority = PriorityListEnum::Patrol;
			m_tOffMeshTimer.Update();
		}
	}

	if (Vars::Misc::Movement::NavEngine::VischeckEnabled.Value && !F::Ticks.m_bWarp && !F::Ticks.m_bDoubletap)
		VischeckPath();

	if (Vars::Misc::Movement::NavEngine::Draw.Value & Vars::Misc::Movement::NavEngine::DrawEnum::PossiblePaths)
	{
		m_vPossiblePaths.clear();
		m_vRejectedPaths.clear();
		if (pArea)
		{
			// Collect nearby exit areas
			std::vector<CNavArea*> vAreas;
			m_pMap->CollectAreasAround(vLocalOrigin, 500.f, vAreas);
			for (auto* pCurrentArea : vAreas)
			{
				for (auto& tConnection : pCurrentArea->m_vConnections)
				{
					if (!tConnection.m_pArea) continue;

					const auto tKey = std::pair<CNavArea*, CNavArea*>(pCurrentArea, tConnection.m_pArea);
					if (auto itCache = m_pMap->m_mVischeckCache.find(tKey); itCache != m_pMap->m_mVischeckCache.end())
					{
						if (itCache->second.m_bPassable)
							m_vPossiblePaths.push_back({ itCache->second.m_tPoints.m_vCurrent, itCache->second.m_tPoints.m_vNext });
						else
							m_vRejectedPaths.push_back({ itCache->second.m_tPoints.m_vCurrent, itCache->second.m_tPoints.m_vNext });
					}
					else
					{
						bool bIsOneWay = m_pMap->IsOneWay(pCurrentArea, tConnection.m_pArea);
						// Force a check if it's not cached
						NavPoints_t tPoints = m_pMap->DeterminePoints(pCurrentArea, tConnection.m_pArea, bIsOneWay);
						DropdownHint_t tDropdown = m_pMap->HandleDropdown(tPoints.m_vCenter, tPoints.m_vNext, bIsOneWay);
						tPoints.m_vCenter = tDropdown.m_vAdjustedPos;

						bool bPassable = IsPlayerPassableNavigation(pLocal, tPoints.m_vCurrent, tPoints.m_vCenter) &&
							IsPlayerPassableNavigation(pLocal, tPoints.m_vCenter, tPoints.m_vNext);

						// Cache it
						CachedConnection_t& tEntry = m_pMap->m_mVischeckCache[tKey];
						tEntry.m_iExpireTick = TICKCOUNT_TIMESTAMP(Vars::Misc::Movement::NavEngine::VischeckCacheTime.Value);
						tEntry.m_eVischeckState = VischeckStateEnum::Visible;
						tEntry.m_bPassable = bPassable;
						tEntry.m_tPoints = tPoints;
						tEntry.m_tDropdown = tDropdown;

						if (bPassable)
							m_vPossiblePaths.push_back({ tPoints.m_vCurrent, tPoints.m_vNext });
						else
							m_vRejectedPaths.push_back({ tPoints.m_vCurrent, tPoints.m_vNext });
					}
				}
			}
		}
	}
	else
	{
		m_vPossiblePaths.clear();
		m_vRejectedPaths.clear();
	}

	if (Vars::Misc::Movement::NavEngine::Draw.Value & Vars::Misc::Movement::NavEngine::DrawEnum::Walkable)
	{
		m_vDebugWalkablePaths.clear();
		if (pArea)
		{
			// Collect nearby exit areas
			std::vector<CNavArea*> vAreas;
			m_pMap->CollectAreasAround(vLocalOrigin, 500.f, vAreas);
			for (auto* pCurrentArea : vAreas)
			{
				for (auto& tConnection : pCurrentArea->m_vConnections)
				{
					if (!tConnection.m_pArea) continue;
					bool bIsOneWay = m_pMap->IsOneWay(pCurrentArea, tConnection.m_pArea);
					NavPoints_t tPoints = m_pMap->DeterminePoints(pCurrentArea, tConnection.m_pArea, bIsOneWay);
					DropdownHint_t tDropdown = m_pMap->HandleDropdown(tPoints.m_vCenter, tPoints.m_vNext, bIsOneWay);
					tPoints.m_vCenter = tDropdown.m_vAdjustedPos;

					F::BotUtils.IsWalkable(pLocal, tPoints.m_vCurrent, tPoints.m_vCenter);
					for (auto& segment : F::BotUtils.m_vWalkableSegments)
						m_vDebugWalkablePaths.push_back(segment);
					F::BotUtils.IsWalkable(pLocal, tPoints.m_vCenter, tPoints.m_vNext);
					for (auto& segment : F::BotUtils.m_vWalkableSegments)
						m_vDebugWalkablePaths.push_back(segment);
				}
			}
		}
	}
	else
		m_vDebugWalkablePaths.clear();

	if (!IsBlacklistIrrelevant())
	{
		m_pMap->UpdateIgnores(pLocal);
		CheckBlacklist(pLocal);
	}
	// Clear blacklist as we dont need it anyway
	else if (!m_pMap->m_mFreeBlacklist.empty())
		m_pMap->m_mFreeBlacklist.clear();

	// CheckPathValidity(pLocal);
	FollowCrumbs(pLocal, pWeapon, pCmd);
	UpdateStuckTime(pLocal);
}

void CNavEngine::AbandonPath(const std::string& sReason)
{
	if (!m_pMap)
		return;

	m_sLastFailureReason = sReason;
	m_pMap->m_pather.Reset();
	m_vCrumbs.clear();
	m_tLastCrumb.m_pNavArea = nullptr;
	// We want to repath on failure
	if (m_bRepathOnFail)
		NavTo(m_vLastDestination, m_eCurrentPriority, true, m_bCurrentNavToLocal, m_bIgnoreTraces);
	else
		m_eCurrentPriority = PriorityListEnum::None;
}

void CNavEngine::UpdateRespawnRooms()
{
	if (!m_vRespawnRooms.empty() && m_pMap)
	{
		std::unordered_set<CNavArea*> setSpawnRoomAreas;
		for (auto tRespawnRoom : m_vRespawnRooms)
		{
			static Vector vStepHeight(0.0f, 0.0f, 18.0f);
			for (auto& tArea : m_pMap->m_navfile.m_vAreas)
			{
				// Already set
				if (setSpawnRoomAreas.contains(&tArea))
					continue;

				if (tRespawnRoom.tData.PointIsWithin(tArea.m_vCenter + vStepHeight)
					|| tRespawnRoom.tData.PointIsWithin(tArea.m_vNwCorner + vStepHeight)
					|| tRespawnRoom.tData.PointIsWithin(tArea.GetNeCorner() + vStepHeight)
					|| tRespawnRoom.tData.PointIsWithin(tArea.GetSwCorner() + vStepHeight)
					|| tRespawnRoom.tData.PointIsWithin(tArea.m_vSeCorner + vStepHeight))
				{
					setSpawnRoomAreas.insert(&tArea);

					uint32_t uFlags = tRespawnRoom.m_iTeam == 0/*Any team*/ ? (TF_NAV_SPAWN_ROOM_BLUE | TF_NAV_SPAWN_ROOM_RED) : (tRespawnRoom.m_iTeam == TF_TEAM_BLUE ? TF_NAV_SPAWN_ROOM_BLUE : TF_NAV_SPAWN_ROOM_RED);
					if (!(tArea.m_iTFAttributeFlags & uFlags))
						tArea.m_iTFAttributeFlags |= uFlags;
				}
			}
		}

		// Set spawn room exit attributes
		for (auto pArea : setSpawnRoomAreas)
		{
			for (auto& tConnection : pArea->m_vConnections)
			{
				if (!(tConnection.m_pArea->m_iTFAttributeFlags & (TF_NAV_SPAWN_ROOM_RED | TF_NAV_SPAWN_ROOM_BLUE | TF_NAV_SPAWN_ROOM_EXIT)))
				{
					tConnection.m_pArea->m_iTFAttributeFlags |= TF_NAV_SPAWN_ROOM_EXIT;
					m_vRespawnRoomExitAreas.push_back(tConnection.m_pArea);
				}
			}
		}
		m_bUpdatedRespawnRooms = true;
	}
}

void CNavEngine::CancelPath()
{
	m_vCrumbs.clear();
	m_tLastCrumb.m_pNavArea = nullptr;
	m_vCurrentPathDir = {};
	m_eCurrentPriority = PriorityListEnum::None;
	m_bIgnoreTraces = false;
}

bool CanJumpIfScoped(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	// You can still jump if youre scoped in water
	if (pLocal->m_fFlags() & FL_INWATER)
		return true;

	auto iWeaponID = pWeapon->GetWeaponID();
	return iWeaponID == TF_WEAPON_SNIPERRIFLE_CLASSIC ? !pWeapon->As<CTFSniperRifleClassic>()->m_bCharging() : !pLocal->InCond(TF_COND_ZOOMED);
}

void CNavEngine::FollowCrumbs(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	static Timer tLastJump{};
	static Timer tPathRescan{};

	size_t uCrumbsSize = m_vCrumbs.size();

	const auto vLocalOrigin = pLocal->GetAbsOrigin();
	if (!uCrumbsSize && m_tOffMeshTimer.Check(6000))
	{
		m_eCurrentPriority = PriorityListEnum::Patrol;
		SDK::WalkTo(pCmd, pLocal, m_vOffMeshTarget);

		static Timer tLastJump{};
		if (m_tInactivityTimer.Check(Vars::Misc::Movement::NavEngine::StuckTime.Value / 2) && tLastJump.Check(0.2f))
		{
			F::BotUtils.ForceJump();
			tLastJump.Update();
		}

		return;
	}

	auto DoLook = [&](const Vec3& vTarget, bool bTargetValid) -> void
		{
			if (G::Attacking == 1)
			{
				F::BotUtils.InvalidateLLAP();
				return;
			}

			auto eLook = Vars::Misc::Movement::NavEngine::LookAtPath.Value;
			bool bSilent = eLook == Vars::Misc::Movement::NavEngine::LookAtPathEnum::Silent || eLook == Vars::Misc::Movement::NavEngine::LookAtPathEnum::LegitSilent;
			bool bLegit = eLook == Vars::Misc::Movement::NavEngine::LookAtPathEnum::Legit || eLook == Vars::Misc::Movement::NavEngine::LookAtPathEnum::LegitSilent;

			if (eLook == Vars::Misc::Movement::NavEngine::LookAtPathEnum::Off)
			{
				F::BotUtils.InvalidateLLAP();
				return;
			}

			if (bSilent && G::AntiAim)
			{
				F::BotUtils.InvalidateLLAP();
				return;
			}

			if (bLegit)
				F::BotUtils.LookLegit(pLocal, pCmd, bTargetValid ? vTarget : Vec3{}, bSilent);
			else if (bTargetValid)
			{
				F::BotUtils.InvalidateLLAP();
				F::BotUtils.LookAtPath(pCmd, Vec2(vTarget.x, vTarget.y), pLocal->GetEyePosition(), bSilent);
			}
			else
				F::BotUtils.InvalidateLLAP();
		};
	// No more crumbs, reset status
	if (!uCrumbsSize)
	{
		// Invalidate last crumb
		m_tLastCrumb.m_pNavArea = nullptr;

		m_bRepathOnFail = false;
		m_eCurrentPriority = PriorityListEnum::None;
		DoLook(Vec3{}, false);
		return;
	}

	// Ensure we do not try to walk downwards unless we are falling
	static std::vector<float> vFallSpeeds;
	Vector vLocalVelocity = pLocal->GetAbsVelocity();

	vFallSpeeds.push_back(vLocalVelocity.z);
	if (vFallSpeeds.size() > 10)
		vFallSpeeds.erase(vFallSpeeds.begin());

	bool bResetHeight = true;
	for (auto flFallSpeed : vFallSpeeds)
	{
		if (!(flFallSpeed <= 0.01f && flFallSpeed >= -0.01f))
			bResetHeight = false;
	}

	if (bResetHeight && !F::Ticks.m_bWarp && !F::Ticks.m_bDoubletap)
	{
		bResetHeight = false;

		Vector vEnd = vLocalOrigin;
		vEnd.z -= 100.0f;

		CGameTrace trace;
		CTraceFilterNavigation filter(pLocal);
		filter.m_iObject = OBJECT_DEFAULT;
		SDK::TraceHull(vLocalOrigin, vEnd, pLocal->m_vecMins(), pLocal->m_vecMaxs(), MASK_PLAYERSOLID, &filter, &trace);

		// Only reset if we are standing on a building
		if (trace.DidHit() && trace.m_pEnt && trace.m_pEnt->IsBuilding())
			bResetHeight = true;
	}

	constexpr float kDefaultReachRadius = 50.f;
	constexpr float kDropReachRadius = 28.f;

	Vector vCrumbTarget{};
	Vector vMoveTarget{};
	Vector vMoveDir{};
	bool bDropCrumb = false;
	bool bHasMoveDir = false;
	bool bHasMoveTarget = false;
	float flReachRadius = kDefaultReachRadius;

	while (true)
	{
		auto& tActiveCrumb = m_vCrumbs[0];
		if (m_tCurrentCrumb.m_pNavArea != tActiveCrumb.m_pNavArea)
			m_tTimeSpentOnCrumbTimer.Update();
		m_tCurrentCrumb = tActiveCrumb;

		bDropCrumb = tActiveCrumb.m_bRequiresDrop;
		vMoveTarget = vCrumbTarget = tActiveCrumb.m_vPos;

		if (bResetHeight)
		{
			vMoveTarget.z = vLocalOrigin.z;
			if (!bDropCrumb)
				vCrumbTarget.z = vMoveTarget.z;
		}

		vMoveDir = tActiveCrumb.m_vApproachDir;
		vMoveDir.z = 0.f;
		float flDirLen = vMoveDir.Length();
		if (flDirLen < 0.01f && uCrumbsSize > 1)
		{
			vMoveDir = m_vCrumbs[1].m_vPos - tActiveCrumb.m_vPos;
			vMoveDir.z = 0.f;
			flDirLen = vMoveDir.Length();
		}

		bHasMoveDir = flDirLen > 0.01f;
		if (bHasMoveDir)
		{
			vMoveDir /= flDirLen;
			if (bDropCrumb)
			{
				float flPushDistance = tActiveCrumb.m_flApproachDistance;
				if (flPushDistance <= 0.f)
					flPushDistance = std::clamp(tActiveCrumb.m_flDropHeight * 0.35f, PLAYER_WIDTH * 0.6f, PLAYER_WIDTH * 2.5f);
				else
					flPushDistance = std::clamp(flPushDistance, PLAYER_WIDTH * 0.6f, PLAYER_WIDTH * 2.5f);

				vMoveTarget += vMoveDir * flPushDistance;
			}
		}
		else
			vMoveDir = {};

		m_vCurrentPathDir = vMoveDir;

		flReachRadius = bDropCrumb ? kDropReachRadius : kDefaultReachRadius;
		Vector vCrumbCheck = vCrumbTarget;
		vCrumbCheck.z = vLocalOrigin.z;

		if (vCrumbCheck.DistToSqr(vLocalOrigin) < pow(flReachRadius, 2))
		{
			m_tLastCrumb = tActiveCrumb;
			m_vCrumbs.erase(m_vCrumbs.begin());
			m_tTimeSpentOnCrumbTimer.Update();
			m_tInactivityTimer.Update();
			uCrumbsSize = m_vCrumbs.size();
			if (!uCrumbsSize)
			{
				DoLook(Vec3{}, false);
				return;
			}
			continue;
		}

		if (!bDropCrumb && uCrumbsSize > 1)
		{
			Vector vNextCheck = m_vCrumbs[1].m_vPos;
			vNextCheck.z = vLocalOrigin.z;
			if (vNextCheck.DistToSqr(vLocalOrigin) < pow(50.0f, 2))
			{
				m_tLastCrumb = m_vCrumbs[1];
				m_vCrumbs.erase(m_vCrumbs.begin(), std::next(m_vCrumbs.begin()));
				m_tTimeSpentOnCrumbTimer.Update();
				uCrumbsSize = m_vCrumbs.size();
				if (!uCrumbsSize)
				{
					DoLook(Vec3{}, false);
					return;
				}
				m_tInactivityTimer.Update();
				continue;
			}
		}

		if (bDropCrumb)
		{
			constexpr float kDropSkipFloor = 18.f;
			bool bDropCompleted = false;
			const float flHeightBelow = vCrumbTarget.z - vLocalOrigin.z;
			const float flCompletionThreshold = std::max(kDropSkipFloor, tActiveCrumb.m_flDropHeight * 0.5f);
			if (flHeightBelow >= flCompletionThreshold)
				bDropCompleted = true;

			if (!bDropCompleted && m_pLocalArea &&
				m_pLocalArea != tActiveCrumb.m_pNavArea &&
				tActiveCrumb.m_flDropHeight > kDropSkipFloor)
				bDropCompleted = true;

			if (!bDropCompleted && uCrumbsSize > 1)
			{
				Vector vNextCheck = m_vCrumbs[1].m_vPos;
				vNextCheck.z = vLocalOrigin.z;
				const float flNextReachRadius = std::max(kDefaultReachRadius, flReachRadius + 12.f);
				if (vNextCheck.DistToSqr(vLocalOrigin) < pow(flNextReachRadius, 2))
					bDropCompleted = true;
			}

			if (bDropCompleted)
			{
				m_tLastCrumb = tActiveCrumb;
				m_vCrumbs.erase(m_vCrumbs.begin());
				m_tTimeSpentOnCrumbTimer.Update();
				m_tInactivityTimer.Update();
				uCrumbsSize = m_vCrumbs.size();
				if (!uCrumbsSize)
				{
					DoLook(Vec3{}, false);
					return;
				}
				continue;
			}
		}

		break;
	}

	// If we make any progress at all, reset this
	// If we spend way too long on this crumb, ignore the logic below
	if (!m_tTimeSpentOnCrumbTimer.Check(Vars::Misc::Movement::NavEngine::StuckDetectTime.Value))
	{
		// 44.0f -> Revved brass beast, do not use z axis as jumping counts towards that.
		if (!vLocalVelocity.Get2D().IsZero(40.0f))
			m_tInactivityTimer.Update();
		else if (bDropCrumb)
		{
			if (bHasMoveDir)
				vMoveTarget += vMoveDir * (PLAYER_WIDTH * 0.75f);
			m_tInactivityTimer.Update();
		}
		else if (Vars::Debug::Logging.Value)
			SDK::Output("CNavEngine", std::format("Spent too much time on the crumb, assuming were stuck, 2Dvelocity: ({},{})", fabsf(vLocalVelocity.Get2D().x), fabsf(vLocalVelocity.Get2D().y)).c_str(), { 255, 131, 131 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
	}

	//if ( !G::DoubleTap && !G::Warp )
	{
		// Detect when jumping is necessary.
		// 1. No jumping if zoomed (or revved)
		// 2. Jump only after inactivity-based stuck detection (or explicit overrides)
		if (pWeapon)
		{
			auto iWeaponID = pWeapon->GetWeaponID();
			if ((iWeaponID != TF_WEAPON_SNIPERRIFLE &&
				iWeaponID != TF_WEAPON_SNIPERRIFLE_CLASSIC &&
				iWeaponID != TF_WEAPON_SNIPERRIFLE_DECAP) ||
				CanJumpIfScoped(pLocal, pWeapon))
			{
				if (iWeaponID != TF_WEAPON_MINIGUN || !(pCmd->buttons & IN_ATTACK2))
				{
					bool bShouldJump = false;
					bool bPreventJump = bDropCrumb;
					if (m_vCrumbs.size() > 1)
					{
						float flHeightDiff = m_vCrumbs[0].m_vPos.z - m_vCrumbs[1].m_vPos.z;
						if (flHeightDiff < 0 && flHeightDiff <= -PLAYER_JUMP_HEIGHT)
							bPreventJump = true;
					}
					// Jump only when inactivity timer says we're stuck and if current area allows jumping
					if (!bPreventJump && m_pLocalArea &&
						m_tInactivityTimer.Check(Vars::Misc::Movement::NavEngine::StuckTime.Value / 2) &&
						!(m_pLocalArea->m_iAttributeFlags & (NAV_MESH_NO_JUMP | NAV_MESH_STAIRS)))
						bShouldJump = true;

					if (bShouldJump && tLastJump.Check(0.2f))
					{
						F::BotUtils.ForceJump();
						tLastJump.Update();
					}
				}
			}
		}
	}

	if (G::Attacking != 1)
	{
		auto eLook = Vars::Misc::Movement::NavEngine::LookAtPath.Value;
		bool bSilent = eLook == Vars::Misc::Movement::NavEngine::LookAtPathEnum::Silent || eLook == Vars::Misc::Movement::NavEngine::LookAtPathEnum::LegitSilent;
		bool bLegit = eLook == Vars::Misc::Movement::NavEngine::LookAtPathEnum::Legit || eLook == Vars::Misc::Movement::NavEngine::LookAtPathEnum::LegitSilent;

		if (eLook == Vars::Misc::Movement::NavEngine::LookAtPathEnum::Off)
			F::BotUtils.InvalidateLLAP();
		else if (bSilent && G::AntiAim)
			F::BotUtils.InvalidateLLAP();
		else if (bLegit)
		{
			Vec3 vLookTarget{ vMoveTarget.x, vMoveTarget.y, vMoveTarget.z };
			F::BotUtils.LookLegit(pLocal, pCmd, vLookTarget, bSilent);
		}
		else
		{
			F::BotUtils.InvalidateLLAP();
			F::BotUtils.LookAtPath(pCmd, Vec2(vMoveTarget.x, vMoveTarget.y), pLocal->GetEyePosition(), bSilent);
		}
	}
	else
		F::BotUtils.InvalidateLLAP();

	SDK::WalkTo(pCmd, pLocal, vMoveTarget);
}

void CNavEngine::Render()
{
	if (!Vars::Misc::Movement::NavEngine::Draw.Value || !IsReady())
		return;

	auto pLocal = H::Entities.GetLocal();
	if (!pLocal || !pLocal->IsAlive())
		return;

	/*if (!F::NavBot.m_vSlightDangerDrawlistNormal.empty())
	{
		for (auto vPos : F::NavBot.m_vSlightDangerDrawlistNormal)
		{
			RenderBox(vPos, Vector(-4.0f, -4.0f, -1.0f), Vector(4.0f, 4.0f, 1.0f), Vector(), Color_t(255, 150, 0, 255), Color_t(255, 150, 0, 255), false);
		}
	}

	if (!F::NavBot.m_vSlightDangerDrawlistDormant.empty())
	{
		for (auto vPos : F::NavBot.m_vSlightDangerDrawlistDormant)
		{
			RenderBox(vPos, Vector(-4.0f, -4.0f, -1.0f), Vector(4.0f, 4.0f, 1.0f), Vector(), Color_t(255, 150, 0, 255), Color_t(255, 150, 0, 255), false);
		}
	}*/

	if (Vars::Misc::Movement::NavEngine::Draw.Value & Vars::Misc::Movement::NavEngine::DrawEnum::Blacklist)
	{
		if (auto pBlacklist = GetFreeBlacklist())
		{
			if (!pBlacklist->empty())
			{
				for (auto& tBlacklistedArea : *pBlacklist)
				{
					H::Draw.RenderBox(tBlacklistedArea.first->m_vCenter, Vector(-4.0f, -4.0f, -1.0f), Vector(4.0f, 4.0f, 1.0f), Vector(), Vars::Colors::NavbotBlacklist.Value, false);
					H::Draw.RenderWireframeBox(tBlacklistedArea.first->m_vCenter, Vector(-4.0f, -4.0f, -1.0f), Vector(4.0f, 4.0f, 1.0f), Vector(), Vars::Colors::NavbotBlacklist.Value, false);
				}
			}
		}
	}
	Vector vOrigin = pLocal->GetAbsOrigin();
	if (Vars::Misc::Movement::NavEngine::Draw.Value & Vars::Misc::Movement::NavEngine::DrawEnum::Area && GetLocalNavArea(vOrigin))
	{
		auto vEdge = m_pLocalArea->GetNearestPoint(Vector2D(vOrigin.x, vOrigin.y));
		vEdge.z += PLAYER_CROUCHED_JUMP_HEIGHT;
		H::Draw.RenderBox(vEdge, Vector(-4.0f, -4.0f, -1.0f), Vector(4.0f, 4.0f, 1.0f), Vector(), Color_t(255, 0, 0, 255), false);
		H::Draw.RenderWireframeBox(vEdge, Vector(-4.0f, -4.0f, -1.0f), Vector(4.0f, 4.0f, 1.0f), Vector(), Color_t(255, 0, 0, 255), false);

		// Nw -> Ne
		H::Draw.RenderLine(m_pLocalArea->m_vNwCorner, m_pLocalArea->GetNeCorner(), Vars::Colors::NavbotArea.Value, true);
		// Nw -> Sw
		H::Draw.RenderLine(m_pLocalArea->m_vNwCorner, m_pLocalArea->GetSwCorner(), Vars::Colors::NavbotArea.Value, true);
		// Ne -> Se
		H::Draw.RenderLine(m_pLocalArea->GetNeCorner(), m_pLocalArea->m_vSeCorner, Vars::Colors::NavbotArea.Value, true);
		// Sw -> Se
		H::Draw.RenderLine(m_pLocalArea->GetSwCorner(), m_pLocalArea->m_vSeCorner, Vars::Colors::NavbotArea.Value, true);
	}

	if (Vars::Misc::Movement::NavEngine::Draw.Value & Vars::Misc::Movement::NavEngine::DrawEnum::Path && !m_vCrumbs.empty())
	{
		for (size_t i = 0; i < m_vCrumbs.size() - 1; i++)
			H::Draw.RenderLine(m_vCrumbs[i].m_vPos, m_vCrumbs[i + 1].m_vPos, Vars::Colors::NavbotPath.Value, false);
	}

	if (Vars::Misc::Movement::NavEngine::Draw.Value & Vars::Misc::Movement::NavEngine::DrawEnum::PossiblePaths)
	{
		for (auto& tPath : m_vPossiblePaths)
			H::Draw.RenderLine(tPath.first, tPath.second, Vars::Colors::NavbotPossiblePath.Value, false);
		for (auto& tPath : m_vRejectedPaths)
			H::Draw.RenderLine(tPath.first, tPath.second, Color_t(255, 0, 0, 255), false);
	}

	if (Vars::Misc::Movement::NavEngine::Draw.Value & Vars::Misc::Movement::NavEngine::DrawEnum::Walkable)
	{
		for (auto& tPath : m_vDebugWalkablePaths)
			H::Draw.RenderLine(tPath.first, tPath.second, Vars::Colors::NavbotWalkablePath.Value, false);
	}

	F::NavBotEngineer.Render();
}