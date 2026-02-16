#include "StayNear.h"
#include "../NavEngine/NavEngine.h"
#include "../../Players/PlayerUtils.h"

namespace
{
	float GetPreferredStalkRadius(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
	{
		if (!pLocal)
			return 400.f;

		switch (pLocal->m_iClass())
		{
		case TF_CLASS_SCOUT:
			return 260.f;
		case TF_CLASS_SOLDIER:
			return 450.f;
		case TF_CLASS_PYRO:
			return 300.f;
		case TF_CLASS_DEMOMAN:
			return 470.f;
		case TF_CLASS_HEAVY:
			return 280.f;
		case TF_CLASS_ENGINEER:
			return pWeapon && pWeapon->m_iItemDefinitionIndex() == Engi_t_TheGunslinger ? 140.f : 260.f;
		case TF_CLASS_MEDIC:
			return 360.f;
		case TF_CLASS_SNIPER:
			return pWeapon && pWeapon->GetWeaponID() == TF_WEAPON_COMPOUND_BOW ? 700.f : 1200.f;
		case TF_CLASS_SPY:
			return 220.f;
		default:
			return 420.f;
		}
	}

	float GetStalkLeadTime(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, float flTargetDistance, float flTargetSpeed)
	{
		float flLeadTime = 0.15f;

		if (!pLocal)
			return flLeadTime;

		switch (pLocal->m_iClass())
		{
		case TF_CLASS_SCOUT:
		case TF_CLASS_PYRO:
		case TF_CLASS_SPY:
			flLeadTime = 0.14f;
			break;
		case TF_CLASS_SOLDIER:
		case TF_CLASS_DEMOMAN:
			flLeadTime = 0.2f;
			break;
		case TF_CLASS_SNIPER:
			flLeadTime = pWeapon && pWeapon->GetWeaponID() == TF_WEAPON_COMPOUND_BOW ? 0.28f : 0.38f;
			break;
		default:
			flLeadTime = 0.18f;
			break;
		}

		flLeadTime += std::clamp(flTargetDistance / 2500.f, 0.f, 0.2f);
		if (flTargetSpeed < 25.f)
			flLeadTime *= 0.6f;

		return std::clamp(flLeadTime, 0.08f, 0.55f);
	}

	Vector Normalize2D(const Vector& v)
	{
		Vector vOut = v;
		vOut.z = 0.f;
		float flLength = vOut.Length();
		if (flLength > 0.01f)
			vOut /= flLength;
		else
			vOut = {};
		return vOut;
	}
}

bool CNavBotStayNear::StayNearTarget(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, int iEntIndex)
{
	auto pEntity = I::ClientEntityList->GetClientEntity(iEntIndex);
	if (!pEntity)
		return false;
	auto pPlayer = pEntity->As<CTFPlayer>();

	Vector vOrigin;

	// No origin recorded, don't bother
	if (!F::BotUtils.GetDormantOrigin(iEntIndex, &vOrigin))
		return false;

	auto pLocalArea = F::NavEngine.GetLocalNavArea();
	if (!pLocalArea)
		return false;

	// Add the vischeck height
	vOrigin.z += PLAYER_CROUCHED_JUMP_HEIGHT;
	Vector vTargetOrigin = vOrigin;

	Vector vTargetVelocity{};
	if (pPlayer && !pPlayer->IsDormant())
		vTargetVelocity = pPlayer->GetAbsVelocity();
	vTargetVelocity.z = 0.f;

	const float flTargetDistance = vTargetOrigin.DistTo(pLocal->GetAbsOrigin());
	const float flTargetSpeed = vTargetVelocity.Length2D();

	const float flPreferredRadiusBase = GetPreferredStalkRadius(pLocal, pWeapon);
	const float flPreferredRadius = std::clamp(flPreferredRadiusBase, F::NavBotCore.m_tSelectedConfig.m_flMinFullDanger, F::NavBotCore.m_tSelectedConfig.m_flMax);
	const float flLeadTime = GetStalkLeadTime(pLocal, pWeapon, flTargetDistance, flTargetSpeed);
	const Vector vPredictedOrigin = vTargetOrigin + vTargetVelocity * flLeadTime;

	Vector vForward = Normalize2D(vTargetVelocity);
	if (vForward.IsZero())
		vForward = Normalize2D(vPredictedOrigin - pLocal->GetAbsOrigin());

	Vector vSide(-vForward.y, vForward.x, 0.f);
	const float flOutrunDistance = std::clamp(flPreferredRadius * 0.35f, 80.f, 450.f);
	const float flSideDistance = std::clamp(flPreferredRadius * 0.28f, 70.f, 300.f);
	const float flSideSign = (iEntIndex + pLocal->entindex()) % 2 ? 1.f : -1.f;
	const Vector vAnchor = vPredictedOrigin + vForward * flOutrunDistance + vSide * (flSideDistance * flSideSign);

	// Use std::pair to avoid using the distance functions more than once
	std::vector<std::pair<CNavArea*, float>> vGoodAreas{};

	for (auto& tArea : F::NavEngine.GetNavFile()->m_vAreas)
	{
		auto vAreaOrigin = tArea.m_vCenter;

		// Is this area valid for stay near purposes?
		if (!IsAreaValidForStayNear(vOrigin, &tArea, false))
			continue;

		const float flDistToPredicted = vAreaOrigin.DistTo(vPredictedOrigin);
		const float flRangePenalty = std::fabs(flDistToPredicted - flPreferredRadius);
		const float flAnchorPenalty = vAreaOrigin.DistTo(vAnchor);
		const float flTravelPenalty = pLocalArea->m_vCenter.DistTo(vAreaOrigin);

		float flAheadPenalty = 0.f;
		if (!vForward.IsZero())
		{
			Vector vToArea = Normalize2D(vAreaOrigin - vPredictedOrigin);
			float flAheadDot = std::clamp(vToArea.Dot(vForward), -1.f, 1.f);
			flAheadPenalty = (1.f - flAheadDot) * 140.f;
		}

		const float flScore = flRangePenalty * 1.4f + flAnchorPenalty + flTravelPenalty * 0.15f + flAheadPenalty;
		vGoodAreas.emplace_back(&tArea, flScore);
	}
	// Sort based on score
	std::sort(vGoodAreas.begin(), vGoodAreas.end(), [](std::pair<CNavArea*, float> a, std::pair<CNavArea*, float> b) { return a.second < b.second; });

	// Try to path to all the good areas, based on distance
	if (std::ranges::any_of(vGoodAreas, [&](std::pair<CNavArea*, float> pair) -> bool
		{
			return F::NavEngine.NavTo(pair.first->m_vCenter, PriorityListEnum::StayNear, true, !F::NavEngine.IsPathing());
		})
		)
	{
		m_iStayNearTargetIdx = pEntity->entindex();
		if (auto pPlayerResource = H::Entities.GetResource())
			m_sFollowTargetName = SDK::ConvertUtf8ToWide(pPlayerResource->GetName(pEntity->entindex()));
		return true;
	}

	return false;
}

bool CNavBotStayNear::IsAreaValidForStayNear(Vector vEntOrigin, CNavArea* pArea, bool bFixLocalZ)
{
	if (bFixLocalZ)
		vEntOrigin.z += PLAYER_CROUCHED_JUMP_HEIGHT;
	auto vAreaOrigin = pArea->m_vCenter;
	vAreaOrigin.z += PLAYER_CROUCHED_JUMP_HEIGHT;

	float flDist = vEntOrigin.DistTo(vAreaOrigin);
	// Too close
	if (flDist < F::NavBotCore.m_tSelectedConfig.m_flMinFullDanger)
		return false;

	// Blacklisted
	if (F::NavEngine.GetFreeBlacklist()->find(pArea) != F::NavEngine.GetFreeBlacklist()->end())
		return false;

	// Too far away
	if (flDist > F::NavBotCore.m_tSelectedConfig.m_flMax)
		return false;

	CGameTrace trace = {};
	CTraceFilterWorldAndPropsOnly filter = {};

	// Attempt to vischeck
	SDK::Trace(vEntOrigin, vAreaOrigin, MASK_SHOT | CONTENTS_GRATE, &filter, &trace);
	return trace.fraction == 1.f;
}

int CNavBotStayNear::IsStayNearTargetValid(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, int iEntIndex)
{
	if (!pLocal || iEntIndex <= 0 || iEntIndex == pLocal->entindex())
		return 0;

	return F::BotUtils.ShouldTarget(pLocal, pWeapon, iEntIndex);
}

bool CNavBotStayNear::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	static Timer tStaynearCooldown{};
	static Timer tInvalidTargetTimer{};
	static int iStayNearTargetIdx = -1;

	// Stay near is expensive so we have to cache. We achieve this by only checking a pre-determined amount of players every
	// CreateMove
	constexpr int MAX_STAYNEAR_CHECKS_RANGE = 3;
	constexpr int MAX_STAYNEAR_CHECKS_CLOSE = 2;

	// Stay near is off
	if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::StalkEnemies))
	{
		iStayNearTargetIdx = -1;
		return false;
	}

	// Don't constantly path, it's slow.
	// Far range classes do not need to repath nearly as often as close range ones.
	if (!tStaynearCooldown.Run(F::NavBotCore.m_tSelectedConfig.m_bPreferFar ? 2.f : 0.5f))
		return F::NavEngine.m_eCurrentPriority == PriorityListEnum::StayNear;

	// Too high priority, so don't try
	if (F::NavEngine.m_eCurrentPriority > PriorityListEnum::StayNear)
	{
		iStayNearTargetIdx = -1;
		return false;
	}

	int iPreviousTargetValid = IsStayNearTargetValid(pLocal, pWeapon, iStayNearTargetIdx);
	// Check and use our previous target if available
	if (iPreviousTargetValid)
	{
		tInvalidTargetTimer.Update();

		// Check if target is RAGE status - if so, always keep targeting them
		int iPriority = H::Entities.GetPriority(iStayNearTargetIdx);
		if (iPriority > F::PlayerUtils.m_vTags[F::PlayerUtils.TagToIndex(DEFAULT_TAG)].m_iPriority)
		{
			if (StayNearTarget(pLocal, pWeapon, iStayNearTargetIdx))
				return true;
		}

		Vector vOrigin;
		if (F::BotUtils.GetDormantOrigin(iStayNearTargetIdx, &vOrigin))
		{
			// Check if current target area is valid
			if (F::NavEngine.IsPathing())
			{
				auto pCrumbs = F::NavEngine.GetCrumbs();
				// We cannot just use the last crumb, as it is always nullptr
				if (pCrumbs->size() > 2)
				{
					auto tLastCrumb = (*pCrumbs)[pCrumbs->size() - 2];
					// Area is still valid, stay on it
					if (IsAreaValidForStayNear(vOrigin, tLastCrumb.m_pNavArea))
						return true;
				}
			}
			// Else Check our origin for validity (Only for ranged classes)
			else if (F::NavBotCore.m_tSelectedConfig.m_bPreferFar && IsAreaValidForStayNear(vOrigin, F::NavEngine.GetLocalNavArea()))
				return true;
		}
		// Else we try to path again
		if (StayNearTarget(pLocal, pWeapon, iStayNearTargetIdx))
			return true;

	}
	// Our previous target wasn't properly checked, try again unless
	else if (iPreviousTargetValid == -1 && !tInvalidTargetTimer.Check(0.1f))
		return F::NavEngine.m_eCurrentPriority == PriorityListEnum::StayNear;

	// Failed, invalidate previous target and try others
	iStayNearTargetIdx = -1;
	tInvalidTargetTimer.Update();

	// Cancel path so that we dont follow old target
	if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::StayNear)
		F::NavEngine.CancelPath();

	std::vector<std::pair<int, int>> vPriorityPlayers{};
	std::unordered_set<int> sHasPriority{};
	for (const auto& pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
	{
		if (pEntity->IsDormant())
			continue;

		int iPriority = H::Entities.GetPriority(pEntity->entindex());
		if (iPriority > F::PlayerUtils.m_vTags[F::PlayerUtils.TagToIndex(DEFAULT_TAG)].m_iPriority)
		{
			vPriorityPlayers.push_back({ pEntity->entindex(), iPriority });
			sHasPriority.insert(pEntity->entindex());
		}
	}
	std::sort(vPriorityPlayers.begin(), vPriorityPlayers.end(), [](std::pair<int, int> a, std::pair<int, int> b) { return a.second > b.second; });

	// First check for RAGE players - they get highest priority
	for (auto [iPlayerIdx, _] : vPriorityPlayers)
	{
		if (!IsStayNearTargetValid(pLocal, pWeapon, iPlayerIdx))
			continue;

		if (StayNearTarget(pLocal, pWeapon, iPlayerIdx))
		{
			iStayNearTargetIdx = iPlayerIdx;
			return true;
		}
	}

	// Then check other players
	int iCalls = 0;
	auto iAdvanceCount = F::NavBotCore.m_tSelectedConfig.m_bPreferFar ? MAX_STAYNEAR_CHECKS_RANGE : MAX_STAYNEAR_CHECKS_CLOSE;
	std::vector<std::pair<int, float>> vSortedPlayers{};
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
	{
		if (iCalls >= iAdvanceCount)
			break;
		iCalls++;

		// Skip RAGE players as we already checked them
		if (sHasPriority.contains(pEntity->entindex()))
			continue;

		auto iPlayerIdx = pEntity->entindex();
		if (!IsStayNearTargetValid(pLocal, pWeapon, iPlayerIdx))
		{
			iCalls--;
			continue;
		}

		Vector vOrigin;
		if (!F::BotUtils.GetDormantOrigin(iPlayerIdx, &vOrigin))
			continue;

		vSortedPlayers.push_back({ iPlayerIdx, vOrigin.DistTo(pLocal->GetAbsOrigin()) });
	}
	if (!vSortedPlayers.empty())
	{
		std::sort(vSortedPlayers.begin(), vSortedPlayers.end(), [](std::pair<int, float> a, std::pair<int, float> b) { return a.second < b.second; });

		for (auto [iIdx, _] : vSortedPlayers)
		{
			// Succeeded pathing
			if (StayNearTarget(pLocal, pWeapon, iIdx))
			{
				iStayNearTargetIdx = iIdx;
				return true;
			}
		}
	}

	// Stay near failed to find any good targets, add extra delay
	tStaynearCooldown += 3.f;
	return false;
}
