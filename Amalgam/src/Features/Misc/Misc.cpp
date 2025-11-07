#include "Misc.h"

#include "../Backtrack/Backtrack.h"
#include "../Ticks/Ticks.h"
#include "../Players/PlayerUtils.h"
#include "../Aimbot/AutoRocketJump/AutoRocketJump.h"
#ifdef TEXTMODE
#include "NamedPipe/NamedPipe.h"
#endif
#include <fstream>

MAKE_SIGNATURE(Voice_IsRecording, "engine.dll", "80 3D ? ? ? ? ? 74 ? 80 3D ? ? ? ? ? 75", 0x0);
MAKE_SIGNATURE(ReportPlayerAccount, "client.dll", "48 89 5C 24 ? 57 48 83 EC ? 48 8B D9 8B FA 48 C1 E9", 0x0);

CMisc::NameDumpResult_t CMisc::DumpNames(bool bAnnounce)
{
	NameDumpResult_t tResult{};
	auto pResource = H::Entities.GetResource();
	if (!pResource)
	{
		if (bAnnounce)
			SDK::Output("DisplayNameDumper", "Player resource unavailable");
		return tResult;
	}
	tResult.m_bResourceAvailable = true;

	auto SanitizeName = [](const char* sRaw) -> std::string
	{
		if (!sRaw)
			return {};

		std::string sClean;
		sClean.reserve(std::strlen(sRaw));
		for (unsigned char c : std::string_view{sRaw})
		{
			if (c < 32 || c > 126)
				continue;
			if (c == ',')
				return {};
			sClean.push_back(static_cast<char>(c));
		}
		return sClean;
	};

	std::vector<std::string> vNames;
	std::unordered_set<std::string> setSessionNames;
	vNames.reserve(I::EngineClient->GetMaxClients());
	setSessionNames.reserve(I::EngineClient->GetMaxClients());

	const int iLocalPlayer = I::EngineClient->GetLocalPlayer();
	for (int n = 1; n <= I::EngineClient->GetMaxClients(); n++)
	{
		if (n == iLocalPlayer)
			continue;

		if (!pResource->m_bValid(n) || !pResource->m_bConnected(n) || pResource->IsFakePlayer(n))
			continue;

		tResult.m_uCandidateCount++;

		const char* pszName = pResource->GetName(n);
		if (!pszName)
		{
			tResult.m_uSkippedInvalid++;
			continue;
		}

		if (std::strchr(pszName, ','))
		{
			tResult.m_uSkippedComma++;
			continue;
		}

		std::string sClean = SanitizeName(pszName);
		if (sClean.empty())
		{
			tResult.m_uSkippedInvalid++;
			continue;
		}

		if (!setSessionNames.emplace(sClean).second)
		{
			tResult.m_uSkippedSessionDuplicate++;
			continue;
		}

		vNames.emplace_back(std::move(sClean));
	}

	if (!tResult.m_uCandidateCount)
	{
		if (bAnnounce)
			SDK::Output("DisplayNameDumper", "No player names found");
		return tResult;
	}

	if (vNames.empty())
	{
		if (bAnnounce)
		{
			const char* pszReason = tResult.m_uSkippedComma ? "All player names contained commas" : "No valid player names to save";
			SDK::Output("DisplayNameDumper", pszReason);
		}
		return tResult;
	}

	auto sPath = std::filesystem::current_path() / "Amalgam" / "names.txt";
	std::error_code ec;
	std::filesystem::create_directories(sPath.parent_path(), ec);

	std::unordered_set<std::string> setExistingNames;
	setExistingNames.reserve(vNames.size() * 2);

	bool bAppendComma = false;
	if (std::filesystem::exists(sPath))
	{
		std::ifstream input(sPath);
		if (input)
		{
			tResult.m_bFileOpened = true;
			std::string sToken;
			while (std::getline(input, sToken, ','))
			{
				while (!sToken.empty() && (sToken.back() == '\n' || sToken.back() == '\r'))
					sToken.pop_back();
				if (!sToken.empty())
					setExistingNames.emplace(sToken);
			}

			input.clear();
			input.seekg(0, std::ios::end);
			bAppendComma = input.tellg() > 0;
		}
		else if (bAnnounce)
			SDK::Output("DisplayNameDumper", std::format("Failed to read existing names from {}", sPath.string()).c_str());
	}

	std::vector<std::string> vNewNames;
	vNewNames.reserve(vNames.size());
	for (const auto& sName : vNames)
	{
		if (setExistingNames.contains(sName))
		{
			tResult.m_uSkippedFileDuplicate++;
			continue;
		}

		setExistingNames.emplace(sName);
		vNewNames.emplace_back(sName);
	}

	tResult.m_uAppendedCount = vNewNames.size();
	tResult.m_outputPath = sPath;

	if (vNewNames.empty())
	{
		if (bAnnounce)
		{
			SDK::Output("DisplayNameDumper", std::format("No new names to save ({} duplicates skipped, {} comma filtered)",
				tResult.m_uSkippedSessionDuplicate + tResult.m_uSkippedFileDuplicate,
				tResult.m_uSkippedComma).c_str());
		}
		return tResult;
	}

	std::ofstream file(sPath, std::ios::app);
	if (!file)
	{
		if (bAnnounce)
			SDK::Output("DisplayNameDumper", std::format("Failed to open {}", sPath.string()).c_str());
		return tResult;
	}

	tResult.m_bFileOpened = true;

	if (bAppendComma)
		file << ',';

	for (size_t i = 0; i < vNewNames.size(); i++)
	{
		if (i)
			file << ',';
		file << vNewNames[i];
	}

	if (!file.good())
	{
		if (bAnnounce)
			SDK::Output("DisplayNameDumper", "Failed to write names");
		return tResult;
	}
	tResult.m_bSuccess = true;

	if (bAnnounce)
	{
		SDK::Output("DisplayNameDumper", std::format("Saved {} new names to {} ({} duplicates skipped, {} comma filtered)",
			tResult.m_uAppendedCount,
			sPath.string(),
			tResult.m_uSkippedSessionDuplicate + tResult.m_uSkippedFileDuplicate,
			tResult.m_uSkippedComma).c_str());
	}

	return tResult;
}

void CMisc::RunPre(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	NoiseSpam(pLocal);
	VoiceCommandSpam(pLocal);
	ChatSpam(pLocal);
	AchievementSpam(pLocal);
	CallVoteSpam(pLocal);
	CheatsBypass();
	WeaponSway();
	AutoReport();

#ifdef TEXTMODE
	F::NamedPipe.Store(pLocal, true);
#endif
	AntiAFK(pLocal, pCmd);
	InstantRespawnMVM(pLocal);
	RandomVotekick(pLocal);
	ExecBuyBot(pLocal);

	if (!pLocal->IsAlive() || pLocal->IsAGhost() || pLocal->m_MoveType() != MOVETYPE_WALK || pLocal->IsSwimming()
		|| pLocal->IsTaunting() || pLocal->InCond(TF_COND_HALLOWEEN_KART) || pLocal->InCond(TF_COND_SHIELD_CHARGE))
		return;

	AutoJump(pLocal, pCmd);
	EdgeJump(pLocal, pCmd);
	AutoJumpbug(pLocal, pCmd);
	AutoStrafe(pLocal, pCmd);
	AutoPeek(pLocal, pCmd);
	BreakJump(pLocal, pCmd);
}

void CMisc::RunPost(CTFPlayer* pLocal, CUserCmd* pCmd, bool pSendPacket)
{
	if (!pLocal->IsAlive() || pLocal->IsAGhost() || pLocal->m_MoveType() != MOVETYPE_WALK || pLocal->IsSwimming()
		|| pLocal->InCond(TF_COND_SHIELD_CHARGE))
		return;

	if (pLocal->IsTaunting() || pLocal->InCond(TF_COND_HALLOWEEN_KART))
		TauntKartControl(pLocal, pCmd);
	else
	{
		EdgeJump(pLocal, pCmd, true);
		AutoPeek(pLocal, pCmd, true);
		FastMovement(pLocal, pCmd);
		BreakShootSound(pLocal, pCmd);
		MovementLock(pLocal, pCmd);
	}
}



void CMisc::AutoJump(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!Vars::Misc::Movement::Bunnyhop.Value)
		return;

	if (auto pWeapon = H::Entities.GetWeapon(); pWeapon && pWeapon->GetWeaponID() == TF_WEAPON_GRAPPLINGHOOK && pWeapon->As<CTFGrapplingHook>()->m_hProjectile())
		return;

	static bool bStaticJump = false, bStaticGrounded = false, bLastAttempted = false;
	const bool bLastJump = bStaticJump, bLastGrounded = bStaticGrounded;
	const bool bCurJump = bStaticJump = pCmd->buttons & IN_JUMP, bCurGrounded = bStaticGrounded = pLocal->m_hGroundEntity();

	if (bCurJump && bLastJump && (bCurGrounded ? !pLocal->IsDucking() : true))
	{
		if (!(bCurGrounded && !bLastGrounded))
			pCmd->buttons &= ~IN_JUMP;

		if (!(pCmd->buttons & IN_JUMP) && bCurGrounded && !bLastAttempted)
			pCmd->buttons |= IN_JUMP;
	}

	if (Vars::Misc::Game::AntiCheatCompatibility.Value)
	{	// prevent more than 9 bhops occurring. if a server has this under that threshold they're retarded anyways
		static int iJumps = 0;
		if (bCurGrounded)
		{
			if (!bLastGrounded && pCmd->buttons & IN_JUMP)
				iJumps++;
			else
				iJumps = 0;

			if (iJumps > 9)
				pCmd->buttons &= ~IN_JUMP;
		}
	}
	bLastAttempted = pCmd->buttons & IN_JUMP;
}

void CMisc::AutoJumpbug(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!Vars::Misc::Movement::AutoJumpbug.Value || !(pCmd->buttons & IN_DUCK) || pLocal->m_hGroundEntity() || pLocal->m_vecVelocity().z > -650.f)
		return;

	float flUnduckHeight = 20 * pLocal->m_flModelScale();
	float flTraceDistance = flUnduckHeight + 2;

	CGameTrace trace = {};
	CTraceFilterWorldAndPropsOnly filter = {};

	Vec3 vOrigin = pLocal->m_vecOrigin();
	SDK::TraceHull(vOrigin, vOrigin - Vec3(0, 0, flTraceDistance), pLocal->m_vecMins(), pLocal->m_vecMaxs(), pLocal->SolidMask(), &filter, &trace);
	if (!trace.DidHit() || trace.fraction * flTraceDistance < flUnduckHeight) // don't try if we aren't in range to unduck or are too low
		return;

	pCmd->buttons &= ~IN_DUCK;
	pCmd->buttons |= IN_JUMP;
}

void CMisc::AutoStrafe(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!Vars::Misc::Movement::AutoStrafe.Value || pLocal->m_hGroundEntity() || !(pLocal->m_afButtonLast() & IN_JUMP) && (pCmd->buttons & IN_JUMP))
		return;

	switch (Vars::Misc::Movement::AutoStrafe.Value)
	{
	case Vars::Misc::Movement::AutoStrafeEnum::Legit:
	{
		static auto cl_sidespeed = U::ConVars.FindVar("cl_sidespeed");
		const float flSideSpeed = cl_sidespeed->GetFloat();

		if (pCmd->mousedx)
		{
			pCmd->forwardmove = 0.f;
			pCmd->sidemove = pCmd->mousedx > 0 ? flSideSpeed : -flSideSpeed;
		}
		break;
	}
	case Vars::Misc::Movement::AutoStrafeEnum::Directional:
	{
		// credits: KGB
		if (!(pCmd->buttons & (IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT)))
			break;

		float flForward = pCmd->forwardmove, flSide = pCmd->sidemove;

		Vec3 vForward, vRight; Math::AngleVectors(pCmd->viewangles, &vForward, &vRight, nullptr);
		vForward.Normalize2D(), vRight.Normalize2D();

		Vec3 vWishDir = Math::VectorAngles({ vForward.x * flForward + vRight.x * flSide, vForward.y * flForward + vRight.y * flSide, 0.f });
		Vec3 vCurDir = Math::VectorAngles(pLocal->m_vecVelocity());
		float flDirDelta = Math::NormalizeAngle(vWishDir.y - vCurDir.y);
		if (fabsf(flDirDelta) > Vars::Misc::Movement::AutoStrafeMaxDelta.Value)
			break;

		float flTurnScale = Math::RemapVal(Vars::Misc::Movement::AutoStrafeTurnScale.Value, 0.f, 1.f, 0.9f, 1.f);
		float flRotation = DEG2RAD((flDirDelta > 0.f ? -90.f : 90.f) + flDirDelta * flTurnScale);
		float flCosRot = cosf(flRotation), flSinRot = sinf(flRotation);

		pCmd->forwardmove = flCosRot * flForward - flSinRot * flSide;
		pCmd->sidemove = flSinRot * flForward + flCosRot * flSide;
	}
	}
}

void CMisc::MovementLock(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	static bool bLock = false;

	if (!Vars::Misc::Movement::MovementLock.Value || pLocal->InCond(TF_COND_HALLOWEEN_KART))
	{
		bLock = false;
		return;
	}

	static Vec3 vMove = {}, vView = {};
	if (!bLock)
	{
		bLock = true;
		vMove = { pCmd->forwardmove, pCmd->sidemove, pCmd->upmove };
		vView = pCmd->viewangles;
	}

	pCmd->forwardmove = vMove.x, pCmd->sidemove = vMove.y, pCmd->upmove = vMove.z;
	SDK::FixMovement(pCmd, vView, pCmd->viewangles);
}

void CMisc::BreakJump(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!Vars::Misc::Movement::BreakJump.Value || F::AutoRocketJump.IsRunning())
		return;

	static bool bStaticJump = false;
	const bool bLastJump = bStaticJump;
	const bool bCurrJump = bStaticJump = pCmd->buttons & IN_JUMP;

	static int iTickSinceGrounded = -1;
	if (pLocal->m_hGroundEntity().Get())
		iTickSinceGrounded = -1;
	iTickSinceGrounded++;

	switch (iTickSinceGrounded)
	{
	case 0:
		if (bLastJump || !bCurrJump || pLocal->IsDucking())
			return;
		break;
	case 1:
		break;
	default:
		return;
	}

	pCmd->buttons |= IN_DUCK;
}

void CMisc::BreakShootSound(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	static int iOriginalWeaponSlot = -1;
	auto pWeapon = H::Entities.GetWeapon();
	if (!Vars::Misc::Exploits::BreakShootSound.Value || F::Ticks.m_bDoubletap || pLocal->m_iClass() != TF_CLASS_SOLDIER || !pWeapon)
		return;

	static bool bLastWasInAttack = false;
	static int iLastWeaponSelect = -1;
	const int iCurrSlot = pWeapon->GetSlot();
	if (pCmd->weaponselect && pCmd->weaponselect != iLastWeaponSelect)
		iOriginalWeaponSlot = -1;

	auto pSwap = iCurrSlot == SLOT_SECONDARY ? pLocal->GetWeaponFromSlot(SLOT_PRIMARY) : iCurrSlot == SLOT_PRIMARY ? pLocal->GetWeaponFromSlot(SLOT_SECONDARY) : pLocal->GetWeaponFromSlot(iOriginalWeaponSlot);
	if (pSwap && pSwap->CanBeSelected() && !pCmd->weaponselect)
	{
		if (bLastWasInAttack)
		{
			if (iOriginalWeaponSlot < 0)
			{
				iOriginalWeaponSlot = iCurrSlot;
				pCmd->weaponselect = pSwap->entindex();
				iLastWeaponSelect = pCmd->weaponselect;
			}
		}
		else
		{
			if (iOriginalWeaponSlot == iCurrSlot && G::CanPrimaryAttack)
				iOriginalWeaponSlot = -1;
			else if (iOriginalWeaponSlot >= 0 && iOriginalWeaponSlot != iCurrSlot)
			{
				pCmd->weaponselect = pSwap->entindex();
				iLastWeaponSelect = pCmd->weaponselect;
			}
		}
	}

	bLastWasInAttack = G::Attacking == 1 && G::CanPrimaryAttack;
}

void CMisc::AntiAFK(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	static Timer tTimer = {};
	m_bAntiAFK = false;
	static auto mp_idledealmethod = U::ConVars.FindVar("mp_idledealmethod");
	static auto mp_idlemaxtime = U::ConVars.FindVar("mp_idlemaxtime");
	const int iIdleMethod = mp_idledealmethod->GetInt();
	const float flMaxIdleTime = mp_idlemaxtime->GetFloat();
	static bool bForce = false;

	// Just in case there's a connection problem
	auto pNetChan = I::EngineClient->GetNetChannelInfo();
	bool bTimingOut = pNetChan && pNetChan->IsTimingOut();
	if (bTimingOut) 
		bForce = true;

	if (pCmd->buttons & (IN_MOVELEFT | IN_MOVERIGHT | IN_FORWARD | IN_BACK) || !pLocal->IsAlive())
	{
		tTimer.Update();
		bForce = false;
	}
	else if (Vars::Misc::Automation::AntiAFK.Value && iIdleMethod && (tTimer.Check(flMaxIdleTime * 60.f - 10.f) || (!bTimingOut && bForce))) // trigger 10 seconds before kick
	{
		pCmd->buttons |= I::GlobalVars->tickcount % 2 ? IN_FORWARD : IN_BACK;
		tTimer.Update();
		bForce = false;
		m_bAntiAFK = true;
	}
}

void CMisc::InstantRespawnMVM(CTFPlayer* pLocal)
{
	if (!Vars::Misc::MannVsMachine::InstantRespawn.Value || pLocal->IsAlive())
		return;

	KeyValues* kv = new KeyValues("MVM_Revive_Response");
	kv->SetBool("accepted", true);
	I::EngineClient->ServerCmdKeyValues(kv);
}

void CMisc::NoiseSpam(CTFPlayer* pLocal)
{
	if (!Vars::Misc::Automation::NoiseSpam.Value || pLocal->m_bUsingActionSlot())
		return;

	static float flLastSpamTime = 0.0f;
	float flCurrentTime = SDK::PlatFloatTime();
	if (flCurrentTime - flLastSpamTime < 0.2f)
		return;

	flLastSpamTime = flCurrentTime;
	I::EngineClient->ServerCmdKeyValues(new KeyValues("use_action_slot_item_server"));
}

void CMisc::CallVoteSpam(CTFPlayer* pLocal)
{
	if (!Vars::Misc::Automation::CallVoteSpam.Value || !m_tCallVoteSpamTimer.Run(1.0f))
		return;

	std::vector<std::string> vVoteOptions = {
		"callvote changelevel cp_badlands",
		"callvote changelevel cp_granary",
		"callvote changelevel cp_well",
		"callvote changelevel cp_5gorge",
		"callvote changelevel cp_freight_final1",
		"callvote changelevel cp_yukon_final",
		"callvote changelevel cp_gravelpit",
		"callvote changelevel cp_dustbowl",
		"callvote changelevel cp_egypt_final",
		"callvote changelevel cp_junction_final",
		"callvote changelevel cp_steel",
		"callvote changelevel ctf_2fort",
		"callvote changelevel ctf_well",
		"callvote changelevel ctf_sawmill",
		"callvote changelevel ctf_turbine",
		"callvote changelevel ctf_doublecross",
		"callvote changelevel pl_badwater",
		"callvote changelevel pl_goldrush",
		"callvote changelevel pl_dustbowl",
		"callvote changelevel pl_upward",
		"callvote changelevel pl_thundermountain",
		"callvote changelevel koth_harvest_final",
		"callvote changelevel koth_nucleus",
		"callvote changelevel koth_sawmill",
		"callvote changelevel koth_viaduct",
		"callvote changelevel cp_5gorge",
		"callvote changelevel cp_dustbowl",
		"callvote changelevel ctf_2fort",
		"callvote changelevel ctf_doublecross",
		"callvote changelevel ctf_turbine",
		"callvote changelevel koth_brazil",
		"callvote changelevel pl_badwater",
		"callvote changelevel pl_pheonix",
		"callvote changelevel plr_bananabay",
		"callvote changelevel plr_hightower",
		"callvote scrambleteams"
	};

	int iRandomIndex = SDK::RandomInt(0, static_cast<int>(vVoteOptions.size()) - 1);
	std::string strSelectedVote = vVoteOptions[iRandomIndex];

	I::ClientState->SendStringCmd(strSelectedVote.c_str());
}

void CMisc::CheatsBypass()
{
	static bool bCheatSet = false;
	static auto sv_cheats = U::ConVars.FindVar("sv_cheats");
	if (Vars::Misc::Exploits::CheatsBypass.Value)
	{
		sv_cheats->m_nValue = 1;
		bCheatSet = true;
	}
	else if (bCheatSet)
	{
		sv_cheats->m_nValue = 0;
		bCheatSet = false;
	}
}

void CMisc::WeaponSway()
{
	static auto cl_wpn_sway_interp = U::ConVars.FindVar("cl_wpn_sway_interp");
	static auto cl_wpn_sway_scale = U::ConVars.FindVar("cl_wpn_sway_scale");

	bool bSway = Vars::Visuals::Viewmodel::SwayInterp.Value || Vars::Visuals::Viewmodel::SwayScale.Value;
	cl_wpn_sway_interp->SetValue(bSway ? Vars::Visuals::Viewmodel::SwayInterp.Value : 0.f);
	cl_wpn_sway_scale->SetValue(bSway ? Vars::Visuals::Viewmodel::SwayScale.Value : 0.f);
}

void CMisc::TauntKartControl(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (Vars::Misc::Automation::TauntControl.Value && pLocal->IsTaunting() && pLocal->m_bAllowMoveDuringTaunt())
	{
		if (pLocal->m_bTauntForceMoveForward())
		{
			if (pCmd->buttons & IN_BACK)
				pCmd->viewangles.x = 91.f;
			else if (!(pCmd->buttons & IN_FORWARD))
				pCmd->viewangles.x = 90.f;
		}
		if (pCmd->buttons & IN_MOVELEFT)
			pCmd->sidemove = pCmd->viewangles.x == 90.f ? -450.f : -pLocal->m_flTauntForceMoveForwardSpeed();
		else if (pCmd->buttons & IN_MOVERIGHT)
			pCmd->sidemove = pCmd->viewangles.x == 90.f ? 450.f : pLocal->m_flTauntForceMoveForwardSpeed();
	}
	else if (Vars::Misc::Automation::KartControl.Value && pLocal->InCond(TF_COND_HALLOWEEN_KART))
	{
		const bool bForward = pCmd->buttons & IN_FORWARD;
		const bool bBack = pCmd->buttons & IN_BACK;
		const bool bLeft = pCmd->buttons & IN_MOVELEFT;
		const bool bRight = pCmd->buttons & IN_MOVERIGHT;

		const bool flipVar = I::GlobalVars->tickcount % 2;
		if (bForward && (!bLeft && !bRight || !flipVar))
		{
			pCmd->forwardmove = 450.f;
			pCmd->viewangles.x = 0.f;
		}
		else if (bBack && (!bLeft && !bRight || !flipVar))
		{
			pCmd->forwardmove = 450.f;
			pCmd->viewangles.x = 91.f;
		}
		else if (pCmd->buttons & (IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT))
		{
			if (flipVar || !F::Ticks.CanChoke())
			{	// you could just do this if you didn't care about viewangles
				Vec3 vMove = { pCmd->forwardmove, pCmd->sidemove, 0.f };
				Vec3 vAngMoveReverse = Math::VectorAngles(vMove * -1.f);
				pCmd->forwardmove = -vMove.Length();
				pCmd->sidemove = 0.f;
				pCmd->viewangles.y = fmodf(pCmd->viewangles.y - vAngMoveReverse.y, 360.f);
				pCmd->viewangles.z = 270.f;
				G::PSilentAngles = true;
			}
		}
		else
			pCmd->viewangles.x = 90.f;

		G::SilentAngles = true;
	}
}

void CMisc::FastMovement(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!pLocal->m_hGroundEntity() || pLocal->InCond(TF_COND_HALLOWEEN_KART))
		return;

	const float flSpeed = pLocal->m_vecVelocity().Length2D();
	const int flMaxSpeed = std::min(pLocal->m_flMaxspeed() * 0.9f, 520.f) - 10.f;
	const int iRun = !pCmd->forwardmove && !pCmd->sidemove ? 0 : flSpeed < flMaxSpeed ? 1 : 2;

	switch (iRun)
	{
	case 0:
	{
		if (!Vars::Misc::Movement::FastStop.Value || !flSpeed)
			return;

		Vec3 vDirection = pLocal->m_vecVelocity().ToAngle();
		vDirection.y = pCmd->viewangles.y - vDirection.y;
		Vec3 vNegatedDirection = vDirection.FromAngle() * -flSpeed;
		pCmd->forwardmove = vNegatedDirection.x;
		pCmd->sidemove = vNegatedDirection.y;

		break;
	}
	case 1:
	{
		if ((pLocal->IsDucking() ? !Vars::Misc::Movement::DuckSpeed.Value : !Vars::Misc::Movement::FastAccelerate.Value)
			|| Vars::Misc::Game::AntiCheatCompatibility.Value
			|| G::Attacking == 1 || F::Ticks.m_bDoubletap || F::Ticks.m_bSpeedhack || F::Ticks.m_bRecharge || G::AntiAim || I::GlobalVars->tickcount % 2)
			return;

		if (!(pCmd->buttons & (IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT)))
			return;

		Vec3 vMove = { pCmd->forwardmove, pCmd->sidemove, 0.f };
		Vec3 vAngMoveReverse = Math::VectorAngles(vMove * -1.f);
		pCmd->forwardmove = -vMove.Length();
		pCmd->sidemove = 0.f;
		pCmd->viewangles.y = fmodf(pCmd->viewangles.y - vAngMoveReverse.y, 360.f);
		pCmd->viewangles.z = 270.f;
		G::PSilentAngles = true;

		break;
	}
	}
}

void CMisc::AutoPeek(CTFPlayer* pLocal, CUserCmd* pCmd, bool bPost)
{
	static bool bReturning = false;

	if (!bPost)
	{
		if (Vars::AutoPeek::Enabled.Value)
		{
			Vec3 vLocalPos = pLocal->m_vecOrigin();

			if (bReturning)
			{
				if (vLocalPos.DistTo2D(m_vPeekReturnPos) < 8.f)
				{
					bReturning = false;
					return;
				}

				SDK::WalkTo(pCmd, pLocal, m_vPeekReturnPos);
				pCmd->buttons &= ~IN_JUMP;
			}
			else if (!pLocal->m_hGroundEntity())
				m_bPeekPlaced = false;

			if (!m_bPeekPlaced)
			{
				m_vPeekReturnPos = vLocalPos;
				m_bPeekPlaced = true;
			}
			else
			{
				static Timer tTimer = {};
				if (tTimer.Run(0.7f))
					H::Particles.DispatchParticleEffect("ping_circle", m_vPeekReturnPos, {});
			}
		}
		else
			m_bPeekPlaced = bReturning = false;
	}
	else if (G::Attacking && m_bPeekPlaced)
		bReturning = true;
}

void CMisc::EdgeJump(CTFPlayer* pLocal, CUserCmd* pCmd, bool bPost)
{
	if (!Vars::Misc::Movement::EdgeJump.Value)
		return;

	static bool bStaticGround = false;
	if (!bPost)
		bStaticGround = pLocal->m_hGroundEntity();
	else if (bStaticGround && !pLocal->m_hGroundEntity())
		pCmd->buttons |= IN_JUMP;
}

void CMisc::Event(IGameEvent* pEvent, uint32_t uHash)
{
	switch (uHash)
	{
	case FNV1A::Hash32Const("client_disconnect"):
	case FNV1A::Hash32Const("client_beginconnect"):
	case FNV1A::Hash32Const("game_newmap"):
		m_vChatSpamLines.clear();
		m_iCurrentChatSpamIndex = 0;
		break;
	case FNV1A::Hash32Const("player_spawn"):
		m_bPeekPlaced = false;
		break;
	case FNV1A::Hash32Const("player_death"):
	{
		if (!Vars::Misc::Automation::AutoTaunt.Value)
			break;

		const auto pLocal = H::Entities.GetLocal();
		if (!pLocal || !pLocal->IsAlive())
			break;

		if (pLocal->IsTaunting() || pLocal->InCond(TF_COND_HALLOWEEN_KART))
			break;

		const int iLocalPlayer = I::EngineClient->GetLocalPlayer();
		const int iAttacker = I::EngineClient->GetPlayerForUserID(pEvent->GetInt("attacker"));
		const int iVictim = I::EngineClient->GetPlayerForUserID(pEvent->GetInt("userid"));
		if (iAttacker != iLocalPlayer || iAttacker == iVictim)
			break;

		const int iChance = std::clamp(Vars::Misc::Automation::AutoTauntChance.Value, 0, 100);
		if (!iChance)
			break;

		if (SDK::RandomInt(1, 100) > iChance)
			break;

		I::EngineClient->ClientCmd_Unrestricted("taunt");
		break;
	}
	case FNV1A::Hash32Const("vote_maps_changed"):
		if (Vars::Misc::Automation::AutoVoteMap.Value)
		{
			I::EngineClient->ClientCmd_Unrestricted(std::format("next_map_vote {}", Vars::Misc::Automation::AutoVoteMapOption.Value).c_str());
		}
		break;
	}
}

int CMisc::AntiBackstab(CTFPlayer* pLocal, CUserCmd* pCmd, bool bSendPacket)
{
	if (!Vars::Misc::Automation::AntiBackstab.Value || !bSendPacket || G::Attacking == 1 || !pLocal || pLocal->m_MoveType() != MOVETYPE_WALK || pLocal->InCond(TF_COND_HALLOWEEN_KART))
		return 0;

	std::vector<std::pair<Vec3, CBaseEntity*>> vTargets = {};
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		if (!pPlayer->IsAlive() || pPlayer->IsAGhost() || pPlayer->InCond(TF_COND_STEALTHED))
			continue;

		auto pWeapon = pPlayer->m_hActiveWeapon()->As<CTFWeaponBase>();
		if (!pWeapon
			|| pWeapon->GetWeaponID() != TF_WEAPON_KNIFE
			&& !(G::PrimaryWeaponType == EWeaponType::MELEE && SDK::AttribHookValue(0, "crit_from_behind", pWeapon) > 0)
			&& !(pWeapon->GetWeaponID() == TF_WEAPON_FLAMETHROWER && SDK::AttribHookValue(0, "set_flamethrower_back_crit", pWeapon) == 1)
			|| F::PlayerUtils.IsIgnored(pPlayer->entindex()))
			continue;

		Vec3 vLocalPos = pLocal->GetCenter();
		Vec3 vTargetPos1 = pPlayer->GetCenter();
		Vec3 vTargetPos2 = vTargetPos1 + pPlayer->m_vecVelocity() * F::Backtrack.GetReal();
		float flDistance = std::max(std::max(SDK::MaxSpeed(pPlayer), SDK::MaxSpeed(pLocal)), pPlayer->m_vecVelocity().Length());
		if ((vLocalPos.DistTo(vTargetPos1) > flDistance || !SDK::VisPosWorld(pLocal, pPlayer, vLocalPos, vTargetPos1))
			&& (vLocalPos.DistTo(vTargetPos2) > flDistance || !SDK::VisPosWorld(pLocal, pPlayer, vLocalPos, vTargetPos2)))
			continue;

		vTargets.emplace_back(vTargetPos2, pEntity);
	}
	if (vTargets.empty())
		return 0;

	std::sort(vTargets.begin(), vTargets.end(), [&](const auto& a, const auto& b) -> bool
		{
			return pLocal->GetCenter().DistTo(a.first) < pLocal->GetCenter().DistTo(b.first);
		});

	auto& pTargetPos = vTargets.front();
	switch (Vars::Misc::Automation::AntiBackstab.Value)
	{
	case Vars::Misc::Automation::AntiBackstabEnum::Yaw:
	{
		Vec3 vAngleTo = Math::CalcAngle(pLocal->m_vecOrigin(), pTargetPos.first);
		vAngleTo.x = pCmd->viewangles.x;
		SDK::FixMovement(pCmd, vAngleTo);
		pCmd->viewangles = vAngleTo;
		
		return 1;
	}
	case Vars::Misc::Automation::AntiBackstabEnum::Pitch:
	case Vars::Misc::Automation::AntiBackstabEnum::Fake:
	{
		bool bCheater = F::PlayerUtils.HasTag(pTargetPos.second->entindex(), F::PlayerUtils.TagToIndex(CHEATER_TAG));
		// if the closest spy is a cheater, assume auto stab is being used, otherwise don't do anything if target is in front
		if (!bCheater)
		{
			auto TargetIsBehind = [&]()
				{
					const float flCompDist = 0.0625f;
					const float flSqCompDist = 0.0884f;

					Vec3 vToTarget = (pLocal->m_vecOrigin() - pTargetPos.first).To2D();
					const float flDist = vToTarget.Normalize();
					if (flDist < flSqCompDist)
						return true;

					const float flExtra = 2.f * flCompDist / flDist; // account for origin compression
					float flPosVsTargetViewMinDot = 0.f - 0.0031f - flExtra;

					Vec3 vTargetForward; Math::AngleVectors(pCmd->viewangles, &vTargetForward);
					vTargetForward.Normalize2D();

					const float flPosVsTargetViewDot = vToTarget.Dot(vTargetForward); // Behind?

					return flPosVsTargetViewDot > flPosVsTargetViewMinDot;
				};

			if (!TargetIsBehind())
				return 0;
		}

		if (!bCheater || Vars::Misc::Automation::AntiBackstab.Value == Vars::Misc::Automation::AntiBackstabEnum::Pitch)
		{
			pCmd->forwardmove *= -1;
			pCmd->viewangles.x = 269.f;
		}
		else
			pCmd->viewangles.x = 271.f;
		// may slip up some auto backstabs depending on mode, though we are still able to be stabbed

		return 2;
	}
	}

	return 0;
}

void CMisc::PingReducer()
{
	static Timer tTimer = {};
	if (!tTimer.Run(0.1f))
		return;

	auto pNetChan = reinterpret_cast<CNetChannel*>(I::EngineClient->GetNetChannelInfo());
	auto pResource = H::Entities.GetResource();
	if (!pNetChan || !pResource)
		return;

	static auto cl_cmdrate = U::ConVars.FindVar("cl_cmdrate");
	const int iCmdRate = cl_cmdrate->GetInt();
	const int Ping = pResource->m_iPing(I::EngineClient->GetLocalPlayer());
	const int iTarget = Vars::Misc::Exploits::PingReducer.Value && (Ping > Vars::Misc::Exploits::PingTarget.Value) ? -1 : iCmdRate;

	NET_SetConVar cmd("cl_cmdrate", std::to_string(m_iWishCmdrate = iTarget).c_str());
	pNetChan->SendNetMsg(cmd);
}

void CMisc::UnlockAchievements()
{
	const auto pAchievementMgr = I::EngineClient->GetAchievementMgr();
	if (pAchievementMgr)
	{
		I::SteamUserStats->RequestCurrentStats();
		for (int i = 0; i < pAchievementMgr->GetAchievementCount(); i++)
			pAchievementMgr->AwardAchievement(pAchievementMgr->GetAchievementByIndex(i)->GetAchievementID());
		I::SteamUserStats->StoreStats();
		I::SteamUserStats->RequestCurrentStats();
	}
}

void CMisc::LockAchievements()
{
	const auto pAchievementMgr = I::EngineClient->GetAchievementMgr();
	if (pAchievementMgr)
	{
		I::SteamUserStats->RequestCurrentStats();
		for (int i = 0; i < pAchievementMgr->GetAchievementCount(); i++)
			I::SteamUserStats->ClearAchievement(pAchievementMgr->GetAchievementByIndex(i)->GetName());
		I::SteamUserStats->StoreStats();
		I::SteamUserStats->RequestCurrentStats();
	}
}

void CMisc::AchievementSpam(CTFPlayer* pLocal)
{
	if (!Vars::Misc::Automation::AchievementSpam.Value || !pLocal || !pLocal->IsAlive())
	{
		m_eAchievementSpamState = AchievementSpamState::IDLE;
		return;
	}

	const auto pAchievementMgr = reinterpret_cast<IAchievementMgr* (*)()>(U::Memory.GetVirtual(I::EngineClient, 114))();
	if (!pAchievementMgr)
	{
		m_eAchievementSpamState = AchievementSpamState::IDLE;
		return;
	}

	switch (m_eAchievementSpamState)
	{
	case AchievementSpamState::IDLE:
	{
		if (!m_tAchievementSpamTimer.Run(20.0f))
			return;

		// Do Androids Dream? achievement by default
		// TODO: add a new column to edit achievement timer & number directly in cheat (like you did with autoitem)
		int specificAchievementID = 2332;

		IAchievement* pAchievement = nullptr;
		for (int i = 0; i < pAchievementMgr->GetAchievementCount(); i++)
		{
			IAchievement* pCurrentAchievement = pAchievementMgr->GetAchievementByIndex(i);
			if (pCurrentAchievement && pCurrentAchievement->GetAchievementID() == specificAchievementID)
			{
				pAchievement = pCurrentAchievement;
				break;
			}
		}

		if (!pAchievement || !pAchievement->GetName())
			return;

		m_iAchievementSpamID = specificAchievementID;
		m_sAchievementSpamName = pAchievement->GetName();
		m_eAchievementSpamState = AchievementSpamState::CLEARING;
		break;
	}
	case AchievementSpamState::CLEARING:
	{
		I::SteamUserStats->RequestCurrentStats();
		I::SteamUserStats->ClearAchievement(m_sAchievementSpamName.c_str());
		I::SteamUserStats->StoreStats();

		m_tAchievementDelayTimer.Update();
		m_eAchievementSpamState = AchievementSpamState::WAITING;
		break;
	}
	case AchievementSpamState::WAITING:
	{
		if (!m_tAchievementDelayTimer.Run(0.1f))
			return;

		m_eAchievementSpamState = AchievementSpamState::AWARDING;
		break;
	}
	case AchievementSpamState::AWARDING:
	{
		I::SteamUserStats->RequestCurrentStats();
		pAchievementMgr->AwardAchievement(m_iAchievementSpamID);
		I::SteamUserStats->StoreStats();

		m_eAchievementSpamState = AchievementSpamState::IDLE;
		break;
	}
	}
}

void CMisc::VoiceCommandSpam(CTFPlayer* pLocal)
{
	if (!Vars::Misc::Automation::VoiceCommandSpam.Value || !pLocal->IsAlive())
		return;

	static float flLastVoiceTime = 0.0f;
	float flCurrentTime = SDK::PlatFloatTime();
	if (flCurrentTime - flLastVoiceTime >= 6.5f) // 6500ms in seconds
	{
		flLastVoiceTime = flCurrentTime;

		switch (Vars::Misc::Automation::VoiceCommandSpam.Value)
		{
		case Vars::Misc::Automation::VoiceCommandSpamEnum::Random:
			{
				int iMenu = SDK::RandomInt(0, 2);
				int iCommand = SDK::RandomInt(0, 8);
				std::string sCmd = "voicemenu " + std::to_string(iMenu) + " " + std::to_string(iCommand);
				I::EngineClient->ClientCmd_Unrestricted(sCmd.c_str());
			}
			break;
		case Vars::Misc::Automation::VoiceCommandSpamEnum::Medic:
			I::EngineClient->ClientCmd_Unrestricted("voicemenu 0 0");
			break;
		case Vars::Misc::Automation::VoiceCommandSpamEnum::Thanks:
			I::EngineClient->ClientCmd_Unrestricted("voicemenu 0 1");
			break;
		case Vars::Misc::Automation::VoiceCommandSpamEnum::NiceShot:
			I::EngineClient->ClientCmd_Unrestricted("voicemenu 2 6");
			break;
		case Vars::Misc::Automation::VoiceCommandSpamEnum::Cheers:
			I::EngineClient->ClientCmd_Unrestricted("voicemenu 2 2");
			break;
		case Vars::Misc::Automation::VoiceCommandSpamEnum::Jeers:
			I::EngineClient->ClientCmd_Unrestricted("voicemenu 2 3");
			break;
		case Vars::Misc::Automation::VoiceCommandSpamEnum::GoGoGo:
			I::EngineClient->ClientCmd_Unrestricted("voicemenu 0 2");
			break;
		case Vars::Misc::Automation::VoiceCommandSpamEnum::MoveUp:
			I::EngineClient->ClientCmd_Unrestricted("voicemenu 0 3");
			break;
		case Vars::Misc::Automation::VoiceCommandSpamEnum::GoLeft:
			I::EngineClient->ClientCmd_Unrestricted("voicemenu 0 4");
			break;
		case Vars::Misc::Automation::VoiceCommandSpamEnum::GoRight:
			I::EngineClient->ClientCmd_Unrestricted("voicemenu 0 5");
			break;
		case Vars::Misc::Automation::VoiceCommandSpamEnum::Yes:
			I::EngineClient->ClientCmd_Unrestricted("voicemenu 0 6");
			break;
		case Vars::Misc::Automation::VoiceCommandSpamEnum::No:
			I::EngineClient->ClientCmd_Unrestricted("voicemenu 0 7");
			break;
		case Vars::Misc::Automation::VoiceCommandSpamEnum::Incoming:
			I::EngineClient->ClientCmd_Unrestricted("voicemenu 1 0");
			break;
		case Vars::Misc::Automation::VoiceCommandSpamEnum::Spy:
			I::EngineClient->ClientCmd_Unrestricted("voicemenu 1 1");
			break;
		case Vars::Misc::Automation::VoiceCommandSpamEnum::Sentry:
			I::EngineClient->ClientCmd_Unrestricted("voicemenu 1 2");
			break;
		case Vars::Misc::Automation::VoiceCommandSpamEnum::NeedTeleporter:
			I::EngineClient->ClientCmd_Unrestricted("voicemenu 1 3");
			break;
		case Vars::Misc::Automation::VoiceCommandSpamEnum::Pootis:
			I::EngineClient->ClientCmd_Unrestricted("voicemenu 1 4");
			break;
		case Vars::Misc::Automation::VoiceCommandSpamEnum::NeedSentry:
			I::EngineClient->ClientCmd_Unrestricted("voicemenu 1 5");
			break;
		case Vars::Misc::Automation::VoiceCommandSpamEnum::ActivateCharge:
			I::EngineClient->ClientCmd_Unrestricted("voicemenu 1 6");
			break;
		case Vars::Misc::Automation::VoiceCommandSpamEnum::Help:
			I::EngineClient->ClientCmd_Unrestricted("voicemenu 2 0");
			break;
		case Vars::Misc::Automation::VoiceCommandSpamEnum::BattleCry:
			I::EngineClient->ClientCmd_Unrestricted("voicemenu 2 1");
			break;
		}
	}
}

void CMisc::AutoReport()
{
	if (!Vars::Misc::Automation::AutoReport.Value)
		return;

	static Timer tReportTimer{};
	if (!tReportTimer.Run(5.0f))
		return;

	int iLocalIdx = I::EngineClient->GetLocalPlayer();
	for (int i = 1; i <= I::EngineClient->GetMaxClients(); i++)
	{
		if (i == iLocalIdx)
			continue;

		if (auto uSteamId = F::PlayerUtils.GetAccountID(i))
		{
			if (H::Entities.IsFriend(i) ||
				H::Entities.InParty(i) ||
				F::PlayerUtils.IsIgnored(i) ||
				F::PlayerUtils.HasTag(i, F::PlayerUtils.TagToIndex(FRIEND_IGNORE_TAG)) ||
				F::PlayerUtils.HasTag(i, F::PlayerUtils.TagToIndex(BOT_IGNORE_TAG)))
				continue;

			uint64_t uSteamID64 = ((uint64_t)1 << 56) | ((uint64_t)1 << 52) | ((uint64_t)1 << 32) | uSteamId;
			S::ReportPlayerAccount.Call<bool>(uSteamID64, 1);
		}
	}
}

void CMisc::RandomVotekick(CTFPlayer* pLocal)
{
	if (!Vars::Misc::Automation::AutoVotekick.Value || !pLocal->IsInValidTeam())
		return;

	if (!m_tAutoVotekickTimer.Run(1.0f))
		return;
	
	auto pResource = H::Entities.GetResource();
	if (!pResource)
		return;

	std::vector<int> vPotentialTargets;

	for (int i = 1; i <= I::EngineClient->GetMaxClients(); i++)
	{
		if (i == pLocal->entindex())
			continue;

		if (!pResource->m_bValid(i) || pResource->IsFakePlayer(i))
			continue;

		if (Vars::Misc::Automation::AutoVotekick.Value == Vars::Misc::Automation::AutoVotekickEnum::Prio && !F::PlayerUtils.IsPrioritized(i))
			continue;

		if (H::Entities.IsFriend(i) || 
			H::Entities.InParty(i) ||
			F::PlayerUtils.IsIgnored(i) ||
			F::PlayerUtils.HasTag(i, F::PlayerUtils.TagToIndex(FRIEND_IGNORE_TAG)) ||
			F::PlayerUtils.HasTag(i, F::PlayerUtils.TagToIndex(BOT_IGNORE_TAG)))
			continue;

		vPotentialTargets.push_back(i);
	}

	if (vPotentialTargets.empty())
		return;

	int iRandom = SDK::RandomInt(0, vPotentialTargets.size() - 1);
	int iTarget = vPotentialTargets[iRandom];

	I::ClientState->SendStringCmd(std::format("callvote Kick \"{} other\"", pResource->m_iUserID(iTarget)).c_str());
}

void CMisc::ChatSpam(CTFPlayer* pLocal)
{
	auto ResetChatTimer = [&]()
	{
		m_tChatSpamTimer.Update();
	};

	if (!Vars::Misc::Automation::ChatSpam::Enable.Value)
	{
		ResetChatTimer();
		return;
	}

	if (!pLocal->IsAlive() || !pLocal->IsInValidTeam() || pLocal->m_iClass() == TF_CLASS_UNDEFINED)
	{
		ResetChatTimer();
		return;
	}

	auto EnsureChatLinesLoaded = [&]() -> bool
	{
		if (!m_vChatSpamLines.empty())
			return true;

		auto LoadLinesFrom = [&](const std::string& sPath) -> bool
		{
			std::ifstream file(sPath);
			if (!file.is_open())
				return false;

			std::vector<std::string> vLines = {};
			std::string sLine;
			while (std::getline(file, sLine))
			{
				if (!sLine.empty())
					vLines.push_back(sLine);
			}

			if (vLines.empty())
				return false;

			m_vChatSpamLines = std::move(vLines);
			m_iCurrentChatSpamIndex = 0;
			SDK::Output("ChatSpam", std::format("Loaded {} lines from {}", m_vChatSpamLines.size(), sPath).c_str(), {}, OUTPUT_CONSOLE | OUTPUT_DEBUG);
			return true;
		};

		char szGamePath[MAX_PATH] = {};
		GetModuleFileNameA(GetModuleHandleA("tf_win64.exe"), szGamePath, MAX_PATH);
		std::string sGameDir = szGamePath;
		if (const size_t uLastSlash = sGameDir.find_last_of("\\/"); uLastSlash != std::string::npos)
			sGameDir.resize(uLastSlash);

		SDK::Output("ChatSpam", "Loading chat lines from file", {}, OUTPUT_CONSOLE | OUTPUT_DEBUG);

		const std::vector<std::string> vCandidatePaths = {
			"cat_chatspam.txt",
			sGameDir + "\\tf\\cat_chatspam.txt"
		};

		for (const auto& sPath : vCandidatePaths)
		{
			if (LoadLinesFrom(sPath))
				return true;
		}

		const std::string sDefaultPath = sGameDir + "\\Amalgam\\cat_chatspam.txt";
		std::ofstream newFile(sDefaultPath);
		if (newFile.is_open())
		{
			newFile << "This is a default message from cat_chatspam.txt\n";
			newFile << "Edit this file Amalgam/cat_chatspam.txt\n";
			newFile << "Each line will be sent as a separate message\n";
			newFile << "[Amalgam] Chat Spam is working!\n";
			newFile << "Put your chat spam lines in this file\n";
			newFile.close();

			if (LoadLinesFrom(sDefaultPath))
				return true;
		}

		SDK::Output("ChatSpam", "Failed to load or create chat spam file, using built-in messages", {}, OUTPUT_CONSOLE | OUTPUT_DEBUG);
		m_vChatSpamLines = {
			"Put your chat spam lines in Amalgam/cat_chatspam.txt",
			"ChatSpam is running but couldn't find Amalgam/cat_chatspam.txt",
			"[Amalgam] Chat Spam is working!"
		};
		m_iCurrentChatSpamIndex = 0;

		return true;
	};

	if (!EnsureChatLinesLoaded() || m_vChatSpamLines.empty())
		return;

	float flSpamInterval = Vars::Misc::Automation::ChatSpam::Interval.Value;
	if (flSpamInterval < 0.2f)
		flSpamInterval = 0.2f;

	if (!m_tChatSpamTimer.Run(flSpamInterval))
		return;

	auto FetchNextChatLine = [&]() -> std::string
	{
		const size_t uLineCount = m_vChatSpamLines.size();
		if (!uLineCount)
			return {};

		if (Vars::Misc::Automation::ChatSpam::Randomize.Value)
		{
			int iMax = static_cast<int>(uLineCount) - 1;
			if (iMax < 0)
				iMax = 0;
			const int iRandomIndex = SDK::RandomInt(0, iMax);
			if (iRandomIndex >= 0 && iRandomIndex < static_cast<int>(uLineCount))
				return m_vChatSpamLines[iRandomIndex];
			return "[ChatSpam]";
		}

		if (m_iCurrentChatSpamIndex < 0 || m_iCurrentChatSpamIndex >= static_cast<int>(uLineCount))
			m_iCurrentChatSpamIndex = 0;

		const std::string& sLine = m_vChatSpamLines[m_iCurrentChatSpamIndex];
		m_iCurrentChatSpamIndex = (m_iCurrentChatSpamIndex + 1) % static_cast<int>(uLineCount);
		return sLine;
	};

	std::string sChatLine = FetchNextChatLine();
	if (sChatLine.empty())
		return;

	if (sChatLine.length() > 150)
		sChatLine.resize(150);

	std::string sChatCommand;
	if (Vars::Misc::Automation::ChatSpam::TeamChat.Value)
		sChatCommand = "say_team \"" + sChatLine + "\"";
	else
		sChatCommand = "say \"" + sChatLine + "\"";

	SDK::Output("ChatSpam", std::format("Sending: {}", sChatCommand).c_str(), {}, OUTPUT_CONSOLE | OUTPUT_DEBUG);
	I::EngineClient->ClientCmd_Unrestricted(sChatCommand.c_str());
}

void CMisc::AutoMvmReadyUp()
{
	if (!Vars::Misc::MannVsMachine::AutoMvmReadyUp.Value)
		return;

	auto pLocal = H::Entities.GetLocal();
	if (!pLocal)
		return;

	auto pGameRules = I::TFGameRules();
	if (!pGameRules)
		return;

	if (!pGameRules->m_bPlayingMannVsMachine() || 
		!pGameRules->m_bInWaitingForPlayers() || 
		pGameRules->m_iRoundState() != GR_STATE_BETWEEN_RNDS)
		return;

	const int iLocalIndex = pLocal->entindex();
	if (iLocalIndex < 0 || iLocalIndex >= 100)
		return;

	if (!pGameRules->IsPlayerReady(iLocalIndex))
		I::EngineClient->ClientCmd_Unrestricted("tournament_player_readystate 1");
}

void CMisc::ExecBuyBot(CTFPlayer* pLocal)
{
	if (!Vars::Misc::MannVsMachine::BuyBot.Value)
		return;

	auto pGameRules = I::TFGameRules();
	if (!pGameRules || !pGameRules->m_bPlayingMannVsMachine())
		return;

	if (!pLocal->m_bInUpgradeZone())
		return;

	// cash threshold
	if (Vars::Misc::MannVsMachine::MaxCash.Value > 0 && pLocal->m_nCurrency() >= Vars::Misc::MannVsMachine::MaxCash.Value)
		return;

	static auto tfMvmRespec = U::ConVars.FindVar("tf_mvm_respec_enabled");
	if (tfMvmRespec->GetInt() != 1)
		return;

	float flCurTime = I::GlobalVars->curtime;
	if (m_flBuybotClock > flCurTime)
		return;

	switch (m_iBuybotStep)
	{
		case 1:
			I::EngineClient->ServerCmdKeyValues(new KeyValues("MvM_UpgradesBegin"));
			{
				KeyValues* kv = new KeyValues("MVM_Upgrade");
				KeyValues* sub = kv->FindKey("Upgrade", true);
				sub->SetInt("itemslot", 1);
				sub->SetInt("Upgrade", 19);
				sub->SetInt("count", 1);
				I::EngineClient->ServerCmdKeyValues(kv);
			}
			{
				KeyValues* kv = new KeyValues("MVM_Upgrade");
				KeyValues* sub = kv->FindKey("Upgrade", true);
				sub->SetInt("itemslot", 1);
				sub->SetInt("Upgrade", 19);
				sub->SetInt("count", 1);
				I::EngineClient->ServerCmdKeyValues(kv);
			}
			{
				KeyValues* kv = new KeyValues("MvM_UpgradesDone");
				kv->SetInt("num_upgrades", 2);
				I::EngineClient->ServerCmdKeyValues(kv);
			}
			break;
		case 2:
			I::EngineClient->ServerCmdKeyValues(new KeyValues("MvM_UpgradesBegin"));
			{
				KeyValues* kv = new KeyValues("MVM_Upgrade");
				KeyValues* sub = kv->FindKey("Upgrade", true);
				sub->SetInt("itemslot", 1);
				sub->SetInt("Upgrade", 19);
				sub->SetInt("count", -1);
				I::EngineClient->ServerCmdKeyValues(kv);
			}
			{
				KeyValues* kv = new KeyValues("MVM_Upgrade");
				KeyValues* sub = kv->FindKey("Upgrade", true);
				sub->SetInt("itemslot", 1);
				sub->SetInt("Upgrade", 19);
				sub->SetInt("count", 1);
				I::EngineClient->ServerCmdKeyValues(kv);
			}
			I::EngineClient->ServerCmdKeyValues(new KeyValues("MVM_Respec"));
			{
				KeyValues* kv = new KeyValues("MvM_UpgradesDone");
				kv->SetInt("num_upgrades", -1);
				I::EngineClient->ServerCmdKeyValues(kv);
			}
			break;
		case 3:
			I::EngineClient->ServerCmdKeyValues(new KeyValues("MvM_UpgradesBegin"));
			{
				KeyValues* kv = new KeyValues("MVM_Upgrade");
				KeyValues* sub = kv->FindKey("Upgrade", true);
				sub->SetInt("itemslot", 1);
				sub->SetInt("Upgrade", 19);
				sub->SetInt("count", 1);
				I::EngineClient->ServerCmdKeyValues(kv);
			}
			{
				KeyValues* kv = new KeyValues("MVM_Upgrade");
				KeyValues* sub = kv->FindKey("Upgrade", true);
				sub->SetInt("itemslot", 1);
				sub->SetInt("Upgrade", 19);
				sub->SetInt("count", 1);
				I::EngineClient->ServerCmdKeyValues(kv);
			}
			{
				KeyValues* kv = new KeyValues("MVM_Upgrade");
				KeyValues* sub = kv->FindKey("Upgrade", true);
				sub->SetInt("itemslot", 1);
				sub->SetInt("Upgrade", 19);
				sub->SetInt("count", -1);
				I::EngineClient->ServerCmdKeyValues(kv);
			}
			{
				KeyValues* kv = new KeyValues("MVM_Upgrade");
				KeyValues* sub = kv->FindKey("Upgrade", true);
				sub->SetInt("itemslot", 1);
				sub->SetInt("Upgrade", 19);
				sub->SetInt("count", -1);
				I::EngineClient->ServerCmdKeyValues(kv);
			}
			{
				KeyValues* kv = new KeyValues("MvM_UpgradesDone");
				kv->SetInt("num_upgrades", 0);
				I::EngineClient->ServerCmdKeyValues(kv);
			}
			break;
	}
	m_iBuybotStep = m_iBuybotStep % 3 + 1;
	m_flBuybotClock = flCurTime + 0.2f;
}

void CMisc::ResetBuyBot()
{
	m_iBuybotStep = 1;
	m_flBuybotClock = 0.0f;
}

void CMisc::MicSpam()
{
	static bool bShouldRestore = false;
	static Timer tRecordTimer = {};

	if (Vars::Misc::Automation::Micspam.Value)
	{
		if (I::EngineClient->IsInGame() && tRecordTimer.Run(10.0f))
		{
			I::EngineClient->ClientCmd_Unrestricted("+voicerecord");
			I::EngineClient->ClientCmd_Unrestricted("voice_avggain 1");
#ifdef TEXTMODE
			I::EngineClient->ClientCmd_Unrestricted("volume 0");
			I::EngineClient->ClientCmd_Unrestricted("voice_enable 1");
			I::EngineClient->ClientCmd_Unrestricted("voice_loopback 0");
#endif
		}

		bShouldRestore = true;
	}
	else if (bShouldRestore)
	{
		if (S::Voice_IsRecording.Call<bool>())
		{
			I::EngineClient->ClientCmd_Unrestricted("-voicerecord");
#ifdef TEXTMODE
			I::EngineClient->ClientCmd_Unrestricted("volume 0");
			I::EngineClient->ClientCmd_Unrestricted("voice_enable 0");
			I::EngineClient->ClientCmd_Unrestricted("voice_loopback 0");
#endif
		}

		bShouldRestore = false;
	}
}