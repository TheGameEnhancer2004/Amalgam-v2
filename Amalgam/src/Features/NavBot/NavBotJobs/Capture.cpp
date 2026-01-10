#include "Capture.h"
#include "../NavEngine/NavEngine.h"
#include "../../Players/PlayerUtils.h"
#include "../NavEngine/Controllers/CPController/CPController.h"
#include "../NavEngine/Controllers/FlagController/FlagController.h"
#include "../NavEngine/Controllers/PLController/PLController.h"
#include "../NavEngine/Controllers/HaarpController/HaarpController.h"
#include "../NavEngine/Controllers/Controller.h"
#include "../../Misc/NamedPipe/NamedPipe.h"

bool CNavBotCapture::ShouldAvoidPlayer(int iIndex)
{
#ifdef TEXTMODE
	if (auto pResource = H::Entities.GetResource(); pResource && F::NamedPipe.IsLocalBot(pResource->m_iAccountID(iIndex)))
		return false;
#endif

	return !F::PlayerUtils.IsIgnored(iIndex);
}

bool CNavBotCapture::GetCtfGoal(CTFPlayer* pLocal, int iOurTeam, int iEnemyTeam, Vector& vOut)
{
	m_sCaptureStatus = L"";
	if (F::GameObjectiveController.m_bHaarp)
	{
		if (iOurTeam == TF_TEAM_BLUE)
		{
			if (F::HaarpController.GetCapturePos(vOut))
			{
				m_sCaptureStatus = F::HaarpController.m_sHaarpStatus;
				return true;
			}
			CCaptureFlag* pBestFlag = nullptr;
			float flBestDist = FLT_MAX;
			Vector vLocalOrigin = pLocal->GetAbsOrigin();
			for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldObjective))
			{
				if (pEntity->GetClassID() != ETFClassID::CCaptureFlag)
					continue;
				auto pFlag = pEntity->As<CCaptureFlag>();
				Vector vPos = pFlag->GetAbsOrigin();
				float flDist = vLocalOrigin.DistToSqr(vPos);
				if (flDist < flBestDist)
				{
					flBestDist = flDist;
					pBestFlag = pFlag;
				}
			}
			if (pBestFlag)
			{
				m_sCaptureStatus = L"Flag";
				vOut = pBestFlag->GetAbsOrigin();
				return true;
			}
		}
		else
		{
			if (F::HaarpController.GetDefensePos(vOut))
			{
				m_sCaptureStatus = F::HaarpController.m_sHaarpStatus;
				return true;
			}
		}
		// If HaarpController didn't provide a goal (e.g. teammate has flag), fall through to standard CTF logic for assistance
	}

	Vector vPosition;
	if (!F::FlagController.GetPosition(iEnemyTeam, vPosition))
	{
		if (!F::FlagController.GetPosition(0, vPosition)) // Try neutral flag
			return false;
		iEnemyTeam = 0; // Use neutral team if found
	}

	// Get Flag related information
	auto iStatus = F::FlagController.GetStatus(iEnemyTeam);
	auto iCarrierIdx = F::FlagController.GetCarrier(iEnemyTeam);

	// CTF is the current capture type
	if (iStatus == TF_FLAGINFO_STOLEN)
	{
		if (iCarrierIdx == pLocal->entindex())
		{
			// Return our capture point location
			if (F::FlagController.GetSpawnPosition(iOurTeam, vPosition))
			{
				m_sCaptureStatus = L"CP";
				vOut = vPosition;
				return true;
			}
		}
		// Assist with capturing
		else if (Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::HelpCaptureObjectives)
		{
			if (F::BotUtils.ShouldAssist(pLocal, iCarrierIdx))
			{
				m_sCaptureStatus = L"Assist";
				// Stay slightly behind and to the side to avoid blocking
				Vector vOffset(40.0f, 40.0f, 0.0f);
				vPosition -= vOffset;
				vOut = vPosition;
				return true;
			}
		}
		return false;
	}

	// Get the flag if not taken by us already
	m_sCaptureStatus = L"Flag";
	vOut = vPosition;

	return true;
}

bool CNavBotCapture::GetPayloadGoal(const Vector vLocalOrigin, int iOurTeam, Vector& vOut)
{
	m_sCaptureStatus = L"Payload";

	Vector vPosition;
	if (!F::PLController.GetClosestPayload(vLocalOrigin, iOurTeam, vPosition))
	{
		auto& tCache = m_aPayloadCache[iOurTeam - TF_TEAM_RED];
		if (I::GlobalVars->curtime - tCache.flTime < 60.0f)
			vPosition = tCache.vPos;
		else
			return false;
	}
	else
	{
		auto& tCache = m_aPayloadCache[iOurTeam - TF_TEAM_RED];
		tCache.vPos = vPosition;
		tCache.flTime = I::GlobalVars->curtime;
	}

	int iTeammatesNearCart = 0;
	constexpr float flCartRadius = 90.0f;

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerTeam))
	{
		if (pEntity->IsDormant() || pEntity->entindex() == I::EngineClient->GetLocalPlayer())
			continue;

		auto pTeammate = pEntity->As<CTFPlayer>();
		if (!pTeammate->IsAlive())
			continue;

		if (pTeammate->GetAbsOrigin().DistTo(vPosition) <= flCartRadius)
			iTeammatesNearCart++;
	}

	const float flTargetDist = 20.0f;
	const int iTotalUnits = iTeammatesNearCart + 1;
	const float flAngle = PI * 2 * (float)(I::EngineClient->GetLocalPlayer() % iTotalUnits) / iTotalUnits;
	const Vector vOffset(cos(flAngle) * flTargetDist, sin(flAngle) * flTargetDist, 0.0f);
	Vector vAdjustedPos = vPosition + vOffset;

	CNavArea* pCartArea = nullptr;

	constexpr float flPlanarTolerance = 90.0f;
	constexpr float flMaxHeightDiff = 60.0f;
	const Vector vCartPos = vPosition;

	auto IsAreaUsable = [&](CNavArea* pArea) -> bool
		{
			if (!pArea)
				return false;

			const float flAreaZ = pArea->GetZ(vCartPos.x, vCartPos.y);
			return std::fabs(flAreaZ - vCartPos.z) <= flMaxHeightDiff;
		};

	auto FindGroundArea = [&]() -> CNavArea*
		{
			CNavArea* pBest = nullptr;
			float flBestDist = FLT_MAX;

			for (auto& tArea : F::NavEngine.GetNavFile()->m_vAreas)
			{
				if (!tArea.IsOverlapping(vCartPos, flPlanarTolerance))
					continue;

				const float flAreaZ = tArea.GetZ(vCartPos.x, vCartPos.y);
				const float flZDiff = std::fabs(flAreaZ - vCartPos.z);
				if (flZDiff > flMaxHeightDiff)
					continue;

				const float flDist = tArea.m_vCenter.DistToSqr(vCartPos);
				if (flDist < flBestDist)
				{
					flBestDist = flDist;
					pBest = &tArea;
				}
			}

			return pBest;
		};

	CNavArea* pInitialArea = F::NavEngine.FindClosestNavArea(vCartPos, false);
	pCartArea = IsAreaUsable(pInitialArea) ? pInitialArea : FindGroundArea();

	if (pCartArea)
	{
		Vector2D planarTarget(vAdjustedPos.x, vAdjustedPos.y);
		Vector vSnapped = pCartArea->GetNearestPoint(planarTarget);
		vAdjustedPos = vSnapped;
	}
	else
		vAdjustedPos.z = vCartPos.z;

	// Adjust position, so it's not floating high up, provided the local player is close.
	if (vLocalOrigin.DistTo(vAdjustedPos) <= 150.0f)
	{
		if (pCartArea)
			vAdjustedPos.z = pCartArea->GetZ(vAdjustedPos.x, vAdjustedPos.y);
		else
			vAdjustedPos.z = vPosition.z;
	}

	if (Vars::Debug::Info.Value)
	{
		const float flCheckRadius = flCartRadius;
		for (auto& tArea : F::NavEngine.GetNavFile()->m_vAreas)
		{
			if (!tArea.IsOverlapping(vPosition, flCheckRadius))
				continue;

			const float flZDiff = std::fabs(tArea.m_vCenter.z - vPosition.z);
			if (flZDiff > flMaxHeightDiff)
				continue;

			G::LineStorage.emplace_back(std::pair<Vector, Vector>(Vector(tArea.m_vNwCorner.x, tArea.m_vNwCorner.y, tArea.m_flNeZ), Vector(tArea.m_vSeCorner.x, tArea.m_vNwCorner.y, tArea.m_flNeZ)), I::GlobalVars->curtime + 2.1f, Color_t(0, 255, 0, 100));
			G::LineStorage.emplace_back(std::pair<Vector, Vector>(Vector(tArea.m_vSeCorner.x, tArea.m_vNwCorner.y, tArea.m_flNeZ), Vector(tArea.m_vSeCorner.x, tArea.m_vSeCorner.y, tArea.m_flSwZ)), I::GlobalVars->curtime + 2.1f, Color_t(0, 255, 0, 100));
			G::LineStorage.emplace_back(std::pair<Vector, Vector>(Vector(tArea.m_vSeCorner.x, tArea.m_vSeCorner.y, tArea.m_flSwZ), Vector(tArea.m_vNwCorner.x, tArea.m_vSeCorner.y, tArea.m_flSwZ)), I::GlobalVars->curtime + 2.1f, Color_t(0, 255, 0, 100));
			G::LineStorage.emplace_back(std::pair<Vector, Vector>(Vector(tArea.m_vNwCorner.x, tArea.m_vSeCorner.y, tArea.m_flSwZ), Vector(tArea.m_vNwCorner.x, tArea.m_vNwCorner.y, tArea.m_flNeZ)), I::GlobalVars->curtime + 2.1f, Color_t(0, 255, 0, 100));
		}
		G::SphereStorage.emplace_back(vPosition, flCartRadius, 20, 20, I::GlobalVars->curtime + 2.1f, Color_t(0, 255, 0, 10), Color_t(0, 255, 0, 100));
	}

	if (vPosition.DistTo(vLocalOrigin) <= flCartRadius || vAdjustedPos.DistTo(vLocalOrigin) <= 45.f)
	{
		m_bOverwriteCapture = true;
		return false;
	}

	vOut = vAdjustedPos;
	return true;
}

bool CNavBotCapture::GetControlPointGoal(const Vector vLocalOrigin, int iOurTeam, Vector& vOut)
{
	m_sCaptureStatus = L"CP";
	std::pair<int, Vector> tControlPointInfo;
	if (!F::CPController.GetClosestControlPointInfo(vLocalOrigin, iOurTeam, tControlPointInfo))
	{
		m_vCurrentCaptureSpot.reset();
		m_vCurrentCaptureCenter.reset();
		ReleaseCaptureSpotClaim();
		return false;
	}

	Vector vPosition = tControlPointInfo.second;
	const int iControlPointIdx = tControlPointInfo.first;

	if (!m_vCurrentCaptureCenter.has_value() || m_vCurrentCaptureCenter->DistToSqr(vPosition) > 1.0f)
	{
		m_vCurrentCaptureCenter = vPosition;
		m_vCurrentCaptureSpot.reset();
	}

	constexpr float flCapRadius = 100.0f; // Approximate capture radius
	constexpr float flThreatRadius = 800.0f; // Distance to check for enemies
	constexpr float flOccupancyRadius = 28.0f;
	const float flOccupancyRadiusSq = flOccupancyRadius * flOccupancyRadius;
	const int iLocalIndex = I::EngineClient->GetLocalPlayer();

	std::vector<Vector> vTeammatePositions;
	vTeammatePositions.reserve(8);
	int iTeammatesOnPoint = 0;

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerTeam))
	{
		if (pEntity->IsDormant() || pEntity->entindex() == iLocalIndex)
			continue;

		auto pTeammate = pEntity->As<CTFPlayer>();
		if (!pTeammate || !pTeammate->IsAlive())
			continue;

		Vector vTeammateOrigin = pTeammate->GetAbsOrigin();
		vTeammatePositions.push_back(vTeammateOrigin);

		if (vTeammateOrigin.DistTo(vPosition) <= flCapRadius)
			iTeammatesOnPoint++;
	}

	bool bEnemiesNear = false;
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
	{
		if (pEntity->IsDormant())
			continue;

		auto pEnemy = pEntity->As<CTFPlayer>();
		if (!pEnemy->IsAlive() || !ShouldAvoidPlayer(pEnemy->entindex()))
			continue;

		if (pEnemy->GetAbsOrigin().DistTo(vPosition) <= flThreatRadius)
		{
			bEnemiesNear = true;
			break;
		}
	}

#ifdef TEXTMODE
	std::vector<Vector> vReservedSpots;
	if (iControlPointIdx != -1)
		vReservedSpots = F::NamedPipe.GetReservedCaptureSpots(SDK::GetLevelName(), iControlPointIdx, H::Entities.GetLocalAccountID());
#endif

	auto SpotTakenByOther = [&](const Vector& vSpot) -> bool
		{
#ifdef TEXTMODE
			for (const auto& vReserved : vReservedSpots)
			{
				if (vReserved.DistTo2DSqr(vSpot) <= flOccupancyRadiusSq)
					return true;
			}
#else
			for (const auto& vTeammatePos : vTeammatePositions)
			{
				if (vTeammatePos.DistTo2DSqr(vSpot) <= flOccupancyRadiusSq)
					return true;
			}
#endif
			return false;
		};

	auto ClosestTeammateDistance = [&](const Vector& vSpot) -> float
		{
			if (vTeammatePositions.empty())
				return FLT_MAX;

			float flBest = FLT_MAX;
			for (const auto& vTeammatePos : vTeammatePositions)
			{
				float flDist = vTeammatePos.DistTo2DSqr(vSpot);
				if (flDist < flBest)
					flBest = flDist;
			}
			return flBest;
		};

	Vector vAdjustedPos = vPosition;

	if (bEnemiesNear)
	{
		m_vCurrentCaptureSpot.reset();
		for (auto tArea : F::NavEngine.GetNavFile()->m_vAreas)
		{
			for (auto& tHidingSpot : tArea.m_vHidingSpots)
			{
				if (tHidingSpot.HasGoodCover() && tHidingSpot.m_vPos.DistTo(vPosition) <= flCapRadius)
				{
					vAdjustedPos = tHidingSpot.m_vPos;
					break;
				}
			}
		}
	}
	else
	{
		if (m_vCurrentCaptureSpot && SpotTakenByOther(m_vCurrentCaptureSpot.value()))
			m_vCurrentCaptureSpot.reset();

		if (!m_vCurrentCaptureSpot)
		{
			const int iSlots = std::clamp(iTeammatesOnPoint + 1, 1, 8);
			const float flBaseRadius = iSlots == 1 ? 0.0f : std::min(flCapRadius - 12.0f, 45.0f + 12.0f * static_cast<float>(iSlots - 1));
			const int iPreferredSlot = iSlots > 0 ? (iLocalIndex % iSlots) : 0;

			auto AdjustToNav = [&](Vector vCandidate)
				{
					if (F::NavEngine.IsNavMeshLoaded())
					{
						if (auto pArea = F::NavEngine.FindClosestNavArea(vCandidate))
						{
							Vector vCorrected = pArea->GetNearestPoint(vCandidate.Get2D());
							vCorrected.z = pArea->m_vCenter.z;
							vCandidate = vCorrected;
						}
					}
					return vCandidate;
				};

			std::vector<Vector> vFallbackCandidates;
			vFallbackCandidates.reserve(iSlots + 12);

			for (int offset = 0; offset < iSlots; ++offset)
			{
				int slotIndex = (iPreferredSlot + offset) % iSlots;
				float t = static_cast<float>(slotIndex) / static_cast<float>(iSlots);
				float flAngle = t * PI * 2.0f;

				Vector vCandidate = vPosition;
				if (flBaseRadius > 1.0f)
				{
					vCandidate.x += cos(flAngle) * flBaseRadius;
					vCandidate.y += sin(flAngle) * flBaseRadius;
				}

				vCandidate = AdjustToNav(vCandidate);
				vFallbackCandidates.push_back(vCandidate);

				if (!SpotTakenByOther(vCandidate))
				{
					m_vCurrentCaptureSpot = vCandidate;
					break;
				}
			}

			if (!m_vCurrentCaptureSpot)
			{
				for (int iRing = 1; iRing <= 2; ++iRing)
				{
					float flRingRadius = std::min(flCapRadius - 12.0f, flBaseRadius + 14.0f * static_cast<float>(iRing));
					int iMaxSegments = std::max(6, iSlots + iRing * 2);
					for (int iSeg = 0; iSeg < iMaxSegments; ++iSeg)
					{
						float flAngle = (static_cast<float>(iSeg) / iMaxSegments) * PI * 2.0f;
						Vector vCandidate = vPosition;
						if (flRingRadius > 1.0f)
						{
							vCandidate.x += cos(flAngle) * flRingRadius;
							vCandidate.y += sin(flAngle) * flRingRadius;
						}

						vCandidate = AdjustToNav(vCandidate);
						vFallbackCandidates.push_back(vCandidate);

						if (!SpotTakenByOther(vCandidate))
						{
							m_vCurrentCaptureSpot = vCandidate;
							break;
						}
					}
					if (m_vCurrentCaptureSpot)
						break;
				}
			}

			if (!m_vCurrentCaptureSpot)
			{
				vFallbackCandidates.push_back(AdjustToNav(vPosition));

				Vector vBestCandidate = vPosition;
				float flBestScore = -1.0f;
				for (const auto& vCandidate : vFallbackCandidates)
				{
					float flScore = ClosestTeammateDistance(vCandidate);
					if (flScore > flBestScore)
					{
						flBestScore = flScore;
						vBestCandidate = vCandidate;
					}
				}

				m_vCurrentCaptureSpot = vBestCandidate;
			}
		}

		if (m_vCurrentCaptureSpot)
			vAdjustedPos = m_vCurrentCaptureSpot.value();
	}

	if (m_vCurrentCaptureSpot && iControlPointIdx != -1)
		ClaimCaptureSpot(*m_vCurrentCaptureSpot, iControlPointIdx);
	else
		ReleaseCaptureSpotClaim();

	if (vLocalOrigin.DistTo(vAdjustedPos) <= 150.0f)
		vAdjustedPos.z = vLocalOrigin.z;

	Vector2D vFlatDelta(vAdjustedPos.x - vLocalOrigin.x, vAdjustedPos.y - vLocalOrigin.y);
	if (vFlatDelta.LengthSqr() <= pow(45.0f, 2))
	{
		if (iControlPointIdx == -1)
		{
			m_vCurrentCaptureSpot.reset();
			ReleaseCaptureSpotClaim();
			if (Vars::Debug::Logging.Value)
				SDK::Output("NavBotCapture", "Capture.GetControlPointGoal: nearby control point index invalid, searching other points", { 255, 200, 50 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
			return false;
		}

		std::pair<int, Vector> tVerify;
		if (!F::CPController.GetClosestControlPointInfo(vLocalOrigin, iOurTeam, tVerify) || tVerify.first != iControlPointIdx)
		{
			m_vCurrentCaptureSpot.reset();
			ReleaseCaptureSpotClaim();
			if (Vars::Debug::Logging.Value)
				SDK::Output("NavBotCapture", std::format("Capture.GetControlPointGoal: control point {} no longer valid (verified {}), searching other points", iControlPointIdx, tVerify.first).c_str(), { 255, 200, 50 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
			return false;
		}

		m_bOverwriteCapture = true;
		return false;
	}

	vOut = vAdjustedPos;
	return true;
}

bool CNavBotCapture::GetDoomsdayGoal(CTFPlayer* pLocal, int iOurTeam, int iEnemyTeam, Vector& vOut)
{
	int iTeam = TEAM_UNASSIGNED;
	while (iTeam != -1)
	{
		auto tFlag = F::FlagController.GetFlag(iTeam);
		if (tFlag.m_pFlag)
			break;

		iTeam = iTeam != iOurTeam ? iOurTeam : -1;
	}

	// No australium found
	if (iTeam == -1)
		return false;

	Vector vPosition;
	if (!F::FlagController.GetPosition(iTeam, vPosition))
		return false;

	// Get Australium related information
	auto iStatus = F::FlagController.GetStatus(iTeam);
	auto iCarrierIdx = F::FlagController.GetCarrier(iTeam);

	if (iStatus == TF_FLAGINFO_STOLEN)
	{
		// We have the australium
		if (iCarrierIdx == pLocal->entindex())
		{
			// Get rocket position - in Doomsday it's marked as a cap point
			Vector vRocket;
			if (F::CPController.GetClosestControlPoint(pLocal->GetAbsOrigin(), iOurTeam, vRocket))
			{
				// If close enough, don't move
				if (vRocket.DistTo(pLocal->GetAbsOrigin()) <= 50.f)
				{
					m_bOverwriteCapture = true;
					return false;
				}

				// Check for enemies near the capture point that might intercept
				bool bEnemiesNearRocket = false;
				constexpr float flThreatRadius = 500.0f; // Distance to check for enemies

				for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
				{
					if (pEntity->IsDormant())
						continue;

					auto pEnemy = pEntity->As<CTFPlayer>();
					if (!pEnemy->IsAlive() || !ShouldAvoidPlayer(pEnemy->entindex()))
						continue;

					if (pEnemy->GetAbsOrigin().DistTo(vRocket) <= flThreatRadius)
					{
						bEnemiesNearRocket = true;
						break;
					}
				}

				// If enemies are near the rocket, stay back a bit until teammates can help
				if (bEnemiesNearRocket && (Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::DefendObjectives))
				{
					// Find a safer approach path or wait for teammates
					auto vPathToRocket = vRocket - pLocal->GetAbsOrigin();
					float pathLen = vPathToRocket.Length();
					if (pathLen > 0.001f)
					{
						vPathToRocket.x /= pathLen;
						vPathToRocket.y /= pathLen;
						vPathToRocket.z /= pathLen;
					}

					// Back up a bit from the rocket
					Vector vSaferPosition = vRocket - (vPathToRocket * 300.0f);
					vOut = vSaferPosition;
					return true;
				}
				vOut = vRocket;
				return true;
			}
		}
		// Help friendly carrier
		else if (Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::HelpCaptureObjectives)
		{
			if (F::BotUtils.ShouldAssist(pLocal, iCarrierIdx))
			{
				// Check if carrier is navigating to the rocket
				auto pCarrier = I::ClientEntityList->GetClientEntity(iCarrierIdx);
				if (pCarrier && !pCarrier->IsDormant())
				{

					// Try to position strategically to protect the carrier
					Vector vRocket;
					if (F::CPController.GetClosestControlPoint(pCarrier->GetAbsOrigin(), iOurTeam, vRocket))
					{
						Vector vCarrierToRocket = vRocket - pCarrier->GetAbsOrigin();
						float len = vCarrierToRocket.Length();
						if (len > 0.001f)
						{
							vCarrierToRocket.x /= len;
							vCarrierToRocket.y /= len;
							vCarrierToRocket.z /= len;
						}

						// Position to the side and slightly behind the carrier in the direction of the rocket
						Vector vCrossProduct = vCarrierToRocket.Cross(Vector(0, 0, 1));
						float crossLen = vCrossProduct.Length();
						if (crossLen > 0.001f)
						{
							vCrossProduct.x /= crossLen;
							vCrossProduct.y /= crossLen;
							vCrossProduct.z /= crossLen;
						}

						// Position offset from carrier toward rocket but slightly to the side
						Vector vOffset = (vCarrierToRocket * -80.0f) + (vCrossProduct * 60.0f);
						vOut = pCarrier->GetAbsOrigin() + vOffset;
						return true;
					}

					// Default offset if rocket position not found
					Vector vOffset(40.0f, 40.0f, 0.0f);
					vPosition -= vOffset;
					vOut = vPosition;
					return true;
				}
			}
		}
	}

	// If nobody has the australium, look for it

	// Check if enemies are near the australium
	bool bEnemiesNearAustralium = false;
	constexpr float flThreatRadius = 600.0f;

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
	{
		if (pEntity->IsDormant())
			continue;

		auto pEnemy = pEntity->As<CTFPlayer>();
		if (!pEnemy->IsAlive() || !ShouldAvoidPlayer(pEnemy->entindex()))
			continue;

		if (pEnemy->GetAbsOrigin().DistTo(vPosition) <= flThreatRadius)
		{
			bEnemiesNearAustralium = true;
			break;
		}
	}

	// If enemies are near and we're not close, approach carefully
	if (bEnemiesNearAustralium && pLocal->GetAbsOrigin().DistTo(vPosition) > 300.f)
	{
		// Try to find a safer approach path
		auto pClosestNav = F::NavEngine.FindClosestNavArea(vPosition);
		if (pClosestNav)
		{
			std::pair<CNavArea*, int> tHidingSpot;
			if (F::NavBotCore.FindClosestHidingSpot(pClosestNav, vPosition, 5, tHidingSpot))
			{
				vOut = tHidingSpot.first->m_vCenter;
				return true;
			}
		}
	}

	// Get the australium if not taken
	vOut = vPosition;
	return true;
}

void CNavBotCapture::ClaimCaptureSpot(const Vector& vSpot, int iPointIdx)
{
#ifdef TEXTMODE
	const std::optional<int> oPreviousIndex = m_iCurrentCapturePointIdx;
	if (iPointIdx >= 0)
	{
		const bool bChangedPoint = !oPreviousIndex || *oPreviousIndex != iPointIdx;
		const bool bChangedSpot = !m_vLastClaimedCaptureSpot || m_vLastClaimedCaptureSpot->DistToSqr(vSpot) > 1.0f;
		if (bChangedPoint && oPreviousIndex)
			F::NamedPipe.AnnounceCaptureSpotRelease(SDK::GetLevelName(), *oPreviousIndex);
		if (bChangedPoint || bChangedSpot || m_tCaptureClaimRefresh.Run(0.6f))
		{
			F::NamedPipe.AnnounceCaptureSpotClaim(SDK::GetLevelName(), iPointIdx, vSpot, 1.5f);
			m_tCaptureClaimRefresh.Update();
		}
	}
#else
	(void)vSpot;
	(void)iPointIdx;
#endif
	m_vLastClaimedCaptureSpot = vSpot;
	m_iCurrentCapturePointIdx = iPointIdx;
}

void CNavBotCapture::ReleaseCaptureSpotClaim()
{
	const bool bHadClaim = m_iCurrentCapturePointIdx.has_value() || m_vLastClaimedCaptureSpot.has_value();
#ifdef TEXTMODE
	if (m_iCurrentCapturePointIdx)
		F::NamedPipe.AnnounceCaptureSpotRelease(SDK::GetLevelName(), *m_iCurrentCapturePointIdx);
#endif
	m_vLastClaimedCaptureSpot.reset();
	m_iCurrentCapturePointIdx.reset();
	if (bHadClaim)
		m_tCaptureClaimRefresh -= 10.f;
}

bool CNavBotCapture::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	static Timer tCaptureTimer;
	static Vector vPreviousTarget;

	if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::CaptureObjectives))
	{
		if (Vars::Debug::Logging.Value)
			SDK::Output("NavBotCapture", "Capture.Run: CaptureObjectives preference disabled", { 255, 200, 50 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
		return false;
	}

	if (const auto& pGameRules = I::TFGameRules())
	{
		if (!((pGameRules->m_iRoundState() == GR_STATE_RND_RUNNING || pGameRules->m_iRoundState() == GR_STATE_STALEMATE) && !pGameRules->m_bInWaitingForPlayers())
			|| pGameRules->m_iRoundState() == GR_STATE_TEAM_WIN
			|| pGameRules->m_bPlayingSpecialDeliveryMode())
		{
			if (Vars::Debug::Logging.Value)
				SDK::Output("NavBotCapture", std::format("Capture.Run: game rules prevented capture (roundState={}, waiting={}, teamwin={}, specialDelivery={})",
					pGameRules->m_iRoundState(), pGameRules->m_bInWaitingForPlayers(), pGameRules->m_iRoundState() == GR_STATE_TEAM_WIN, pGameRules->m_bPlayingSpecialDeliveryMode()).c_str(), { 255, 200, 50 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
			return false;
		}
	}

	if (!tCaptureTimer.Check(2.f))
	{
		return F::NavEngine.m_eCurrentPriority == PriorityListEnum::Capture;
	}

	// Priority too high, don't try
	if (F::NavEngine.m_eCurrentPriority > PriorityListEnum::Capture)
	{
		return false;
	}

	int iOurTeam = pLocal->m_iTeamNum();
	int iEnemyTeam = iOurTeam == TF_TEAM_BLUE ? TF_TEAM_RED : TF_TEAM_BLUE;
	m_bOverwriteCapture = false;

	const auto vLocalOrigin = pLocal->GetAbsOrigin();
	bool bGotTarget = false;

	// Where we want to go
	Vector vTarget;

	// Run logic
	switch (F::GameObjectiveController.m_eGameMode)
	{
	case TF_GAMETYPE_CTF:
		bGotTarget = GetCtfGoal(pLocal, iOurTeam, iEnemyTeam, vTarget);
		break;
	case TF_GAMETYPE_CP:
		bGotTarget = GetControlPointGoal(vLocalOrigin, iOurTeam, vTarget);
		break;
	case TF_GAMETYPE_ESCORT:
		bGotTarget = GetPayloadGoal(vLocalOrigin, iOurTeam, vTarget);
		break;
	default:
		if (F::GameObjectiveController.m_bDoomsday)
			bGotTarget = GetDoomsdayGoal(pLocal, iOurTeam, iEnemyTeam, vTarget);
		else if (F::GameObjectiveController.m_bHaarp)
			bGotTarget = GetCtfGoal(pLocal, iOurTeam, iEnemyTeam, vTarget);
		break;
	}

	// Overwritten, for example because we are currently on the payload, cancel any sort of pathing and return true
	if (m_bOverwriteCapture)
	{
		if (Vars::Debug::Logging.Value)
			SDK::Output("NavBotCapture", "Capture.Run: overwritten capture (player is on objective or close enough)", { 150, 255, 150 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
		F::NavEngine.CancelPath();
		return true;
	}

	// No target, bail and set on cooldown
	if (!bGotTarget)
	{
		tCaptureTimer.Update();
		return F::NavEngine.m_eCurrentPriority == PriorityListEnum::Capture;
	}

	if (Vars::Debug::Info.Value)
	{
		G::SphereStorage.emplace_back(vTarget, 30.f, 20, 20, I::GlobalVars->curtime + 2.1f, Color_t(255, 255, 255, 10), Color_t(255, 255, 255, 100));
	}

	// If priority is not capturing, or we have a new target, try to path there
	if (F::NavEngine.m_eCurrentPriority != PriorityListEnum::Capture || vTarget != vPreviousTarget)
	{
		bool bNavOk = F::NavEngine.NavTo(vTarget, PriorityListEnum::Capture, true, !F::NavEngine.IsPathing());
		if (bNavOk)
		{
			if (Vars::Debug::Logging.Value)
				SDK::Output("NavBotCapture", "Capture.Run: NavTo succeeded, started capture path", { 150, 255, 150 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
			vPreviousTarget = vTarget;
			return true;
		}
		else
		{
			if (Vars::Debug::Logging.Value)
				SDK::Output("NavBotCapture", "Capture.Run: NavTo failed for capture target", { 255, 100, 100 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
			tCaptureTimer.Update();
		}
	}
	return false;
}

void CNavBotCapture::Reset()
{
	m_vCurrentCaptureSpot.reset();
	m_vCurrentCaptureCenter.reset();
	ReleaseCaptureSpotClaim();
	for (auto& tCache : m_aPayloadCache)
		tCache = {};
}
