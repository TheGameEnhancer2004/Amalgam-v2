#include "NavBotJobs/Roam.h"
#include "NavBotJobs/EscapeDanger.h"
#include "NavBotJobs/Melee.h"
#include "NavBotJobs/Capture.h"
#include "NavBotJobs/GetSupplies.h"
#include "NavBotJobs/Engineer.h"
#include "NavBotJobs/SnipeSentry.h"
#include "NavBotJobs/GroupWithOthers.h"
#include "NavBotJobs/Reload.h"
#include "NavBotJobs/StayNear.h"
#include "DangerManager/DangerManager.h"
#include "NavEngine/NavEngine.h"
#include "../FollowBot/FollowBot.h"
#include "../PacketManip/FakeLag/FakeLag.h"
#include "../CritHack/CritHack.h"
#include "../Ticks/Ticks.h"
#include "../Misc/Misc.h"


void CNavBotCore::UpdateSlot(CTFPlayer* pLocal, ClosestEnemy_t tClosestEnemy)
{
	static Timer tSlotTimer{};
	if (!tSlotTimer.Run(0.2f))
		return;

	// Prioritize reloading
	int iReloadSlot = F::NavBotReload.m_iLastReloadSlot = F::NavBotReload.GetReloadWeaponSlot(pLocal, tClosestEnemy);

	if (F::NavBotEngineer.IsEngieMode(pLocal))
	{
		int iSwitch = 0;
		switch (F::NavBotEngineer.m_eTaskStage)
		{
			// We are currently building something
		case EngineerTaskStageEnum::BuildSentry:
		case EngineerTaskStageEnum::BuildDispenser:
			if (F::NavBotEngineer.m_tCurrentBuildingSpot.m_flDistanceToTarget != FLT_MAX && F::NavBotEngineer.m_tCurrentBuildingSpot.m_vPos.DistTo(pLocal->GetAbsOrigin()) <= 500.f)
			{
				if (pLocal->m_bCarryingObject())
				{
					auto pWeapon = pLocal->m_hActiveWeapon().Get()->As<CTFWeaponBase>();
					if (pWeapon && pWeapon->GetSlot() != 3)
						F::BotUtils.SetSlot(pLocal, SLOT_PRIMARY);
				}
				return;
			}
			break;
			// We are currently upgrading/repairing something
		case EngineerTaskStageEnum::SmackSentry:
			iSwitch = F::NavBotEngineer.m_flDistToSentry <= 300.f;
			break;
		case EngineerTaskStageEnum::SmackDispenser:
			iSwitch = F::NavBotEngineer.m_flDistToDispenser <= 500.f;
			break;
		default:
			break;
		}

		if (iSwitch)
		{
			if (iSwitch == 1)
			{
				if (F::BotUtils.m_iCurrentSlot < SLOT_MELEE)
					F::BotUtils.SetSlot(pLocal, SLOT_MELEE);
			}
			return;
		}
	}

	if (F::BotUtils.m_iCurrentSlot != F::BotUtils.m_iBestSlot)
		F::BotUtils.SetSlot(pLocal, iReloadSlot != -1 ? iReloadSlot : Vars::Misc::Movement::BotUtils::WeaponSlot.Value ? F::BotUtils.m_iBestSlot : -1);
}

static bool FindClosestHidingSpotRecursive(CNavArea* pArea, const Vector& vVischeckPoint, int iRecursionCount, std::pair<CNavArea*, int>& tOut, bool bVischeck, int iRecursionIndex, std::vector<CNavArea*>& vVisited)
{
	if (!pArea || iRecursionCount <= 0)
		return false;

	Vector vAreaOrigin = pArea->m_vCenter;
	vAreaOrigin.z += PLAYER_CROUCHED_JUMP_HEIGHT;

	int iNextIndex = iRecursionIndex + 1;

	if (bVischeck && !F::NavEngine.IsVectorVisibleNavigation(vAreaOrigin, vVischeckPoint))
	{
		tOut = { pArea, iRecursionIndex };
		return true;
	}

	if (iNextIndex >= iRecursionCount)
		return false;

	std::pair<CNavArea*, int> tBestSpot{};
	for (auto& tConnection : pArea->m_vConnections)
	{
		CNavArea* pNextArea = tConnection.m_pArea;
		if (!pNextArea)
			continue;

		if (std::find(vVisited.begin(), vVisited.end(), pNextArea) != vVisited.end())
			continue;

		vVisited.push_back(pNextArea);

		std::pair<CNavArea*, int> tSpot;
		if (FindClosestHidingSpotRecursive(pNextArea, vVischeckPoint, iRecursionCount, tSpot, bVischeck, iNextIndex, vVisited) && (!tBestSpot.first || tSpot.second < tBestSpot.second))
			tBestSpot = tSpot;
	}

	tOut = tBestSpot;
	return tBestSpot.first != nullptr;
}

bool CNavBotCore::FindClosestHidingSpot(CNavArea* pArea, Vector vVischeckPoint, int iRecursionCount, std::pair<CNavArea*, int>& tOut, bool bVischeck, int iRecursionIndex)
{
	std::vector<CNavArea*> vVisited;
	vVisited.reserve(32);
	return FindClosestHidingSpotRecursive(pArea, vVischeckPoint, iRecursionCount, tOut, bVischeck, iRecursionIndex, vVisited);
}

static bool IsWeaponValidForDT(CTFWeaponBase* pWeapon)
{
	if (!pWeapon || F::BotUtils.m_iCurrentSlot == SLOT_MELEE)
		return false;

	auto iWepID = pWeapon->GetWeaponID();
	if (iWepID == TF_WEAPON_SNIPERRIFLE || iWepID == TF_WEAPON_SNIPERRIFLE_CLASSIC || iWepID == TF_WEAPON_SNIPERRIFLE_DECAP)
		return false;

	return SDK::WeaponDoesNotUseAmmo(pWeapon, false);
}

void CNavBotCore::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (!Vars::Misc::Movement::NavBot::Enabled.Value || !Vars::Misc::Movement::NavEngine::Enabled.Value ||
		!pLocal->IsAlive() || F::NavEngine.m_eCurrentPriority == PriorityListEnum::Followbot || F::FollowBot.m_bActive || !F::NavEngine.IsReady())
	{
		F::NavBotStayNear.m_iStayNearTargetIdx = -1;
		F::NavBotReload.m_iLastReloadSlot = -1;
		return;
	}

	if (Vars::Debug::Info.Value)
	{
		for (const auto& segment : F::BotUtils.m_vWalkableSegments)
		{
			G::LineStorage.push_back({ { segment.first, segment.second }, I::GlobalVars->curtime + I::GlobalVars->interval_per_tick * 2.f, { 0, 255, 0, 255 } });
		}

		if (F::BotUtils.m_vPredictedJumpPos.Length() > 0.f)
		{
			G::LineStorage.push_back({ { pLocal->GetAbsOrigin(), F::BotUtils.m_vPredictedJumpPos }, I::GlobalVars->curtime + I::GlobalVars->interval_per_tick * 2.f, { 255, 255, 0, 255 } });
			G::SphereStorage.push_back({ F::BotUtils.m_vJumpPeakPos, 5.f, 10, 10, I::GlobalVars->curtime + I::GlobalVars->interval_per_tick * 2.f, { 255, 0, 0, 255 }, { 0, 0, 0, 0 } });
			G::SphereStorage.push_back({ F::BotUtils.m_vPredictedJumpPos, 5.f, 10, 10, I::GlobalVars->curtime + I::GlobalVars->interval_per_tick * 2.f, { 0, 0, 255, 255 }, { 0, 0, 0, 0 } });
		}
	}

	if (Vars::Misc::Movement::NavBot::DisableOnSpectate.Value && H::Entities.IsSpectated())
	{
		F::NavBotStayNear.m_iStayNearTargetIdx = -1;
		F::NavBotReload.m_iLastReloadSlot = -1;
		m_tIdleTimer.Update();
		m_tAntiStuckTimer.Update();
		return;
	}

	if (F::NavEngine.m_eCurrentPriority != PriorityListEnum::StayNear)
		F::NavBotStayNear.m_iStayNearTargetIdx = -1;

	if (F::Ticks.m_bWarp || F::Ticks.m_bDoubletap)
	{
		m_tIdleTimer.Update();
		m_tAntiStuckTimer.Update();
		return;
	}

	if (!pWeapon)
	{
		m_tIdleTimer.Update();
		m_tAntiStuckTimer.Update();
		return;
	}

	if (pCmd->buttons & (IN_FORWARD | IN_BACK | IN_MOVERIGHT | IN_MOVELEFT) && !F::Misc.m_bAntiAFK)
	{
		m_tIdleTimer.Update();
		m_tAntiStuckTimer.Update();
		m_vStuckAngles = pCmd->viewangles;
		return;
	}

	F::NavBotGroup.UpdateLocalBotPositions(pLocal);

	// Update our current nav area
	if (!F::NavEngine.GetLocalNavArea(pLocal->GetAbsOrigin()))
	{
		// This should never happen.
		// In case it did then theres something wrong with nav engine
		F::NavBotStayNear.m_iStayNearTargetIdx = -1;
		F::NavBotReload.m_iLastReloadSlot = -1;
		m_tIdleTimer.Update();
		m_tAntiStuckTimer.Update();
		return;
	}

	// Recharge doubletap every n seconds
	static Timer tDoubletapRecharge{};
	if (Vars::Misc::Movement::NavBot::RechargeDT.Value && IsWeaponValidForDT(pWeapon))
	{
		if (!F::Ticks.m_bRechargeQueue &&
			(Vars::Misc::Movement::NavBot::RechargeDT.Value != Vars::Misc::Movement::NavBot::RechargeDTEnum::WaitForFL || !Vars::Fakelag::Fakelag.Value || !F::FakeLag.m_iGoal) &&
			G::Attacking != 1 &&
			(F::Ticks.m_iShiftedTicks < F::Ticks.m_iShiftedGoal) && tDoubletapRecharge.Check(Vars::Misc::Movement::NavBot::RechargeDTDelay.Value))
			F::Ticks.m_bRechargeQueue = true;
		else if (F::Ticks.m_iShiftedTicks >= F::Ticks.m_iShiftedGoal)
			tDoubletapRecharge.Update();
	}

	// Not used
	// RefreshSniperSpots();
	F::NavBotEngineer.RefreshLocalBuildings(pLocal);
	F::NavBotEngineer.RefreshBuildingSpots(pLocal, F::BotUtils.m_tClosestEnemy);

	// Update the distance config
	switch (pLocal->m_iClass())
	{
	case TF_CLASS_SCOUT:
	case TF_CLASS_HEAVY:
		m_tSelectedConfig = CONFIG_SHORT_RANGE;
		break;
	case TF_CLASS_ENGINEER:
		m_tSelectedConfig = F::NavBotEngineer.IsEngieMode(pLocal) ? pWeapon->m_iItemDefinitionIndex() == Engi_t_TheGunslinger ? CONFIG_GUNSLINGER_ENGINEER : CONFIG_ENGINEER : CONFIG_SHORT_RANGE;
		break;
	case TF_CLASS_SNIPER:
		m_tSelectedConfig = pWeapon->GetWeaponID() == TF_WEAPON_COMPOUND_BOW ? CONFIG_MID_RANGE : CONFIG_LONG_RANGE;
		break;
	default:
		m_tSelectedConfig = CONFIG_MID_RANGE;
	}

	UpdateSlot(pLocal, F::BotUtils.m_tClosestEnemy);
	F::DangerManager.Update(pLocal);

	// TODO:
	// Add engie logic and target sentries logic. (Done)
	// Also maybe add some spy sapper logic? (No.)
	// Fix defend and help capture logic
	// Fix reload stuff because its really janky
	// Finish auto wewapon stuff
	// Make a better closest enemy logic
	// Fix dormant player blacklist not actually running

	if (F::NavBotDanger.EscapeSpawn(pLocal)
		|| F::NavBotDanger.EscapeProjectiles(pLocal)
		|| F::NavBotDanger.EscapeDanger(pLocal)
		|| F::NavBotSupplies.Run(pCmd, pLocal, GetSupplyEnum::Health)
		|| F::NavBotEngineer.Run(pCmd, pLocal, F::BotUtils.m_tClosestEnemy)
		|| F::NavBotReload.Run(pLocal, pWeapon)
		|| F::NavBotMelee.Run(pCmd, pLocal, F::BotUtils.m_iCurrentSlot, F::BotUtils.m_tClosestEnemy)
		|| F::NavBotSupplies.Run(pCmd, pLocal, GetSupplyEnum::Ammo)
		|| F::NavBotCapture.Run(pLocal, pWeapon)
		|| F::NavBotSnipe.Run(pLocal)
		|| F::NavBotReload.RunSafe(pLocal, pWeapon)
		|| F::NavBotStayNear.Run(pLocal, pWeapon)
		|| F::NavBotSupplies.Run(pCmd, pLocal, GetSupplyEnum::Health | GetSupplyEnum::LowPrio)
		|| F::NavBotGroup.Run(pLocal, pWeapon) // Move in formation
		|| F::NavBotRoam.Run(pLocal, pWeapon))
	{
		bool bIsPathing = F::NavEngine.IsPathing();
		if (!bIsPathing)
		{
			// If we have a job but no path, we consider it idle (stuck or waiting for gods agreement to move lol)
		}
		else
		{
			m_tIdleTimer.Update();
			m_tAntiStuckTimer.Update();
		}

		// Force crithack in dangerous conditions
		// TODO:
		// Maybe add some logic to it (more logic)
		CTFPlayer* pPlayer = nullptr;
		switch (F::NavEngine.m_eCurrentPriority)
		{
		case PriorityListEnum::StayNear:
			pPlayer = I::ClientEntityList->GetClientEntity(F::NavBotStayNear.m_iStayNearTargetIdx)->As<CTFPlayer>();
			if (pPlayer)
				F::CritHack.m_bForce = !pPlayer->IsDormant() && pPlayer->m_iHealth() >= pWeapon->GetDamage();
			break;
		case PriorityListEnum::MeleeAttack:
		case PriorityListEnum::GetHealth:
		case PriorityListEnum::EscapeDanger:
			pPlayer = I::ClientEntityList->GetClientEntity(F::BotUtils.m_tClosestEnemy.m_iEntIdx)->As<CTFPlayer>();
			F::CritHack.m_bForce = pPlayer && !pPlayer->IsDormant() && pPlayer->m_iHealth() >= pWeapon->GetDamage();
			break;
		default:
			F::CritHack.m_bForce = false;
			break;
		}
	}
	else if (F::NavEngine.IsReady() && !F::NavEngine.IsSetupTime())
	{
		float flIdleTime = SDK::PlatFloatTime() - m_tIdleTimer.GetLastUpdate();
		if (flIdleTime > m_flNextIdleTime)
		{
			if (flIdleTime < m_flNextIdleTime + 0.5f)
			{
				pCmd->forwardmove = 450.f;

				if (m_tAntiStuckTimer.Run(m_flNextStuckAngleChange))
				{
					m_flNextStuckAngleChange = SDK::RandomFloat(0.1f, 0.3f);
					m_vStuckAngles.y += SDK::RandomFloat(-15.f, 15.f);
					Math::ClampAngles(m_vStuckAngles);
				}

				SDK::FixMovement(pCmd, m_vStuckAngles);
			}
			else
			{
				m_tIdleTimer.Update();
				m_flNextIdleTime = SDK::RandomFloat(4.f, 10.f);
			}
		}
	}
	else
	{
		m_tIdleTimer.Update();
		m_tAntiStuckTimer.Update();
		m_vStuckAngles = pCmd->viewangles;
		m_flNextIdleTime = SDK::RandomFloat(4.f, 10.f);
	}
}

void CNavBotCore::Reset()
{
	F::NavBotStayNear.m_iStayNearTargetIdx = -1;
	F::NavBotReload.m_iLastReloadSlot = -1;
	F::NavBotSnipe.m_iTargetIdx = -1;
	F::NavBotSupplies.ResetTemp();
	F::NavBotEngineer.Reset();
	F::NavBotCapture.Reset();
	m_flNextIdleTime = SDK::RandomFloat(4.f, 10.f);
}

void CNavBotCore::Draw(CTFPlayer* pLocal)
{
	if (!(Vars::Menu::Indicators.Value & Vars::Menu::IndicatorsEnum::NavBot) || !pLocal->IsAlive())
		return;

	auto bIsReady = F::NavEngine.IsReady();
	if (!Vars::Debug::Info.Value && !bIsReady)
		return;

	int x = Vars::Menu::NavBotDisplay.Value.x;
	int y = Vars::Menu::NavBotDisplay.Value.y + 8;
	const auto& fFont = H::Fonts.GetFont(FONT_INDICATORS);
	const int nTall = fFont.m_nTall + H::Draw.Scale(1);

	EAlign align = ALIGN_TOP;
	if (x <= 100 + H::Draw.Scale(50, Scale_Round))
	{
		x -= H::Draw.Scale(42, Scale_Round);
		align = ALIGN_TOPLEFT;
	}
	else if (x >= H::Draw.m_nScreenW - 100 - H::Draw.Scale(50, Scale_Round))
	{
		x += H::Draw.Scale(42, Scale_Round);
		align = ALIGN_TOPRIGHT;
	}

	const auto& cColor = F::NavEngine.IsPathing() ? Vars::Menu::Theme::Active.Value : Vars::Menu::Theme::Inactive.Value;
	const auto& cReadyColor = bIsReady ? Vars::Menu::Theme::Active.Value : Vars::Menu::Theme::Inactive.Value;
	int iInSpawn = -1;
	int iAreaFlags = -1;
	if (F::NavEngine.IsNavMeshLoaded())
	{
		if (auto pLocalArea = F::NavEngine.GetLocalNavArea())
		{
			iAreaFlags = pLocalArea->m_iTFAttributeFlags;
			iInSpawn = iAreaFlags & (TF_NAV_SPAWN_ROOM_BLUE | TF_NAV_SPAWN_ROOM_RED);
		}
	}
	std::wstring sJob = L"None";
	switch (F::NavEngine.m_eCurrentPriority)
	{
	case PriorityListEnum::Patrol:
		sJob = F::NavBotRoam.m_bDefending ? L"Defend" : L"Patrol";
		if (F::NavBotRoam.m_bDefending && !F::NavBotCapture.m_sCaptureStatus.empty())
		{
			sJob += L" (";
			sJob += F::NavBotCapture.m_sCaptureStatus;
			sJob += L")";
		}
		break;
	case PriorityListEnum::LowPrioGetHealth:
		sJob = L"Get health (Low-Prio)";
		break;
	case PriorityListEnum::StayNear:
		sJob = std::format(L"Stalk enemy ({})", F::NavBotStayNear.m_sFollowTargetName.data());
		break;
	case PriorityListEnum::RunReload:
		sJob = L"Run reload";
		break;
	case PriorityListEnum::RunSafeReload:
		sJob = L"Run safe reload";
		break;
	case PriorityListEnum::SnipeSentry:
		sJob = L"Snipe sentry";
		break;
	case PriorityListEnum::GetAmmo:
		sJob = L"Get ammo";
		break;
	case PriorityListEnum::Capture:
		sJob = L"Capture";
		if (!F::NavBotCapture.m_sCaptureStatus.empty())
		{
			sJob += L" (";
			sJob += F::NavBotCapture.m_sCaptureStatus;
			sJob += L")";
		}
		break;
	case PriorityListEnum::MeleeAttack:
		sJob = L"Melee";
		break;
	case PriorityListEnum::Engineer:
		sJob = L"Engineer (";
		switch (F::NavBotEngineer.m_eTaskStage)
		{
		case EngineerTaskStageEnum::BuildSentry:
			sJob += L"Build sentry";
			break;
		case EngineerTaskStageEnum::BuildDispenser:
			sJob += L"Build dispenser";
			break;
		case EngineerTaskStageEnum::SmackSentry:
			sJob += L"Smack sentry";
			break;
		case EngineerTaskStageEnum::SmackDispenser:
			sJob += L"Smack dispenser";
			break;
		default:
			sJob += L"None";
			break;
		}
		sJob += L')';
		break;
	case PriorityListEnum::GetHealth:
		sJob = L"Get health";
		break;
	case PriorityListEnum::EscapeSpawn:
		sJob = L"Escape spawn";
		break;
	case PriorityListEnum::EscapeDanger:
		sJob = L"Escape danger";
		break;
	case PriorityListEnum::Followbot:
		sJob = L"FollowBot";
		break;
	default:
		break;
	}

	H::Draw.StringOutlined(fFont, x, y, cColor, Vars::Menu::Theme::Background.Value, align, std::format(L"Job: {} {}", sJob, std::wstring(F::CritHack.m_bForce ? L"(Crithack on)" : L"")).data());
	if (F::NavEngine.IsPathing())
	{
		auto pCrumbs = F::NavEngine.GetCrumbs();
		float flDist = pLocal->GetAbsOrigin().DistTo(F::NavEngine.m_vLastDestination);
		H::Draw.StringOutlined(fFont, x, y += nTall, cColor, Vars::Menu::Theme::Background.Value, align, std::format("Nodes: {} (Dist: {:.0f})", pCrumbs->size(), flDist).c_str());
	}

	float flIdleTime = SDK::PlatFloatTime() - m_tIdleTimer.GetLastUpdate();
	if (flIdleTime > 2.0f && F::NavEngine.IsPathing())
	{
		H::Draw.StringOutlined(fFont, x, y += nTall, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value, align, std::format("Stuck: {:.1f}s", flIdleTime).c_str());
	}

	if (!F::NavEngine.IsPathing() && !F::NavEngine.m_sLastFailureReason.empty())
	{
		H::Draw.StringOutlined(fFont, x, y += nTall, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value, align, std::format("Failed: {}", F::NavEngine.m_sLastFailureReason).c_str());
	}

	if (Vars::Debug::Info.Value)
	{
		H::Draw.StringOutlined(fFont, x, y += nTall, cReadyColor, Vars::Menu::Theme::Background.Value, align, std::format("Is ready: {}", std::to_string(bIsReady)).c_str());
		H::Draw.StringOutlined(fFont, x, y += nTall, cReadyColor, Vars::Menu::Theme::Background.Value, align, std::format("Priority: {}", static_cast<int>(F::NavEngine.m_eCurrentPriority)).c_str());
		H::Draw.StringOutlined(fFont, x, y += nTall, cReadyColor, Vars::Menu::Theme::Background.Value, align, std::format("In spawn: {}", std::to_string(iInSpawn)).c_str());
		H::Draw.StringOutlined(fFont, x, y += nTall, cReadyColor, Vars::Menu::Theme::Background.Value, align, std::format("Area flags: {}", std::to_string(iAreaFlags)).c_str());

		if (F::NavEngine.IsNavMeshLoaded())
		{
			H::Draw.StringOutlined(fFont, x, y += nTall, cReadyColor, Vars::Menu::Theme::Background.Value, align, std::format("Map: {}", F::NavEngine.GetNavFilePath()).c_str());
			if (auto pLocalArea = F::NavEngine.GetLocalNavArea())
				H::Draw.StringOutlined(fFont, x, y += nTall, cReadyColor, Vars::Menu::Theme::Background.Value, align, std::format("Area ID: {}", pLocalArea->m_uId).c_str());
			H::Draw.StringOutlined(fFont, x, y += nTall, cReadyColor, Vars::Menu::Theme::Background.Value, align, std::format("Total areas: {}", F::NavEngine.GetNavFile()->m_vAreas.size()).c_str());
		}

		if (F::NavEngine.IsPathing() || F::NavEngine.m_vLastDestination.Length() > 0.f)
		{
			const auto& vDest = F::NavEngine.m_vLastDestination;
			H::Draw.StringOutlined(fFont, x, y += nTall, cColor, Vars::Menu::Theme::Background.Value, align, std::format("Dest: {:.0f}, {:.0f}, {:.0f}", vDest.x, vDest.y, vDest.z).c_str());
		}

		float flIdleTime = SDK::PlatFloatTime() - m_tIdleTimer.GetLastUpdate();
		bool bIsIdle = F::NavEngine.m_eCurrentPriority == PriorityListEnum::None || !F::NavEngine.IsPathing();
		H::Draw.StringOutlined(fFont, x, y += nTall, bIsIdle ? Vars::Menu::Theme::Active.Value : Vars::Menu::Theme::Inactive.Value, Vars::Menu::Theme::Background.Value, align, std::format("Idle: {} ({:.1f}s)", bIsIdle ? "Yes" : "No", std::max(0.f, flIdleTime)).c_str());

		if (pLocal && Vars::Misc::Movement::NavBot::DangerOverlay.Value)
		{
			int iDrawn = 0;
			const float flMaxDist = Vars::Misc::Movement::NavBot::DangerOverlayMaxDist.Value;
			const float flMaxDistSqr = flMaxDist * flMaxDist;
			for (const auto& [pArea, tData] : F::DangerManager.GetDangerMap())
			{
				if (!F::NavEngine.GetNavMap() || !F::NavEngine.GetNavMap()->IsAreaValid(pArea) || tData.m_flScore <= 0.f)
					continue;

				if (pArea->m_vCenter.DistToSqr(pLocal->GetAbsOrigin()) > flMaxDistSqr)
					continue;

				Color_t tColor = Color_t(255, 200, 0, 80);
				if (tData.m_flScore >= DANGER_SCORE_STICKY)
					tColor = Color_t(255, 50, 50, 90);
				else if (tData.m_flScore >= DANGER_SCORE_ENEMY_NORMAL)
					tColor = Color_t(255, 140, 0, 90);

				G::SphereStorage.push_back({ pArea->m_vCenter, 24.f, 10, 10, I::GlobalVars->curtime + I::GlobalVars->interval_per_tick * 2.f, tColor, Color_t(), true });

				if (++iDrawn >= 64)
					break;
			}
		}
	}
}
