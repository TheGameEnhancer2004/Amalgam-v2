#include "NavEngine.h"
#include "../BotUtils.h"
#include "../DangerManager/DangerManager.h"

// 0 = Success, 1 = No Path, 2 = Start/End invalid
int CMap::Solve(CNavArea* pStart, CNavArea* pEnd, std::vector<void*>* path, float* cost)
{
	if (!pStart || !pEnd || m_navfile.m_vAreas.empty())
		return 2;

	if (m_vPathNodes.size() != m_navfile.m_vAreas.size())
		m_vPathNodes.assign(m_navfile.m_vAreas.size(), {});

	m_iQueryId++;

	using NodePair = std::pair<float, size_t>;
	std::priority_queue<NodePair, std::vector<NodePair>, std::greater<NodePair>> openSet;

	size_t uStartIndex = pStart - &m_navfile.m_vAreas[0];
	size_t uEndIndex = pEnd - &m_navfile.m_vAreas[0];

	if (uStartIndex >= m_vPathNodes.size() || uEndIndex >= m_vPathNodes.size())
		return 2;

	PathNode_t& tStartNode = m_vPathNodes[uStartIndex];
	tStartNode.m_g = 0.0f;
	tStartNode.m_f = pStart->m_vCenter.DistTo(pEnd->m_vCenter);
	tStartNode.m_pParent = nullptr;
	tStartNode.m_iQueryId = m_iQueryId;
	tStartNode.m_bInOpen = true;

	openSet.push({ tStartNode.m_f, uStartIndex });

	std::vector<micropather::StateCost> vNeighbors;
	vNeighbors.reserve(8);

	while (!openSet.empty())
	{
		size_t uCurrentIndex = openSet.top().second;
		float fCurrentScore = openSet.top().first;
		openSet.pop();

		if (uCurrentIndex == uEndIndex)
		{
			// Path Found! Reconstruct.
			if (cost) *cost = m_vPathNodes[uCurrentIndex].m_g;
			
			path->clear();
			CNavArea* pCurrent = pEnd;
			while (pCurrent)
			{
				path->push_back(reinterpret_cast<void*>(pCurrent)); // MicroPather returns void*
				size_t uIdx = pCurrent - &m_navfile.m_vAreas[0];
				pCurrent = m_vPathNodes[uIdx].m_pParent;
			}
			std::reverse(path->begin(), path->end());
			return 0;
		}

		PathNode_t& tCurrentNode = m_vPathNodes[uCurrentIndex];
		tCurrentNode.m_bInOpen = false;

		if (fCurrentScore > tCurrentNode.m_f)
			continue;

		CNavArea* pCurrentArea = &m_navfile.m_vAreas[uCurrentIndex];

		vNeighbors.clear();
		GetDirectNeighbors(pCurrentArea, vNeighbors);

		for (const auto& tNeighbor : vNeighbors)
		{
			CNavArea* pNextArea = reinterpret_cast<CNavArea*>(tNeighbor.state);
			float flCostToNext = tNeighbor.cost;
			
			size_t uNextIndex = pNextArea - &m_navfile.m_vAreas[0];
			PathNode_t& tNextNode = m_vPathNodes[uNextIndex];

			if (tNextNode.m_iQueryId != m_iQueryId)
			{
				tNextNode.m_g = std::numeric_limits<float>::max();
				tNextNode.m_f = std::numeric_limits<float>::max();
				tNextNode.m_pParent = nullptr;
				tNextNode.m_iQueryId = m_iQueryId;
				tNextNode.m_bInOpen = false;
			}

			float flNewG = tCurrentNode.m_g + flCostToNext;

			if (flNewG < tNextNode.m_g)
			{
				tNextNode.m_pParent = pCurrentArea;
				tNextNode.m_g = flNewG;
				float h = pNextArea->m_vCenter.DistTo(pEnd->m_vCenter);
				tNextNode.m_f = flNewG + h;

				if (!tNextNode.m_bInOpen)
				{
					tNextNode.m_bInOpen = true;
					openSet.push({ tNextNode.m_f, uNextIndex });
				}
			}
		}
	}

	return 1;
}

void CMap::GetDirectNeighbors(CNavArea* pCurrentArea, std::vector<micropather::StateCost>& neighbors)
{
	const int iNow = I::GlobalVars->tickcount;
	const int iCacheExpiry = TICKCOUNT_TIMESTAMP(Vars::Misc::Movement::NavEngine::VischeckCacheTime.Value);

	auto pLocal = H::Entities.GetLocal();
	const int iTeam = pLocal ? pLocal->m_iTeamNum() : 0;

	AdjacentCost(reinterpret_cast<void*>(pCurrentArea), &neighbors);
}

void CMap::AdjacentCost(void* pArea, std::vector<micropather::StateCost>* pAdjacent)
{
	if (!pArea)
		return;

	CNavArea* pCurrentArea = reinterpret_cast<CNavArea*>(pArea);
	const int iNow = I::GlobalVars->tickcount;
	const int iCacheExpirySeconds = std::min(Vars::Misc::Movement::NavEngine::VischeckCacheTime.Value, 45);
	const int iCacheExpiry = TICKCOUNT_TIMESTAMP(iCacheExpirySeconds);

	auto pLocal = H::Entities.GetLocal();
	const int iTeam = pLocal ? pLocal->m_iTeamNum() : 0;
	for (NavConnect_t& tConnection : pCurrentArea->m_vConnections)
	{
		CNavArea* pNextArea = tConnection.m_pArea;
		if (!pNextArea || pNextArea == pCurrentArea)
			continue;

		if (!IsAreaValid(pCurrentArea) || !IsAreaValid(pNextArea) || !HasDirectConnection(pCurrentArea, pNextArea))
			continue;

		const auto tAreaBlockKey = std::pair<CNavArea*, CNavArea*>(pNextArea, pNextArea);
		if (auto itBlocked = m_mVischeckCache.find(tAreaBlockKey); itBlocked != m_mVischeckCache.end())
		{
			if (itBlocked->second.m_eVischeckState == VischeckStateEnum::NotVisible &&
				(itBlocked->second.m_iExpireTick == 0 || itBlocked->second.m_iExpireTick > iNow))
			{
				if (itBlocked->second.m_bStuckBlacklist)
					continue;
			}
		}

		float flBlacklistPenalty = 0.f;
		if (!m_bFreeBlacklistBlocked && !F::NavEngine.m_bIgnoreTraces)
		{
			if (auto itBlacklist = m_mFreeBlacklist.find(pNextArea); itBlacklist != m_mFreeBlacklist.end())
			{
				flBlacklistPenalty = GetBlacklistPenalty(itBlacklist->second);
				if (!std::isfinite(flBlacklistPenalty))
					continue;
			}
		}

		const auto tKey = std::pair<CNavArea*, CNavArea*>(pCurrentArea, pNextArea);
		
		CachedConnection_t& tEntry = m_mVischeckCache[tKey];
		bool bValidCache = (tEntry.m_iExpireTick == 0 || tEntry.m_iExpireTick > iNow);

		if (bValidCache && !tEntry.m_bPassable)
		{
			if (tEntry.m_bStuckBlacklist)
				continue;

			bValidCache = false;
		}

		NavPoints_t tPoints{};
		DropdownHint_t tDropdown{};
		float flBaseCost = std::numeric_limits<float>::max();
		bool bPassable = false;

		if (bValidCache && tEntry.m_eVischeckState == VischeckStateEnum::Visible && tEntry.m_bPassable)
		{
			tPoints = tEntry.m_tPoints;
			tDropdown = tEntry.m_tDropdown;
			flBaseCost = tEntry.m_flCachedCost;
			bPassable = true;
		}
		else
		{
			bool bIsOneWay = IsOneWay(pCurrentArea, pNextArea);

			tPoints = DeterminePoints(pCurrentArea, pNextArea, bIsOneWay);
			tDropdown = HandleDropdown(tPoints.m_vCenter, tPoints.m_vNext, bIsOneWay);
			tPoints.m_vCenter = tDropdown.m_vAdjustedPos;

			const float flUpDelta = tPoints.m_vCenterNext.z - tPoints.m_vCenter.z;
			const float flPlanarDelta = tPoints.m_vCenter.DistTo2D(tPoints.m_vCenterNext);
			const float flCenterPlanarDelta = pCurrentArea->m_vCenter.DistTo2D(pNextArea->m_vCenter);
			const float flOverlapX = std::min(pCurrentArea->m_vSeCorner.x, pNextArea->m_vSeCorner.x) - std::max(pCurrentArea->m_vNwCorner.x, pNextArea->m_vNwCorner.x);
			const float flOverlapY = std::min(pCurrentArea->m_vSeCorner.y, pNextArea->m_vSeCorner.y) - std::max(pCurrentArea->m_vNwCorner.y, pNextArea->m_vNwCorner.y);
			const bool bStackedOverlap = flOverlapX > PLAYER_WIDTH * 1.2f && flOverlapY > PLAYER_WIDTH * 1.2f;
			const bool bSuspiciousVerticalLink = !bIsOneWay
				&& flUpDelta > std::max(PLAYER_CROUCHED_JUMP_HEIGHT * 0.8f, 36.f)
				&& bStackedOverlap
				&& flCenterPlanarDelta < PLAYER_WIDTH * 0.75f
				&& flPlanarDelta < PLAYER_WIDTH * 0.4f;
			const bool bClearlyUnreachableJump = !bIsOneWay &&
				(flUpDelta > (PLAYER_CROUCHED_JUMP_HEIGHT + 10.f) || (flUpDelta > PLAYER_JUMP_HEIGHT * 1.35f && flPlanarDelta < PLAYER_WIDTH * 1.1f));
			const int iUnreachableCacheExpiry = TICKCOUNT_TIMESTAMP(90.f);

			if (!F::NavEngine.m_bIgnoreTraces && ((flUpDelta > PLAYER_CROUCHED_JUMP_HEIGHT) || bSuspiciousVerticalLink || bClearlyUnreachableJump))
			{
				tEntry.m_iExpireTick = bClearlyUnreachableJump ? iUnreachableCacheExpiry : iCacheExpiry;
				tEntry.m_eVischeckState = VischeckStateEnum::NotVisible;
				tEntry.m_bPassable = false;
				tEntry.m_flCachedCost = std::numeric_limits<float>::max();
				tEntry.m_tPoints = tPoints;
				tEntry.m_tDropdown = tDropdown;
				continue;
			}

			if (!F::NavEngine.m_bIgnoreTraces && pLocal)
			{
				if (bSuspiciousVerticalLink)
				{
					const bool bPassToMid = F::NavEngine.IsPlayerPassableNavigation(pLocal, tPoints.m_vCurrent, tPoints.m_vCenter);
					const bool bPassToNext = bPassToMid && F::NavEngine.IsPlayerPassableNavigation(pLocal, tPoints.m_vCenter, tPoints.m_vNext);
					if (!bPassToNext)
					{
						tEntry.m_iExpireTick = iUnreachableCacheExpiry;
						tEntry.m_eVischeckState = VischeckStateEnum::NotVisible;
						tEntry.m_bPassable = false;
						tEntry.m_flCachedCost = std::numeric_limits<float>::max();
						tEntry.m_tPoints = tPoints;
						tEntry.m_tDropdown = tDropdown;
						continue;
					}
				}
			}

			bPassable = true;
			flBaseCost = EvaluateConnectionCost(pCurrentArea, pNextArea, tPoints, tDropdown, iTeam);

			tEntry.m_iExpireTick = iCacheExpiry;
			tEntry.m_eVischeckState = VischeckStateEnum::Visible;
			tEntry.m_bPassable = true;
			tEntry.m_tPoints = tPoints;
			tEntry.m_tDropdown = tDropdown;
			tEntry.m_flCachedCost = flBaseCost;
		}

		if (!bPassable)
			continue;

		if (!std::isfinite(flBaseCost) || flBaseCost <= 0.f)
		{
			flBaseCost = EvaluateConnectionCost(pCurrentArea, pNextArea, tPoints, tDropdown, iTeam);
			tEntry.m_flCachedCost = flBaseCost;
			tEntry.m_tPoints = tPoints;
			tEntry.m_tDropdown = tDropdown;
		}

		float flFinalCost = std::max(tPoints.m_vCurrent.DistTo(tPoints.m_vNext), 1.f);
		if (!F::NavEngine.m_bIgnoreTraces)
		{
			if (!m_bFreeBlacklistBlocked && flBlacklistPenalty > 0.f && std::isfinite(flBlacklistPenalty))
				flFinalCost += std::clamp(flBlacklistPenalty * 0.2f, 0.f, 350.f);

			const float flDangerPenalty = std::clamp(F::DangerManager.GetCost(pNextArea) * 0.02f, 0.f, 220.f);
			flFinalCost += flDangerPenalty;

			if (auto itStuck = m_mConnectionStuckTime.find(tKey); itStuck != m_mConnectionStuckTime.end())
			{
				if (itStuck->second.m_iExpireTick == 0 || itStuck->second.m_iExpireTick > iNow)
				{
					float flStuckPenalty = std::clamp(static_cast<float>(itStuck->second.m_iTimeStuck) * 18.f, 12.f, 160.f);
					flFinalCost += flStuckPenalty;
				}
			}
		}
		else
			flFinalCost *= 1.2f;

		if (!std::isfinite(flFinalCost) || flFinalCost <= 0.f)
			continue;

		if (Vars::Misc::Movement::NavEngine::PathRandomization.Value)
		{
			// Deterministic randomization based on bot index and area address blah blah i dont know how it works
			// ai made this im bad at math
			
			// This has nothing to do with 'bot index', entindex can get changed often 
			// so if your intention was to make the bots have different pathing based on their 'botpanel index' this is not the approach.
			// 
			// Currently it takes into account area pointers and an entity id, using entindex here is not the best idea
			// since area ptrs have a physical memory address which should already be different for each bot (unless im not understanding how virtual machines work in which case this logic makes even less sense).
			// 
			// So right now if my assumptions are correct the random here is not based on anything, its literally random based on less random
			// 
			// Also try not to use ai next time for something you dont understand. 
			// Im fine with using it for repetitive tasks or discovering new ways of solving problems 
			// but using it for adding something you have no understanding of is not only stupid but also dangerous because it could cause some issues later

			uintptr_t uSeed = reinterpret_cast<uintptr_t>(pNextArea) ^ (pLocal ? pLocal->entindex() : 0);
			uSeed = (uSeed ^ 0xDEADBEEF) * 1664525u + 1013904223u;
			float flNoise = (uSeed & 0xFFFF) / 65536.0f;
			flFinalCost *= (1.0f + flNoise * 0.15f);
		}

		pAdjacent->push_back({ reinterpret_cast<void*>(pNextArea), flFinalCost });
		
		if (tEntry.m_bPassable)
		{
			tEntry.m_flCachedCost = flBaseCost;
			tEntry.m_iExpireTick = iCacheExpiry;
		}
	}
}

bool CMap::HasDirectConnection(CNavArea* pFrom, CNavArea* pTo) const
{
	if (!pFrom || !pTo)
		return false;

	if (pFrom == pTo)
		return true;

	for (const auto& tConnection : pFrom->m_vConnections)
	{
		if (tConnection.m_pArea == pTo)
			return true;
	}

	return false;
}

DropdownHint_t CMap::HandleDropdown(const Vector& vCurrentPos, const Vector& vNextPos, bool bIsOneWay)
{
	DropdownHint_t tHint{};
	tHint.m_vAdjustedPos = vCurrentPos;

	Vector vToTarget = vNextPos - vCurrentPos;
	const float flHeightDiff = vToTarget.z;

	Vector vHorizontal = vToTarget;
	vHorizontal.z = 0.f;
	const float flHorizontalLength = vHorizontal.Length();

	constexpr float kSmallDropGrace = 18.f;
	constexpr float kEdgePadding = 8.f;

	if (flHeightDiff < 0.f)
	{
		const float flDropDistance = -flHeightDiff;
		if (flDropDistance > kSmallDropGrace && flHorizontalLength > 1.f)
		{
			tHint.m_bRequiresDrop = true;
			tHint.m_flDropHeight = flDropDistance;
			tHint.m_vApproachDir = vHorizontal / flHorizontalLength;

			Vector vDirection = tHint.m_vApproachDir;

			const float desiredAdvance = std::clamp(flDropDistance * 0.5f, PLAYER_WIDTH * 0.85f, PLAYER_WIDTH * 2.5f);
			const float flMaxAdvance = std::max(flHorizontalLength - kEdgePadding, 0.f);
			float flApproach = desiredAdvance;
			if (flMaxAdvance > 0.f)
				flApproach = std::min(flApproach, flMaxAdvance);
			else
				flApproach = std::min(flApproach, flHorizontalLength * 0.8f);

			const float flMinAdvanceRatio = bIsOneWay ? 0.35f : 0.5f;
			const float flMinAdvanceWidth = bIsOneWay ? PLAYER_WIDTH * 0.5f : PLAYER_WIDTH * 0.75f;
			const float minAdvance = std::min(flHorizontalLength * 0.95f, std::max(flMinAdvanceWidth, flHorizontalLength * flMinAdvanceRatio));
			flApproach = std::max(flApproach, minAdvance);
			flApproach = std::min(flApproach, flHorizontalLength * 0.95f);
			tHint.m_flApproachDistance = std::max(flApproach, 0.f);

			tHint.m_vAdjustedPos = vCurrentPos + vDirection * tHint.m_flApproachDistance;
			tHint.m_vAdjustedPos.z = vCurrentPos.z;

			auto GetGroundZ = [&](const Vector& vProbe, float& flGroundZ) -> bool
			{
				CTraceFilterNavigation tFilter;
				CGameTrace tTrace{};

				Vector vStart = vProbe;
				vStart.z += PLAYER_CROUCHED_JUMP_HEIGHT;

				Vector vEnd = vProbe;
				vEnd.z -= std::max(flDropDistance + PLAYER_HEIGHT * 1.5f, PLAYER_HEIGHT * 2.f);

				SDK::Trace(vStart, vEnd, MASK_PLAYERSOLID_BRUSHONLY, &tFilter, &tTrace);
				if (!tTrace.DidHit())
					return false;

				flGroundZ = tTrace.endpos.z;
				return true;
			};

			const float flEdgeSearchStart = std::min(flHorizontalLength * 0.95f, std::max(tHint.m_flApproachDistance, PLAYER_WIDTH * 0.8f));
			const float flEdgeSearchEnd = std::max(PLAYER_WIDTH * 0.35f, flEdgeSearchStart - std::max(PLAYER_WIDTH * 2.2f, flHorizontalLength * 0.6f));
			const float flProbeStep = std::clamp(PLAYER_WIDTH * 0.45f, 14.f, 28.f);
			const int iProbeCount = 8;

			for (int i = 0; i <= iProbeCount; i++)
			{
				const float flT = iProbeCount > 0 ? static_cast<float>(i) / static_cast<float>(iProbeCount) : 0.f;
				const float flDist = flEdgeSearchStart + (flEdgeSearchEnd - flEdgeSearchStart) * std::clamp(flT, 0.f, 1.f);

				Vector vCandidate = vCurrentPos + vDirection * flDist;
				vCandidate.z = vCurrentPos.z;

				Vector vAhead = vCurrentPos + vDirection * std::min(flDist + flProbeStep, flHorizontalLength * 0.99f);
				vAhead.z = vCurrentPos.z;

				float flGroundHere = 0.f;
				if (!GetGroundZ(vCandidate, flGroundHere))
					continue;

				const float flLocalStepDown = vCurrentPos.z - flGroundHere;
				if (flLocalStepDown > PLAYER_JUMP_HEIGHT)
					continue;

				float flGroundAhead = 0.f;
				const bool bAheadGround = GetGroundZ(vAhead, flGroundAhead);
				const bool bHoleAhead = !bAheadGround;
				const bool bDropAhead = bAheadGround && (flGroundHere - flGroundAhead) >= std::max(16.f, flDropDistance * 0.35f);

				if (bHoleAhead || bDropAhead)
				{
					tHint.m_flApproachDistance = flDist;
					tHint.m_vAdjustedPos = vCandidate;
					break;
				}
			}
		}
	}
	else if (!bIsOneWay && flHeightDiff > 0.f && flHorizontalLength > 1.f)
	{
		Vector vDirection = vHorizontal / flHorizontalLength;

		// Step back slightly to help with climbing onto the next area.
		const float retreat = std::clamp(flHeightDiff * 0.35f, PLAYER_WIDTH * 0.3f, PLAYER_WIDTH);
		tHint.m_vAdjustedPos = vCurrentPos - vDirection * retreat;
		tHint.m_vAdjustedPos.z = vCurrentPos.z;
		tHint.m_vApproachDir = -vDirection;
		tHint.m_flApproachDistance = retreat;
	}

	return tHint;
}

NavPoints_t CMap::DeterminePoints(CNavArea* pCurrentArea, CNavArea* pNextArea, bool bIsOneWay)
{
	auto vCurrentCenter = pCurrentArea->m_vCenter;
	auto vNextCenter = pNextArea->m_vCenter;
	// Gets a vector on the edge of the current area that is as close as possible to the center of the next area
	auto vCurrentClosest = pCurrentArea->GetNearestPoint(Vector2D(vNextCenter.x, vNextCenter.y));
	// Do the same for the other area
	auto vNextClosest = pNextArea->GetNearestPoint(Vector2D(vCurrentCenter.x, vCurrentCenter.y));

	// Use one of them as a center point, the one that is either x or y alligned with a center
	// Of the areas. This will avoid walking into walls.
	auto vClosest = vCurrentClosest;

	// Determine if alligned, if not, use the other one as the center point
	if (vClosest.x != vCurrentCenter.x && vClosest.y != vCurrentCenter.y && vClosest.x != vNextCenter.x && vClosest.y != vNextCenter.y)
	{
		vClosest = vNextClosest;
		// Use the point closest to next_closest on the "original" mesh for z
		vClosest.z = pCurrentArea->GetNearestPoint(Vector2D(vNextClosest.x, vNextClosest.y)).z;
	}

	// Nearest point to center on "next", used for height checks
	auto vCenterNext = pNextArea->GetNearestPoint(Vector2D(vClosest.x, vClosest.y));

	return NavPoints_t(vCurrentCenter, vClosest, vCenterNext, vNextCenter);
}

bool CMap::IsOneWay(CNavArea* pFrom, CNavArea* pTo) const
{
	if (!pFrom || !pTo)
		return true;

	for (auto& tBackConnection : pTo->m_vConnections)
	{
		if (tBackConnection.m_pArea == pFrom)
			return false;
	}

	return true;
}

float CMap::EvaluateConnectionCost(CNavArea* pCurrentArea, CNavArea* pNextArea, const NavPoints_t& tPoints, const DropdownHint_t& tDropdown, int iTeam) const
{
	auto HorizontalDistance = [](const Vector& vStart, const Vector& vEnd) -> float
		{
			Vector vFlat = vEnd - vStart;
			vFlat.z = 0.f;
			float flLen = vFlat.Length();
			return flLen > 0.f ? flLen : 0.f;
		};

	float flForwardDistance = std::max(HorizontalDistance(tPoints.m_vCurrent, tPoints.m_vNext), 1.f);
	float flDeviationStart = HorizontalDistance(tPoints.m_vCurrent, tPoints.m_vCenter);
	float flDeviationEnd = HorizontalDistance(tPoints.m_vCenter, tPoints.m_vNext);
	float flHeightDiff = tPoints.m_vNext.z - tPoints.m_vCurrent.z;

	float flCost = flForwardDistance;
	flCost += flDeviationStart * 0.3f;
	flCost += flDeviationEnd * 0.2f;

	if (flHeightDiff > 0.f)
		flCost += flHeightDiff * 1.8f;
	else if (flHeightDiff < -8.f)
		flCost += std::abs(flHeightDiff) * 0.9f;

	if (tDropdown.m_bRequiresDrop)
	{
		flCost += tDropdown.m_flDropHeight * 2.2f;
		flCost += tDropdown.m_flApproachDistance * 0.45f;
	}
	else if (tDropdown.m_flApproachDistance > 0.f)
		flCost += tDropdown.m_flApproachDistance * 0.25f;

	Vector vForward = tPoints.m_vCenter - tPoints.m_vCurrent;
	Vector vForwardNext = tPoints.m_vNext - tPoints.m_vCenter;
	vForward.z = 0.f;
	vForwardNext.z = 0.f;
	float flLen1 = vForward.Length();
	float flLen2 = vForwardNext.Length();
	if (flLen1 > 1.f && flLen2 > 1.f)
	{
		vForward /= flLen1;
		vForwardNext /= flLen2;
		float flDot = std::clamp(vForward.Dot(vForwardNext), -1.f, 1.f);
		float flTurnPenalty = (1.f - flDot) * 30.f;
		flCost += flTurnPenalty;
	}

	Vector vAreaExtent = pNextArea->m_vSeCorner - pNextArea->m_vNwCorner;
	vAreaExtent.z = 0.f;
	float flAreaSize = vAreaExtent.Length();
	if (flAreaSize > 0.f)
		flCost -= std::clamp(flAreaSize * 0.01f, 0.f, 12.f);

	// that should work i guess, bot should get penalty for being inside its own spawn too
	const bool bRedSpawn = pNextArea->m_iTFAttributeFlags & TF_NAV_SPAWN_ROOM_RED;
	const bool bBlueSpawn = pNextArea->m_iTFAttributeFlags & TF_NAV_SPAWN_ROOM_BLUE;
	if (bRedSpawn || bBlueSpawn)
	{
		if (iTeam == TF_TEAM_RED && bBlueSpawn && !bRedSpawn)
			flCost += 220.f;
		else if (iTeam == TF_TEAM_BLUE && bRedSpawn && !bBlueSpawn)
			flCost += 220.f;
		else if (bRedSpawn && bBlueSpawn)
			flCost += 60.f;
		else
			flCost += 40.f;
	}

	if (pNextArea->m_iAttributeFlags & NAV_MESH_AVOID)
		flCost += 100000.f;

	if (pNextArea->m_iAttributeFlags & NAV_MESH_CROUCH)
		flCost += flForwardDistance * 5.f;

	const bool bHasReturnPath = HasDirectConnection(pNextArea, pCurrentArea);
	const bool bIrreversibleEnter = !bHasReturnPath;
	int iForwardExitCount = 0;
	for (const auto& tExit : pNextArea->m_vConnections)
	{
		auto pExitArea = tExit.m_pArea;
		if (!pExitArea || pExitArea == pNextArea || pExitArea == pCurrentArea || !IsAreaValid(pExitArea))
			continue;
		iForwardExitCount++;
	}

	if (iForwardExitCount == 0)
		flCost += bIrreversibleEnter ? 900.f : 220.f;
	else if (iForwardExitCount == 1)
		flCost += 90.f;

	if (bIrreversibleEnter)
	{
		flCost += 160.f;

		if (tDropdown.m_bRequiresDrop)
			flCost += std::clamp(tDropdown.m_flDropHeight * 3.0f, 120.f, 420.f);

		if (pNextArea->m_iAttributeFlags & NAV_MESH_NO_JUMP)
			flCost += 220.f;
	}

	return std::max(flCost, 1.f);
}

float CMap::GetBlacklistPenalty(const BlacklistReason_t& tReason) const
{
	if (m_bIgnoreSentryBlacklist)
	{
		switch (tReason.m_eValue)
		{
		case BlacklistReasonEnum::Sentry:
		case BlacklistReasonEnum::SentryMedium:
		case BlacklistReasonEnum::SentryLow:
			return 0.f;
		default: break;
		}
	}

	switch (tReason.m_eValue)
	{
	case BlacklistReasonEnum::Sentry:
		return 3500.f;
	case BlacklistReasonEnum::EnemyInvuln:
		return 1500.f;
	case BlacklistReasonEnum::Sticky:
		return 1000.f;
	case BlacklistReasonEnum::SentryMedium:
		return 800.f;
	case BlacklistReasonEnum::SentryLow:
		return 400.f;
	case BlacklistReasonEnum::EnemyDormant:
		return 200.f;
	case BlacklistReasonEnum::EnemyNormal:
		return 300.f;
	case BlacklistReasonEnum::BadBuildSpot:
		return 100.f;
	default:
		return 0.f;
	}
}

bool CMap::ShouldOverrideBlacklist(const BlacklistReason_t& tCurrent, const BlacklistReason_t& tIncoming) const
{
	if (tIncoming.m_eValue == tCurrent.m_eValue)
		return true;

	const float flCurrent = GetBlacklistPenalty(tCurrent);
	const float flIncoming = GetBlacklistPenalty(tIncoming);

	if (!std::isfinite(flIncoming))
		return true;
	if (!std::isfinite(flCurrent))
		return false;

	return flIncoming >= flCurrent;
}

void CMap::CollectAreasAround(const Vector& vOrigin, float flRadius, std::vector<CNavArea*>& vOutAreas)
{
	vOutAreas.clear();

	CNavArea* pSeedArea = FindClosestNavArea(vOrigin, false);
	if (!pSeedArea)
		return;

	const float flRadiusSqr = flRadius * flRadius;
	const float flExpansionLimit = flRadiusSqr * 4.f;

	std::queue<std::pair<CNavArea*, float>> qAreas;
	std::unordered_set<CNavArea*> setVisited;

	float flSeedDist = (pSeedArea->m_vCenter - vOrigin).LengthSqr();
	qAreas.emplace(pSeedArea, flSeedDist);
	setVisited.insert(pSeedArea);

	int iLoopLimit = 2048;
	while (!qAreas.empty() && iLoopLimit-- > 0)
	{
		auto [tArea, flDist] = qAreas.front();
		qAreas.pop();

		if (flDist <= flRadiusSqr)
			vOutAreas.push_back(tArea);

		if (flDist > flExpansionLimit)
			continue;

		for (auto& tConnection : tArea->m_vConnections)
		{
			CNavArea* pNextArea = tConnection.m_pArea;
			if (!pNextArea)
				continue;

			float flNextDist = (pNextArea->m_vCenter - vOrigin).LengthSqr();
			if (flNextDist > flExpansionLimit)
				continue;

			if (setVisited.insert(pNextArea).second)
				qAreas.emplace(pNextArea, flNextDist);
		}
	}

	if (vOutAreas.empty())
		vOutAreas.push_back(pSeedArea);
}

void CMap::ApplyBlacklistAround(const Vector& vOrigin, float flRadius, const BlacklistReason_t& tReason, unsigned int nMask, bool bRequireLOS)
{
	std::lock_guard lock(m_mutex);
	std::vector<CNavArea*> vCandidates;
	CollectAreasAround(vOrigin, flRadius + HALF_PLAYER_WIDTH, vCandidates);
	if (vCandidates.empty())
		return;

	const float flRadiusSqr = flRadius * flRadius;

	for (auto pArea : vCandidates)
	{
		if (!pArea)
			continue;

		Vector vAreaPoint = pArea->m_vCenter;
		vAreaPoint.z += PLAYER_CROUCHED_JUMP_HEIGHT;
		if (vOrigin.DistToSqr(vAreaPoint) > flRadiusSqr)
			continue;

		if (bRequireLOS && !F::NavEngine.IsVectorVisibleNavigation(vOrigin, vAreaPoint, nMask))
			continue;

		auto itEntry = m_mFreeBlacklist.find(pArea);
		if (itEntry != m_mFreeBlacklist.end())
		{
			if (itEntry->second.m_eValue == tReason.m_eValue)
			{
				itEntry->second.m_iTime = std::max(itEntry->second.m_iTime, tReason.m_iTime);
				continue;
			}

			if (!ShouldOverrideBlacklist(itEntry->second, tReason))
				continue;
		}

		m_mFreeBlacklist[pArea] = tReason;
	}
}

CNavArea* CMap::FindClosestNavArea(const Vector& vPos, bool bLocalOrigin)
{
	std::lock_guard lock(m_mutex);
	float flBestOverlapScore = FLT_MAX;
	CNavArea* pBestOverlapArea = nullptr;

	float flBestScore = FLT_MAX;
	CNavArea* pBestArea = nullptr;

	for (auto& tArea : m_navfile.m_vAreas)
	{
		const bool bOverlapping = tArea.IsOverlapping(vPos);
		const float flAreaZ = tArea.GetZ(vPos.x, vPos.y);
		const float flVerticalToArea = std::fabs(flAreaZ - vPos.z);

		if (bOverlapping)
		{
			float flOverlapScore = flVerticalToArea;
			if (bLocalOrigin)
			{
				if (vPos.z < (tArea.m_flMinZ - PLAYER_CROUCHED_JUMP_HEIGHT))
					flOverlapScore += PLAYER_HEIGHT;
				if (vPos.z > (tArea.m_flMaxZ + PLAYER_CROUCHED_JUMP_HEIGHT))
					flOverlapScore += PLAYER_HEIGHT * 0.5f;
			}

			if (flOverlapScore < flBestOverlapScore)
			{
				flBestOverlapScore = flOverlapScore;
				pBestOverlapArea = &tArea;
			}
		}

		Vector vDelta = tArea.m_vCenter - vPos;
		const float flPlanarDistSqr = vDelta.x * vDelta.x + vDelta.y * vDelta.y;
		const float flScore = flPlanarDistSqr + (flVerticalToArea * flVerticalToArea * 6.f);
		if (flScore < flBestScore)
		{
			flBestScore = flScore;
			pBestArea = &tArea;
		}
	}

	if (pBestOverlapArea)
		return pBestOverlapArea;

	return pBestArea;
}

void CMap::UpdateIgnores(CTFPlayer* pLocal)
{
	static Timer tUpdateTime;
	if (!tUpdateTime.Run(1.f))
		return;

	// Clear the blacklist
	F::NavEngine.ClearFreeBlacklist(BlacklistReason_t(BlacklistReasonEnum::Sentry));
	F::NavEngine.ClearFreeBlacklist(BlacklistReason_t(BlacklistReasonEnum::SentryMedium));
	F::NavEngine.ClearFreeBlacklist(BlacklistReason_t(BlacklistReasonEnum::SentryLow));
	F::NavEngine.ClearFreeBlacklist(BlacklistReason_t(BlacklistReasonEnum::EnemyInvuln));
	if (Vars::Misc::Movement::NavBot::Blacklist.Value & Vars::Misc::Movement::NavBot::BlacklistEnum::Players)
	{
		constexpr float flInvulnerableRadius = 1000.0f;
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
		{
			auto pPlayer = pEntity->As<CTFPlayer>();
			if (!pPlayer->IsAlive())
				continue;

			if (!pPlayer->IsInvulnerable() || (pLocal->m_iClass() == TF_CLASS_HEAVY && G::SavedDefIndexes[SLOT_MELEE] == Heavy_t_TheHolidayPunch))
				continue;

			Vector vPlayerOrigin;
			if (!F::BotUtils.GetDormantOrigin(pPlayer->entindex(), &vPlayerOrigin))
				continue;

			vPlayerOrigin.z += PLAYER_CROUCHED_JUMP_HEIGHT;
			ApplyBlacklistAround(vPlayerOrigin, flInvulnerableRadius, BlacklistReason_t(BlacklistReasonEnum::EnemyInvuln), MASK_SHOT, true);
		}
	}

	if (Vars::Misc::Movement::NavBot::Blacklist.Value & Vars::Misc::Movement::NavBot::BlacklistEnum::Sentries)
	{
		constexpr float flHighDangerRange = 900.0f;
		constexpr float flMediumDangerRange = 1050.0f;
		constexpr float flLowDangerRange = 1200.0f;

		for (auto pEntity : H::Entities.GetGroup(EntityEnum::BuildingEnemy))
		{
			auto pBuilding = pEntity->As<CBaseObject>();
			if (pBuilding->GetClassID() != ETFClassID::CObjectSentrygun)
				continue;

			auto pSentry = pBuilding->As<CObjectSentrygun>();
			if (pSentry->m_iState() == SENTRY_STATE_INACTIVE)
				continue;

			bool bStrongClass = pLocal->m_iClass() == TF_CLASS_HEAVY || pLocal->m_iClass() == TF_CLASS_SOLDIER;
			if (bStrongClass && (pSentry->m_bMiniBuilding() || pSentry->m_iUpgradeLevel() == 1))
				continue;

			int iBullets = pSentry->m_iAmmoShells();
			int iRockets = pSentry->m_iAmmoRockets();
			if (iBullets == 0 && (pSentry->m_iUpgradeLevel() != 3 || iRockets == 0))
				continue;

			if ((!pSentry->m_bCarryDeploy() && pSentry->m_bBuilding()) || pSentry->m_bPlacing() || pSentry->m_bHasSapper())
				continue;

			Vector vSentryOrigin;
			if (!F::BotUtils.GetDormantOrigin(pSentry->entindex(), &vSentryOrigin))
				continue;

			vSentryOrigin.z += PLAYER_CROUCHED_JUMP_HEIGHT;

			ApplyBlacklistAround(vSentryOrigin, flHighDangerRange, BlacklistReason_t(BlacklistReasonEnum::Sentry), MASK_SHOT | CONTENTS_GRATE, true);
			ApplyBlacklistAround(vSentryOrigin, flMediumDangerRange, BlacklistReason_t(BlacklistReasonEnum::SentryMedium), MASK_SHOT | CONTENTS_GRATE, true);
			if (!bStrongClass)
				ApplyBlacklistAround(vSentryOrigin, flLowDangerRange, BlacklistReason_t(BlacklistReasonEnum::SentryLow), MASK_SHOT | CONTENTS_GRATE, true);
		}
	}

	if (Vars::Misc::Movement::NavBot::Blacklist.Value & Vars::Misc::Movement::NavBot::BlacklistEnum::Stickies)
	{
		const auto iBlacklistEndTimestamp = TICKCOUNT_TIMESTAMP(Vars::Misc::Movement::NavEngine::StickyIgnoreTime.Value);
		const float flStickyRadius = 130.0f + HALF_PLAYER_WIDTH;

		for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldProjectile))
		{
			auto pSticky = pEntity->As<CTFGrenadePipebombProjectile>();
			if (pSticky->GetClassID() != ETFClassID::CTFGrenadePipebombProjectile ||
				pSticky->m_iTeamNum() == pLocal->m_iTeamNum() ||
				pSticky->m_iType() != TF_GL_MODE_REMOTE_DETONATE ||
				pSticky->IsDormant() ||
				!pSticky->m_vecVelocity().IsZero(1.f))
				continue;

			Vector vStickyOrigin = pSticky->GetAbsOrigin();
			vStickyOrigin.z += PLAYER_JUMP_HEIGHT / 2.0f;

			ApplyBlacklistAround(vStickyOrigin, flStickyRadius, BlacklistReason_t(BlacklistReasonEnum::Sticky, iBlacklistEndTimestamp), MASK_SHOT, true);
		}
	}

	static size_t uPreviousBlacklistSize = 0;
	std::erase_if(m_mFreeBlacklist, [](const auto& entry) { return entry.second.m_iTime && entry.second.m_iTime < I::GlobalVars->tickcount; });
	std::erase_if(m_mVischeckCache, [](const auto& entry) { return entry.second.m_iExpireTick < I::GlobalVars->tickcount; });
	std::erase_if(m_mConnectionStuckTime, [](const auto& entry) { return entry.second.m_iExpireTick < I::GlobalVars->tickcount; });

	bool bErased = uPreviousBlacklistSize != m_mFreeBlacklist.size();
	uPreviousBlacklistSize = m_mFreeBlacklist.size();
}
