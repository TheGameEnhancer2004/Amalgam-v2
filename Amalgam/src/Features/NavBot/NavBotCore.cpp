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
#include "NavEngine/NavEngine.h"
#include "../FollowBot/FollowBot.h"
#include "../PacketManip/FakeLag/FakeLag.h"
#include "../CritHack/CritHack.h"
#include "../Ticks/Ticks.h"
#include "../Misc/Misc.h"

void CNavBotCore::UpdateEnemyBlacklist(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, int iSlot)
{
	if (!(Vars::Misc::Movement::NavBot::Blacklist.Value & Vars::Misc::Movement::NavBot::BlacklistEnum::Players) ||
		F::NavEngine.IsBlacklistIrrelevant())
		return;

	if (!(Vars::Misc::Movement::NavBot::Blacklist.Value & Vars::Misc::Movement::NavBot::BlacklistEnum::DormantThreats))
		F::NavEngine.ClearFreeBlacklist(BlacklistReason_t(BlacklistReasonEnum::EnemyDormant));

	if (!(Vars::Misc::Movement::NavBot::Blacklist.Value & Vars::Misc::Movement::NavBot::BlacklistEnum::NormalThreats))
	{
		F::NavEngine.ClearFreeBlacklist(BlacklistReason_t(BlacklistReasonEnum::EnemyNormal));
		return;
	}

	static Timer tBlacklistUpdateTimer{};
	static Timer tDormantUpdateTimer{};
	static int iLastSlotBlacklist = SLOT_PRIMARY;

	bool bShouldRunNormal = tBlacklistUpdateTimer.Run(Vars::Misc::Movement::NavBot::BlacklistDelay.Value) || iLastSlotBlacklist != iSlot;
	bool bShouldRunDormant = Vars::Misc::Movement::NavBot::Blacklist.Value & Vars::Misc::Movement::NavBot::BlacklistEnum::DormantThreats && (tDormantUpdateTimer.Run(Vars::Misc::Movement::NavBot::BlacklistDormantDelay.Value) || iLastSlotBlacklist != iSlot);
	// Don't run since we do not care here
	if (!bShouldRunNormal && !bShouldRunDormant)
		return;

	// Clear blacklist for normal entities
	if (bShouldRunNormal)
		F::NavEngine.ClearFreeBlacklist(BlacklistReason_t(BlacklistReasonEnum::EnemyNormal));

	// Clear blacklist for dormant entities
	if (bShouldRunDormant)
		F::NavEngine.ClearFreeBlacklist(BlacklistReason_t(BlacklistReasonEnum::EnemyDormant));

	if (const auto& pGameRules = I::TFGameRules())
	{
		if (pGameRules->m_iRoundState() == GR_STATE_TEAM_WIN)
			return;
	}

	// #NoFear
	if (iSlot == SLOT_MELEE)
		return;

	// Store the danger of the individual nav areas
	std::unordered_map<CNavArea*, int> mDormantSlightDanger;
	std::unordered_map<CNavArea*, int> mNormalSlightDanger;

	// This is used to cache Dangerous areas between ents
	std::unordered_map<CTFPlayer*, std::vector<CNavArea*>> mEntMarkedDormantSlightDanger;
	std::unordered_map<CTFPlayer*, std::vector<CNavArea*>> mEntMarkedNormalSlightDanger;

	std::vector<std::pair<CTFPlayer*, Vector>> vCheckedPlayerOrigins;
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
	{
		// Entity is generally invalid, ignore
		auto pPlayer = pEntity->As<CTFPlayer>();
		if (!pPlayer->IsAlive())
			continue;

		bool bDormant = pPlayer->IsDormant();
		if (!bDormant)
		{
			// Should not run on normal entity and entity is not dormant, ignore
			if (!bShouldRunNormal)
				continue;
		}
		// Should not run on dormant and entity is dormant, ignore.
		else if (!bShouldRunDormant)
			continue;

		Vector vOrigin;
		if (!F::BotUtils.GetDormantOrigin(pPlayer->entindex(), vOrigin))
			continue;

		vOrigin.z += PLAYER_CROUCHED_JUMP_HEIGHT;

		bool bShouldCheck = true;

		// Find already dangerous marked areas by other entities
		auto mToLoop = bDormant ? &mEntMarkedDormantSlightDanger : &mEntMarkedNormalSlightDanger;

		// Add new danger entries
		auto mToMark = bDormant ? &mDormantSlightDanger : &mNormalSlightDanger;

		for (auto [pCheckedPlayer, vCheckedOrigin] : vCheckedPlayerOrigins)
		{
			// If this origin is closer than a quarter of the min HU (or less than 100 HU) to a cached one, don't go through
			// all nav areas again DistTo is much faster than DistTo which is why we use it here
			float flDist = m_tSelectedConfig.m_flMinSlightDanger;

			flDist *= 0.25f;
			flDist = std::max(100.0f, flDist);

			if (vOrigin.DistTo(vCheckedOrigin) < flDist)
			{
				bShouldCheck = false;

				bool bIsAbsoluteDanger = flDist < m_tSelectedConfig.m_flMinFullDanger;
				if (!bIsAbsoluteDanger && (false/*slight danger when capping*/ || F::NavEngine.m_eCurrentPriority != PriorityListEnum::Capture))
				{
					// The area is not visible by the player
					if (!F::NavEngine.IsVectorVisibleNavigation(vOrigin, vCheckedOrigin, MASK_SHOT))
						continue;

					for (auto& pArea : (*mToLoop)[pCheckedPlayer])
					{
						(*mToMark)[pArea]++;
						if ((*mToMark)[pArea] >= Vars::Misc::Movement::NavBot::BlacklistSlightDangerLimit.Value)
							(*F::NavEngine.GetFreeBlacklist())[pArea] = bDormant ? BlacklistReasonEnum::EnemyDormant : BlacklistReasonEnum::EnemyNormal;
					}
				}
				break;
			}
		}
		if (!bShouldCheck)
			continue;

		// Now check which areas they are close to
		for (auto& tArea : F::NavEngine.GetNavFile()->m_vAreas)
		{
			float flDist = tArea.m_vCenter.DistTo(vOrigin);
			float flSlightDangerDist = m_tSelectedConfig.m_flMinSlightDanger;
			float flFullDangerDist = m_tSelectedConfig.m_flMinFullDanger;

			// Not dangerous, Still don't bump
			if (!F::BotUtils.ShouldTarget(pLocal, pWeapon, pPlayer->entindex()))
			{
				flSlightDangerDist = PLAYER_WIDTH * 1.2f;
				flFullDangerDist = PLAYER_WIDTH * 1.2f;
			}

			if (flDist < flSlightDangerDist)
			{
				Vector vNavAreaPos = tArea.m_vCenter;
				vNavAreaPos.z += PLAYER_CROUCHED_JUMP_HEIGHT;
				// The area is not visible by the player
				if (!F::NavEngine.IsVectorVisibleNavigation(vOrigin, vNavAreaPos, MASK_SHOT))
					continue;

				// Add as marked area
				(*mToLoop)[pPlayer].push_back(&tArea);

				// Just slightly dangerous, only mark as such if it's clear
				if (flDist >= flFullDangerDist &&
					(Vars::Misc::Movement::NavBot::Preferences.Value & Vars::Misc::Movement::NavBot::PreferencesEnum::SafeCapping || F::NavEngine.m_eCurrentPriority != PriorityListEnum::Capture))
				{
					(*mToMark)[&tArea]++;
					if ((*mToMark)[&tArea] < Vars::Misc::Movement::NavBot::BlacklistSlightDangerLimit.Value)
						continue;
				}
				(*F::NavEngine.GetFreeBlacklist())[&tArea] = bDormant ? BlacklistReasonEnum::EnemyDormant : BlacklistReasonEnum::EnemyNormal;
			}
		}
		vCheckedPlayerOrigins.emplace_back(pPlayer, vOrigin);
	}
}

void CNavBotCore::UpdateSlot(CTFPlayer* pLocal, ClosestEnemy_t tClosestEnemy)
{
	static Timer tSlotTimer{};
	if (!tSlotTimer.Run(0.2f))
		return;

	// Prioritize reloading
	int iReloadSlot = F::NavBotReload.m_iLastReloadSlot = F::NavBotReload.GetReloadWeaponSlot(pLocal, tClosestEnemy);

	// Special case for engineer bots
	if (F::NavBotEngineer.IsEngieMode(pLocal))
	{
		int iSwitch = 0;
		switch (F::NavBotEngineer.m_eTaskStage)
		{
		// We are currently building something (we dont really need to hold the melee weapon)
		case EngineerTaskStageEnum::BuildSentry:
		case EngineerTaskStageEnum::BuildDispenser:
			iSwitch = 2 * (F::NavBotEngineer.m_tCurrentBuildingSpot.m_flDistanceToTarget != FLT_MAX && F::NavBotEngineer.m_tCurrentBuildingSpot.m_vPos.DistTo(pLocal->GetAbsOrigin()) <= 500.f);
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
			// Dont interrupt building process
			return;
		}
	}

	if (F::BotUtils.m_iCurrentSlot != F::BotUtils.m_iBestSlot)
		F::BotUtils.SetSlot(pLocal, iReloadSlot != -1 ? iReloadSlot : Vars::Misc::Movement::BotUtils::WeaponSlot.Value ? F::BotUtils.m_iBestSlot : -1);
}

bool CNavBotCore::FindClosestHidingSpot(CNavArea* pArea, Vector vVischeckPoint, int iRecursionCount, std::pair<CNavArea*, int>& tOut, bool bVischeck, int iRecursionIndex)
{
	static std::vector<CNavArea*> vAlreadyRecursed;
	if (iRecursionIndex == 0)
		vAlreadyRecursed.clear();

	Vector vAreaOrigin = pArea->m_vCenter;
	vAreaOrigin.z += PLAYER_CROUCHED_JUMP_HEIGHT;

	// Increment recursion index
	iRecursionIndex++;

	// If the area works, return it
	if (bVischeck && !F::NavEngine.IsVectorVisibleNavigation(vAreaOrigin, vVischeckPoint))
	{
		tOut = { pArea, iRecursionIndex - 1 };
		return true;
	}
	// Termination condition not hit yet
	else if (iRecursionIndex < iRecursionCount)
	{
		// Store the nearest area
		std::pair<CNavArea*, int> tBestSpot;
		for (auto& tConnection : pArea->m_vConnections)
		{
			if (std::find(vAlreadyRecursed.begin(), vAlreadyRecursed.end(), tConnection.m_pArea) != vAlreadyRecursed.end())
				continue;

			vAlreadyRecursed.push_back(tConnection.m_pArea);

			std::pair<CNavArea*, int> tSpot;
			if (FindClosestHidingSpot(tConnection.m_pArea, vVischeckPoint, iRecursionCount, tSpot, iRecursionIndex, bVischeck) && (!tBestSpot.first || tSpot.second < tBestSpot.second))
				tBestSpot = tSpot;
		}
		tOut = tBestSpot;
		return tBestSpot.first;
	}
	return false;
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

	if (Vars::Misc::Movement::NavBot::DisableOnSpectate.Value && H::Entities.IsSpectated())
	{
		F::NavBotStayNear.m_iStayNearTargetIdx = -1;
		F::NavBotReload.m_iLastReloadSlot = -1;
		return;
	}

	if (F::NavEngine.m_eCurrentPriority != PriorityListEnum::StayNear)
		F::NavBotStayNear.m_iStayNearTargetIdx = -1;

	if (F::Ticks.m_bWarp || F::Ticks.m_bDoubletap)
		return;

	if (!pWeapon)
		return;

	if (pCmd->buttons & (IN_FORWARD | IN_BACK | IN_MOVERIGHT | IN_MOVELEFT) && !F::Misc.m_bAntiAFK)
		return;

	F::NavBotGroup.UpdateLocalBotPositions(pLocal);

	// Update our current nav area
	if (!F::NavEngine.GetLocalNavArea(pLocal->GetAbsOrigin()))
	{
		// This should never happen.
		// In case it did then theres something wrong with nav engine
		F::NavBotStayNear.m_iStayNearTargetIdx = -1;
		F::NavBotReload.m_iLastReloadSlot = -1;
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
	UpdateEnemyBlacklist(pLocal, pWeapon, F::BotUtils.m_iCurrentSlot);

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
		|| F::NavBotMelee.Run(pCmd, pLocal, F::BotUtils.m_iCurrentSlot, F::BotUtils.m_tClosestEnemy)
		|| F::NavBotDanger.EscapeDanger(pLocal)
		|| F::NavBotSupplies.Run(pCmd, pLocal, GetSupplyEnum::Health)
		|| F::NavBotSupplies.Run(pCmd, pLocal, GetSupplyEnum::Ammo)
		//|| F::NavBotReload.Run(pLocal, pWeapon)
		|| F::NavBotReload.RunSafe(pLocal, pWeapon)
		|| F::NavBotGroup.Run(pLocal, pWeapon) // Move in formation
		|| F::NavBotCapture.Run(pLocal, pWeapon)
		|| F::NavBotEngineer.Run(pCmd, pLocal, F::BotUtils.m_tClosestEnemy)
		|| F::NavBotSnipe.Run(pLocal)
		|| F::NavBotStayNear.Run(pLocal, pWeapon)
		|| F::NavBotSupplies.Run(pCmd, pLocal, GetSupplyEnum::Health | GetSupplyEnum::LowPrio)
		|| F::NavBotRoam.Run(pLocal, pWeapon))
	{
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
}

void CNavBotCore::Reset()
{
	F::NavBotStayNear.m_iStayNearTargetIdx = -1;
	F::NavBotReload.m_iLastReloadSlot = -1;
	F::NavBotSnipe.m_iTargetIdx = -1;
	F::NavBotSupplies.ResetTemp();
	F::NavBotEngineer.Reset();
	F::NavBotCapture.Reset();
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
	if (Vars::Debug::Info.Value)
	{
		H::Draw.StringOutlined(fFont, x, y += nTall, cReadyColor, Vars::Menu::Theme::Background.Value, align, std::format("Is ready: {}", std::to_string(bIsReady)).c_str());
		H::Draw.StringOutlined(fFont, x, y += nTall, cReadyColor, Vars::Menu::Theme::Background.Value, align, std::format("In spawn: {}", std::to_string(iInSpawn)).c_str());
		H::Draw.StringOutlined(fFont, x, y += nTall, cReadyColor, Vars::Menu::Theme::Background.Value, align, std::format("Area flags: {}", std::to_string(iAreaFlags)).c_str());
	}
}
