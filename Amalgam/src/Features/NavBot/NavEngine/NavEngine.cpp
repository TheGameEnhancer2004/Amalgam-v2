#include "NavEngine.h"
#include "../../Ticks/Ticks.h"
#include "../../Misc/Misc.h"
#include "../BotUtils.h"
#include "../../FollowBot/FollowBot.h"
#include <limits>

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
	CTraceFilterNavigation filter = {};
	SDK::Trace(vFrom, vTo, nMask, &filter, &trace);
	return trace.fraction == 1.0f;
}

// ??????
bool CNavEngine::IsPlayerPassableNavigation(const Vector vFrom, Vector vTo, unsigned int nMask)
{
	CGameTrace trace = {};
	CTraceFilterNavigation filter = {};

	Vector vToTarget = vTo - vFrom, vAngles;
	Math::VectorAngles(vToTarget, vAngles);

	Vector vForward, vRight;
	Math::AngleVectors(vAngles, &vForward, &vRight, nullptr);
	vRight.z = 0;

	// We want to keep the same angle for these two bounding box traces
	Vector vRelativeEndPos = vForward * vToTarget.Length();

	Vector vLeftRayStart = vFrom - vRight * HALF_PLAYER_WIDTH;
	Vector vLeftRayEnd = vLeftRayStart + vRelativeEndPos;
	SDK::Trace(vLeftRayStart, vLeftRayEnd, nMask, &filter, &trace);

	// Left ray hit something
	if (trace.DidHit())
		return false;

	Vector vRightRayStart = vFrom + vRight * HALF_PLAYER_WIDTH;
	Vector vRightRayEnd = vRightRayStart + vRelativeEndPos;
	SDK::Trace(vRightRayStart, vRightRayEnd, nMask, &filter, &trace);

	// Return if the right ray hit something
	return !trace.DidHit();
}

bool CNavEngine::NavTo(const Vector& vDestination, PriorityListEnum::PriorityListEnum ePriority, bool bShouldRepath, bool bNavToLocal, bool bIsRepath)
{
	if (F::Ticks.m_bWarp || F::Ticks.m_bDoubletap)
		return false;

	if (!IsReady())
		return false;

	// Don't path, priority is too low
	if (ePriority < m_eCurrentPriority)
		return false;

	if (!GetLocalNavArea())
		return false;

	CNavArea* pDestArea = FindClosestNavArea(vDestination, false);
	if (!m_pLocalArea || !pDestArea)
		return false;

	auto vPath = m_pMap->FindPath(m_pLocalArea, pDestArea);
	if (vPath.empty())
		return false;

	if (!bNavToLocal)
		vPath.erase(vPath.begin());

	m_vCrumbs.clear();
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
				tPoints = m_pMap->DeterminePoints(pArea, pNextArea);
				tDropdown = m_pMap->HandleDropdown(tPoints.m_vCenter, tPoints.m_vNext);
				tPoints.m_vCenter = tDropdown.m_vAdjustedPos;
			}

			Crumb_t tStartCrumb = {};
			tStartCrumb.m_pNavArea = pArea;
			tStartCrumb.m_vPos = tPoints.m_vCurrent;
			m_vCrumbs.push_back(tStartCrumb);

			Crumb_t tCenterCrumb = {};
			tCenterCrumb.m_pNavArea = pArea;
			tCenterCrumb.m_vPos = tPoints.m_vCenter;
			tCenterCrumb.m_bRequiresDrop = tDropdown.m_bRequiresDrop;
			tCenterCrumb.m_flDropHeight = tDropdown.m_flDropHeight;
			tCenterCrumb.m_flApproachDistance = tDropdown.m_flApproachDistance;
			tCenterCrumb.m_vApproachDir = tDropdown.m_vApproachDir;
			m_vCrumbs.push_back(tCenterCrumb);
		}
		else
			m_vCrumbs.push_back({ pArea, pArea->m_vCenter });
	}

	m_vCrumbs.push_back({ nullptr, vDestination });
	m_tInactivityTimer.Update();

	m_eCurrentPriority = ePriority;
	m_bCurrentNavToLocal = bNavToLocal;
	m_bRepathOnFail = bShouldRepath;

	// Ensure we know where to go
	if (m_bRepathOnFail)
		m_vLastDestination = vDestination;

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

	const auto iVischeckCacheExpireTimestamp = TICKCOUNT_TIMESTAMP(Vars::Misc::Movement::NavEngine::VischeckCacheTime.Value);

	// Iterate all the crumbs
	for (auto it = m_vCrumbs.begin(), next = it + 1; next != m_vCrumbs.end(); it++, next++)
	{
		auto tCrumb = *it;
		auto tNextCrumb = *next;
		auto tKey = std::pair<CNavArea*, CNavArea*>(tCrumb.m_pNavArea, tNextCrumb.m_pNavArea);

		auto vCurrentCenter = tCrumb.m_vPos;
		vCurrentCenter.z += PLAYER_CROUCHED_JUMP_HEIGHT;

		auto vNextCenter = tNextCrumb.m_vPos;
		vNextCenter.z += PLAYER_CROUCHED_JUMP_HEIGHT;
		
		// Check if we can pass, if not, abort pathing and mark as bad
		if (!IsPlayerPassableNavigation(vCurrentCenter, vNextCenter))
		{
			// Mark as invalid for a while
			CachedConnection_t tEntry{};
			tEntry.m_iExpireTick = iVischeckCacheExpireTimestamp;
			tEntry.m_eVischeckState = VischeckStateEnum::NotVisible;
			tEntry.m_bPassable = false;
			tEntry.m_flCachedCost = std::numeric_limits<float>::max();
			m_pMap->m_mVischeckCache[tKey] = tEntry;
			AbandonPath();
			break;
		}
		// Else we can update the cache (if not marked bad before this)
		else if (!m_pMap->m_mVischeckCache.count(tKey) || m_pMap->m_mVischeckCache[tKey].m_eVischeckState != VischeckStateEnum::NotVisible)
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
	if (!tBlacklistCheckTimer.Run(0.5f))
		return;

	// Local player is ubered and does not care about the blacklist
	// TODO: Only for damage type things
	if (pLocal->IsInvulnerable())
	{
		m_pMap->m_bFreeBlacklistBlocked = true;
		m_pMap->m_pather.Reset();
		return;
	}

	for (auto&[pArea, _] : m_pMap->m_mFreeBlacklist)
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

	for (auto& tCrumb : m_vCrumbs)
	{
		// A path Node is blacklisted, abandon pathing
		for (auto&[pArea, _] : m_pMap->m_mFreeBlacklist)
		{
			if (pArea == tCrumb.m_pNavArea)
			{
				AbandonPath();
				return;
			}
		}
	}
}

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
			m_pMap->m_mVischeckCache[tKey].m_eVischeckState = VischeckStateEnum::NotChecked;
			m_pMap->m_mVischeckCache[tKey].m_bPassable = false;
			AbandonPath();
			return;
		}
	}
}

void CNavEngine::Reset(bool bForced)
{
	CancelPath();
	m_pLocalArea = nullptr;

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

	GetLocalNavArea(pLocal->GetAbsOrigin());

	if (Vars::Misc::Movement::NavEngine::VischeckEnabled.Value && !F::Ticks.m_bWarp && !F::Ticks.m_bDoubletap)
		VischeckPath();

	if (!IsBlacklistIrrelevant())
	{
		m_pMap->UpdateIgnores(pLocal);
		CheckBlacklist(pLocal);
	}
	// Clear blacklist as we dont need it anyway
	else if (!m_pMap->m_mFreeBlacklist.empty())
		m_pMap->m_mFreeBlacklist.clear();


	FollowCrumbs(pLocal, pWeapon, pCmd);
	UpdateStuckTime(pLocal);
}

void CNavEngine::AbandonPath()
{
	if (!m_pMap)
		return;

	m_pMap->m_pather.Reset();
	m_vCrumbs.clear();
	m_tLastCrumb.m_pNavArea = nullptr;
	// We want to repath on failure
	if (m_bRepathOnFail)
		NavTo(m_vLastDestination, m_eCurrentPriority, true, m_bCurrentNavToLocal, false);
	else
		m_eCurrentPriority = PriorityListEnum::None;
}

void CNavEngine::CancelPath()
{
	m_vCrumbs.clear();
	m_tLastCrumb.m_pNavArea = nullptr;
	m_eCurrentPriority = PriorityListEnum::None;
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
	static int iTicksSinceJump = 0;
	static bool bCrouch = false; // Used to determine if we want to jump or if we want to crouch

	size_t uCrumbsSize = m_vCrumbs.size();
	// No more crumbs, reset status
	if (!uCrumbsSize)
	{
		// Invalidate last crumb
		m_tLastCrumb.m_pNavArea = nullptr;

		m_bRepathOnFail = false;
		m_eCurrentPriority = PriorityListEnum::None;
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

	const auto vLocalOrigin = pLocal->GetAbsOrigin();
	if (bResetHeight && !F::Ticks.m_bWarp && !F::Ticks.m_bDoubletap)
	{
		bResetHeight = false;

		Vector vEnd = vLocalOrigin;
		vEnd.z -= 100.0f;

		CGameTrace trace;
		CTraceFilterHitscan filter{};
		filter.pSkip = pLocal;
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
				return;
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
					return;
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
					return;
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
						// Make it crouch until we land, but jump the first tick
						pCmd->buttons |= bCrouch ? IN_DUCK : IN_JUMP;

						// Only flip to crouch state, not to jump state
						if (!bCrouch)
						{
							bCrouch = true;
							iTicksSinceJump = 0;
						}
						iTicksSinceJump++;

						// Update jump timer now since we are back on ground
						if (bCrouch && pLocal->OnSolid() && iTicksSinceJump > 3)
						{
							// Reset
							bCrouch = false;
							tLastJump.Update();
						}
					}
				}
			}
		}
	}

	if (G::Attacking != 1)
	{
		// Look at path (nav spin) (smooth nav)
		switch (Vars::Misc::Movement::NavEngine::LookAtPath.Value)
		{
		case Vars::Misc::Movement::NavEngine::LookAtPathEnum::Off:
			break;
		case Vars::Misc::Movement::NavEngine::LookAtPathEnum::Silent:
			if (G::AntiAim)
				break;
			[[fallthrough]];
		case Vars::Misc::Movement::NavEngine::LookAtPathEnum::Plain:
		default:
			F::BotUtils.LookAtPath(pCmd, Vec2( vMoveTarget.x, vMoveTarget.y ), pLocal->GetEyePosition(), Vars::Misc::Movement::NavEngine::LookAtPath.Value == Vars::Misc::Movement::NavEngine::LookAtPathEnum::Silent);
			break;
		}
	}

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
}