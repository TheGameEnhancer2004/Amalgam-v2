#include "SnipeSentry.h"
#include "../NavEngine/NavEngine.h"

bool CNavBotSnipe::IsAreaValidForSnipe(Vector vEntOrigin, Vector vAreaOrigin, bool bFixSentryZ)
{
	if (bFixSentryZ)
		vEntOrigin.z += 40.0f;
	vAreaOrigin.z += PLAYER_CROUCHED_JUMP_HEIGHT;

	// Too close to be valid
	if (vEntOrigin.DistTo(vAreaOrigin) <= 1100.f + HALF_PLAYER_WIDTH)
		return false;

	// Fails vischeck, bad
	if (!F::NavEngine.IsVectorVisibleNavigation(vAreaOrigin, vEntOrigin))
		return false;
	return true;
}

bool CNavBotSnipe::TryToSnipe(int iEntIdx)
{
	Vector vOrigin;
	if (!F::BotUtils.GetDormantOrigin(iEntIdx, vOrigin))
		return false;

	// Add some z to dormant sentries as it only returns origin
	//if (ent->IsDormant())
	vOrigin.z += 40.0f;

	std::vector<std::pair<CNavArea*, float>> vGoodAreas;
	for (auto& area : F::NavEngine.GetNavFile()->m_vAreas)
	{
		// Not usable
		if (!IsAreaValidForSnipe(vOrigin, area.m_vCenter, false))
			continue;
		vGoodAreas.push_back(std::pair<CNavArea*, float>(&area, area.m_vCenter.DistTo(vOrigin)));
	}

	// Sort based on distance
	if (F::NavBotCore.m_tSelectedConfig.m_bPreferFar)
		std::sort(vGoodAreas.begin(), vGoodAreas.end(), [](std::pair<CNavArea*, float> a, std::pair<CNavArea*, float> b) { return a.second > b.second; });
	else
		std::sort(vGoodAreas.begin(), vGoodAreas.end(), [](std::pair<CNavArea*, float> a, std::pair<CNavArea*, float> b) { return a.second < b.second; });

	if (std::ranges::any_of(vGoodAreas, [](std::pair<CNavArea*, float> pair) { return F::NavEngine.NavTo(pair.first->m_vCenter, PriorityListEnum::SnipeSentry); }))
		return true;
	return false;
}

bool CNavBotSnipe::Run(CTFPlayer* pLocal)
{
	static Timer tSentrySnipeCooldown;
	static Timer tInvalidTargetTimer{};

	if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::TargetSentries))
	{
		m_iTargetIdx = -1;
		return false;
	}

	// Make sure we don't try to do it on shortrange classes unless specified
	if (!(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::TargetSentriesLowRange)
		&& (pLocal->m_iClass() == TF_CLASS_SCOUT || pLocal->m_iClass() == TF_CLASS_PYRO))
	{
		m_iTargetIdx = -1;
		return false;
	}

	// Sentries don't move often, so we can use a slightly longer timer
	if (!tSentrySnipeCooldown.Run(2.f))
		return F::NavEngine.m_eCurrentPriority == PriorityListEnum::SnipeSentry;

	int iPreviousTargetValid = F::BotUtils.ShouldTargetBuilding(pLocal, m_iTargetIdx);
	if (iPreviousTargetValid == ShouldTargetEnum::Target)
	{
		tInvalidTargetTimer.Update();

		Vector vOrigin;
		if (F::BotUtils.GetDormantOrigin(m_iTargetIdx, vOrigin))
		{
			// We cannot just use the last crumb, as it is always nullptr
			if (F::NavEngine.m_tLastCrumb.m_pNavArea)
			{
				// Area is still valid, stay on it
				if (IsAreaValidForSnipe(vOrigin, F::NavEngine.m_tLastCrumb.m_pNavArea->m_vCenter))
					return true;
			}
			if (TryToSnipe(m_iTargetIdx))
				return true;
		}
	}
	// Our previous target wasn't properly checked
	else if (iPreviousTargetValid == ShouldTargetEnum::Invalid && !tInvalidTargetTimer.Check(0.1f))
		return F::NavEngine.m_eCurrentPriority == PriorityListEnum::SnipeSentry;

	tInvalidTargetTimer.Update();

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::BuildingEnemy))
	{
		int iEntIdx = pEntity->entindex();

		// Invalid sentry
		if (F::BotUtils.ShouldTargetBuilding(pLocal, iEntIdx) != ShouldTargetEnum::Target)
			continue;

		// Succeeded in trying to snipe it
		if (TryToSnipe(iEntIdx))
		{
			m_iTargetIdx = iEntIdx;
			return true;
		}
	}

	m_iTargetIdx = -1;
	return false;
}
