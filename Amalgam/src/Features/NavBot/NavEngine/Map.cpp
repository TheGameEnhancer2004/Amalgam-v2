#include "NavEngine.h"
#include "../BotUtils.h"

void CMap::AdjacentCost(void* pArea, std::vector<micropather::StateCost>* pAdjacent)
{
	CNavArea& tArea = *reinterpret_cast<CNavArea*>(pArea);
	for (NavConnect_t& tConnection : tArea.m_vConnections)
	{
		// An area being entered twice means it is blacklisted from entry entirely
		auto tConnectionKey = std::pair<CNavArea*, CNavArea*>(tConnection.m_pArea, tConnection.m_pArea);

		// Entered and marked bad?
		if (m_mVischeckCache.count(tConnectionKey) && m_mVischeckCache[tConnectionKey].m_eVischeckState == VischeckStateEnum::NotVisible)
			continue;

		// If the extern blacklist is running, ensure we don't try to use a bad area
		if (!m_bFreeBlacklistBlocked && std::any_of(m_mFreeBlacklist.begin(), m_mFreeBlacklist.end(), [&](const auto& entry) { return entry.first == tConnection.m_pArea; }))
			continue;

		auto tPoints = DeterminePoints(&tArea, tConnection.m_pArea);
		const auto tDropdown = HandleDropdown(tPoints.m_vCenter, tPoints.m_vNext);
		tPoints.m_vCenter = tDropdown.m_vAdjustedPos;

		float flHeightDiff = tPoints.m_vCenterNext.z - tPoints.m_vCenter.z;

		// Too high for us to jump!
		if (flHeightDiff > PLAYER_CROUCHED_JUMP_HEIGHT)
			continue;

		tPoints.m_vCurrent.z += PLAYER_CROUCHED_JUMP_HEIGHT;
		tPoints.m_vCenter.z += PLAYER_CROUCHED_JUMP_HEIGHT;
		tPoints.m_vNext.z += PLAYER_CROUCHED_JUMP_HEIGHT;

		const auto tKey = std::pair<CNavArea*, CNavArea*>(&tArea, tConnection.m_pArea);
		if (m_mVischeckCache.count(tKey))
		{
			if (m_mVischeckCache[tKey].m_eVischeckState)
			{
				const float cost = tConnection.m_pArea->m_vCenter.DistTo(tArea.m_vCenter);
				pAdjacent->push_back(micropather::StateCost{ reinterpret_cast<void*>(tConnection.m_pArea), cost });
			}
		}
		else
		{
			// Check if there is direct line of sight
			if (F::NavEngine.IsPlayerPassableNavigation(tPoints.m_vCurrent, tPoints.m_vCenter) &&
				F::NavEngine.IsPlayerPassableNavigation(tPoints.m_vCenter, tPoints.m_vNext))
			{
				m_mVischeckCache[tKey] = CachedConnection_t{ TICKCOUNT_TIMESTAMP(60), VischeckStateEnum::Visible };

				const float flCost = tPoints.m_vNext.DistTo(tPoints.m_vCurrent);
				pAdjacent->push_back(micropather::StateCost{ reinterpret_cast<void*>(tConnection.m_pArea), flCost });
			}
			else
				m_mVischeckCache[tKey] = CachedConnection_t{ TICKCOUNT_TIMESTAMP(60), VischeckStateEnum::NotVisible };
		}
	}
}

DropdownHint_t CMap::HandleDropdown(const Vector& vCurrentPos, const Vector& vNextPos)
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
			Vector vDirection = vHorizontal / flHorizontalLength;

			// Distance to move forward before dropping. Favour wider moves for larger drops.
			const float desiredAdvance = std::clamp(flDropDistance * 0.4f, PLAYER_WIDTH * 0.75f, PLAYER_WIDTH * 2.5f);
			const float flMaxAdvance = std::max(flHorizontalLength - kEdgePadding, 0.f);
			float flApproach = desiredAdvance;
			if (flMaxAdvance > 0.f)
				flApproach = std::min(flApproach, flMaxAdvance);
			else
				flApproach = std::min(flApproach, flHorizontalLength * 0.8f);

			const float minAdvance = std::min(flHorizontalLength * 0.95f, std::max(PLAYER_WIDTH * 0.6f, flHorizontalLength * 0.5f));
			flApproach = std::max(flApproach, minAdvance);
			flApproach = std::min(flApproach, flHorizontalLength * 0.95f);
			tHint.m_flApproachDistance = std::max(flApproach, 0.f);

			tHint.m_vAdjustedPos = vCurrentPos + vDirection * tHint.m_flApproachDistance;
			tHint.m_vAdjustedPos.z = vCurrentPos.z;
			tHint.m_bRequiresDrop = true;
			tHint.m_flDropHeight = flDropDistance;
			tHint.m_vApproachDir = vDirection;
		}
	}
	else if (flHeightDiff > 0.f && flHorizontalLength > 1.f)
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

NavPoints_t CMap::DeterminePoints(CNavArea* pCurrentArea, CNavArea* pNextArea)
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

	// If safepathing is enabled, adjust points to stay more centered and avoid corners
	if (Vars::Misc::Movement::NavEngine::SafePathing.Value)
	{
		// Move points more towards the center of the areas
		//Vector vToNext = (vNextCenter - vCurrentCenter);
		//vToNext.z = 0.0f;
		//vToNext.Normalize();

		// Calculate center point as a weighted average between area centers
		// Use a 60/40 split to favor the current area more
		vClosest = vCurrentCenter + (vNextCenter - vCurrentCenter) * 0.4f;

		// Add extra safety margin near corners
		float flCornerMargin = PLAYER_WIDTH * 0.75f;

		// Check if we're near a corner by comparing distances to area edges
		bool bNearCorner = false;
		Vector vCurrentMins = pCurrentArea->m_vNwCorner; // Northwest corner
		Vector vCurrentMaxs = pCurrentArea->m_vSeCorner; // Southeast corner

		if (vClosest.x - vCurrentMins.x < flCornerMargin ||
			vCurrentMaxs.x - vClosest.x < flCornerMargin ||
			vClosest.y - vCurrentMins.y < flCornerMargin ||
			vCurrentMaxs.y - vClosest.y < flCornerMargin)
			bNearCorner = true;

		// If near corner, move point more towards center
		if (bNearCorner)
			vClosest = vClosest + (vCurrentCenter - vClosest).Normalized() * flCornerMargin;

		// Ensure the point is within the current area
		vClosest = pCurrentArea->GetNearestPoint(Vector2D(vClosest.x, vClosest.y));
	}

	// Nearest point to center on "next", used for height checks
	auto vCenterNext = pNextArea->GetNearestPoint(Vector2D(vClosest.x, vClosest.y));

	return NavPoints_t(vCurrentCenter, vClosest, vCenterNext, vNextCenter);
}

CNavArea* CMap::FindClosestNavArea(const Vector& vPos, bool bLocalOrigin)
{
	float flOverallBestDist = FLT_MAX, flBestDist = FLT_MAX;
	Vector vCorrected = vPos; vCorrected.z += PLAYER_CROUCHED_JUMP_HEIGHT;

	// If multiple candidates for LocalNav have been found, pick the closest
	CNavArea* pOverallBestArea = nullptr, * pBestArea = nullptr;
	for (auto& tArea : m_navfile.m_vAreas)
	{
		// Marked bad, do not use if local origin
		if (bLocalOrigin)
		{
			auto tKey = std::pair<CNavArea*, CNavArea*>(&tArea, &tArea);
			if (m_mVischeckCache.count(tKey) && m_mVischeckCache[tKey].m_eVischeckState == VischeckStateEnum::NotVisible)
				continue;
		}

		float flDist = tArea.m_vCenter.DistToSqr(vPos);
		if (flDist < flBestDist)
		{
			flBestDist = flDist;
			pBestArea = &tArea;
		}

		if (flOverallBestDist < flDist)
			continue;

		auto vCenterCorrected = tArea.m_vCenter;
		vCenterCorrected.z += PLAYER_CROUCHED_JUMP_HEIGHT;

		// Check if we are within x and y bounds of an area
		if (!tArea.IsOverlapping(vPos) || !F::NavEngine.IsVectorVisibleNavigation(vCorrected, vCenterCorrected))
			continue;

		flOverallBestDist = flDist;
		pOverallBestArea = &tArea;

		// Early return if the area is overlapping and visible
		if (flOverallBestDist == flBestDist)
			return pOverallBestArea;
	}

	return pOverallBestArea ? pOverallBestArea : pBestArea;
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
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
		{
			if (!pEntity->IsPlayer())
				continue;

			auto pPlayer = pEntity->As<CTFPlayer>();
			if (!pPlayer->IsAlive())
				continue;

			// Skip vulnerable players or invulnerable too if we have holiday punch equipped
			if (!pPlayer->IsInvulnerable() || (pLocal->m_iClass() == TF_CLASS_HEAVY && G::SavedDefIndexes[SLOT_MELEE] == Heavy_t_TheHolidayPunch))
				continue;

			// Get origin of the player
			Vector vPlayerOrigin;
			if (!F::BotUtils.GetDormantOrigin(pPlayer->entindex(), vPlayerOrigin))
				continue;

			vPlayerOrigin.z += PLAYER_CROUCHED_JUMP_HEIGHT;

			for (auto& tArea : m_navfile.m_vAreas)
			{
				Vector vArea = tArea.m_vCenter;
				vArea.z += PLAYER_CROUCHED_JUMP_HEIGHT;

				// Check if player is close to the area and can potentially hurt us
				float flDistSqr = vPlayerOrigin.DistToSqr(vArea);
				if (flDistSqr > pow(1000.0f, 2) || !F::NavEngine.IsVectorVisibleNavigation(vPlayerOrigin, vArea, MASK_SHOT))
					continue;

				m_mFreeBlacklist[&tArea] = BlacklistReasonEnum::EnemyInvuln;
			}
		}
	}

	if (Vars::Misc::Movement::NavBot::Blacklist.Value & Vars::Misc::Movement::NavBot::BlacklistEnum::Sentries)
	{
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::BuildingEnemy))
		{
			if (!pEntity->IsBuilding())
				continue;

			auto pBuilding = pEntity->As<CBaseObject>();
			if (pBuilding->GetClassID() != ETFClassID::CObjectSentrygun)
				continue;

			auto pSentry = pBuilding->As<CObjectSentrygun>();

			// Should we even ignore the sentry?
			if (pSentry->m_iState() == SENTRY_STATE_INACTIVE)
				continue;

			// Soldier/Heavy do not care about Level 1 or mini sentries
			bool bStrongClass = pLocal->m_iClass() == TF_CLASS_HEAVY || pLocal->m_iClass() == TF_CLASS_SOLDIER;
			if (bStrongClass && (pSentry->m_bMiniBuilding() || pSentry->m_iUpgradeLevel() == 1))
				continue;

			int iBullets = pSentry->m_iAmmoShells();
			int iRockets = pSentry->m_iAmmoRockets();
			if (iBullets == 0 && (pSentry->m_iUpgradeLevel() != 3 || iRockets == 0))
				continue;

			// It's still building/being sapped, ignore.
			// Unless it just was deployed from a carry, then it's dangerous
			if ((!pSentry->m_bCarryDeploy() && pSentry->m_bBuilding()) ||
				pSentry->m_bPlacing() || pSentry->m_bHasSapper())
				continue;

			// Get origin of the sentry
			Vector vSentryOrigin;
			if (!F::BotUtils.GetDormantOrigin(pSentry->entindex(), vSentryOrigin))
				continue;

			// Add some height to imitate sentry view offset
			vSentryOrigin.z += PLAYER_CROUCHED_JUMP_HEIGHT;

			// Define range tiers for sentry danger
			constexpr float flHighDangerRange = 900.0f; // Full blacklist
			constexpr float flMediumDangerRange = 1050.0f; // Caution range (try to avoid)
			constexpr float flLowDangerRange = 1200.0f; // Safe for some classes

			// Actual building check
			for (auto& tArea : m_navfile.m_vAreas)
			{
				Vector vArea = tArea.m_vCenter;
				vArea.z += PLAYER_CROUCHED_JUMP_HEIGHT;

				float flDistSqr = vSentryOrigin.DistToSqr(vArea);
				bool bHighDanger = flDistSqr <= pow(flHighDangerRange + HALF_PLAYER_WIDTH, 2);
				bool bMediumDanger = !bHighDanger && flDistSqr <= pow(flMediumDangerRange + HALF_PLAYER_WIDTH, 2);
				bool bLowDanger = !bMediumDanger && !bStrongClass && flDistSqr <= pow(flLowDangerRange + HALF_PLAYER_WIDTH, 2);

				BlacklistReasonEnum::BlacklistReasonEnum eReason;
				if (bLowDanger || bMediumDanger)
				{
					// Only blacklist if not already blacklisted and sentry can see this area
					if (m_mFreeBlacklist.find(&tArea) != m_mFreeBlacklist.end() || !F::NavEngine.IsVectorVisibleNavigation(vSentryOrigin, vArea, MASK_SHOT))
						continue;

					eReason = bLowDanger ? BlacklistReasonEnum::SentryLow : BlacklistReasonEnum::SentryMedium;
				}
				else if (bHighDanger)
				{
					// Check if sentry can see this area
					if (!F::NavEngine.IsVectorVisibleNavigation(vSentryOrigin, vArea, MASK_SHOT))
						continue;

					// High danger - full blacklist
					eReason = BlacklistReasonEnum::Sentry;
				}
				else
					continue;

				m_mFreeBlacklist[&tArea] = eReason;
			}
		}
	}
	
	if (Vars::Misc::Movement::NavBot::Blacklist.Value & Vars::Misc::Movement::NavBot::BlacklistEnum::Stickies)
	{
		const auto iBlacklistEndTimestamp = TICKCOUNT_TIMESTAMP(Vars::Misc::Movement::NavEngine::StickyIgnoreTime.Value);
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldProjectile))
		{
			auto pSticky = pEntity->As<CTFGrenadePipebombProjectile>();
			if (pSticky->GetClassID() != ETFClassID::CTFGrenadePipebombProjectile || pSticky->m_iTeamNum() == pLocal->m_iTeamNum() ||
				pSticky->m_iType() != TF_GL_MODE_REMOTE_DETONATE || pSticky->IsDormant() || !pSticky->m_vecVelocity().IsZero(1.f))
				continue;

			auto vStickyOrigin = pSticky->GetAbsOrigin();
			// Make sure the sticky doesn't vischeck from inside the floor
			vStickyOrigin.z += PLAYER_JUMP_HEIGHT / 2.0f;

			for (auto& tArea : m_navfile.m_vAreas)
			{
				Vector vArea = tArea.m_vCenter;
				vArea.z += PLAYER_JUMP_HEIGHT;

				// Out of range or cant see the area
				if (vStickyOrigin.DistToSqr(vArea) > pow(130.0f + HALF_PLAYER_WIDTH, 2) || !F::NavEngine.IsVectorVisibleNavigation(vStickyOrigin, vArea, MASK_SHOT))
					continue;

				// Blacklist because it's in range of the sticky, but stickies make no noise (cant get the dormant origin), so blacklist it for a specific timeframe
				m_mFreeBlacklist[&tArea] = { BlacklistReasonEnum::Sticky, iBlacklistEndTimestamp };
				
			}
		}
	}

	static size_t uPreviousBlacklistSize = 0;
	std::erase_if(m_mFreeBlacklist, [](const auto& entry) { return entry.second.m_iTime && entry.second.m_iTime < I::GlobalVars->tickcount; });
	std::erase_if(m_mVischeckCache, [](const auto& entry) { return entry.second.m_iExpireTick < I::GlobalVars->tickcount; });
	std::erase_if(m_mConnectionStuckTime, [](const auto& entry) { return entry.second.m_iExpireTick < I::GlobalVars->tickcount; });

	bool bErased = uPreviousBlacklistSize != m_mFreeBlacklist.size();
	uPreviousBlacklistSize = m_mFreeBlacklist.size();

	if (bErased)
		m_pather.Reset();
}

void CMap::UpdateRespawnRooms()
{
	std::vector<CFuncRespawnRoom*> vFoundEnts;
	CServerBaseEntity* pPrevEnt = nullptr;
	while (true)
	{
		auto pEntity = I::ServerTools->FindEntityByClassname(pPrevEnt, "func_respawnroom");
		if (!pEntity)
			break;

		pPrevEnt = pEntity;

		vFoundEnts.push_back(pEntity->As<CFuncRespawnRoom>());
	}

	if (vFoundEnts.empty())
	{
		if (Vars::Debug::Logging.Value)
			SDK::Output("CMap::UpdateRespawnRooms", "Couldn't find any room entities", { 255, 50, 50 }, OUTPUT_CONSOLE | OUTPUT_DEBUG | OUTPUT_TOAST | OUTPUT_MENU);
		return;
	}

	for (auto pRespawnRoom : vFoundEnts)
	{
		if (!pRespawnRoom)
			continue;

		static Vector vStepHeight(0.0f, 0.0f, 18.0f);
		for (auto& tArea : m_navfile.m_vAreas)
		{
			if (pRespawnRoom->PointIsWithin(tArea.m_vCenter + vStepHeight)
				|| pRespawnRoom->PointIsWithin(tArea.m_vNwCorner + vStepHeight)
				|| pRespawnRoom->PointIsWithin(tArea.GetNeCorner() + vStepHeight)
				|| pRespawnRoom->PointIsWithin(tArea.GetSwCorner() + vStepHeight)
				|| pRespawnRoom->PointIsWithin(tArea.m_vSeCorner + vStepHeight))
			{
				uint32_t uFlags = pRespawnRoom->m_iTeamNum() == TF_TEAM_BLUE ? TF_NAV_SPAWN_ROOM_BLUE : TF_NAV_SPAWN_ROOM_RED;
				if (!(tArea.m_iTFAttributeFlags & uFlags))
					tArea.m_iTFAttributeFlags |= uFlags;
			}
		}
	}
}