#include "Misc.h"

#include "../Backtrack/Backtrack.h"
#include "../Ticks/Ticks.h"
#include "../Players/PlayerUtils.h"
#include "../Aimbot/AutoRocketJump/AutoRocketJump.h"
#ifdef TEXTMODE
#include "NamedPipe/NamedPipe.h"
#endif
#include <fstream>

void CMisc::RunPre(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	NoiseSpam(pLocal);
	VoiceCommandSpam(pLocal);
	ChatSpam(pLocal);
	CheatsBypass();
	WeaponSway();

	#ifdef TEXTMODE
	if (I::EngineClient && I::EngineClient->IsInGame() && I::EngineClient->IsConnected())
	{
		static Timer namedPipeTimer{};
		if (namedPipeTimer.Run(1.0f))
		{
			F::NamedPipe::UpdateLocalBotIgnoreStatus();
		}
	}
	#endif
	
	if (!pLocal)
		return;

	AntiAFK(pLocal, pCmd);
	InstantRespawnMVM(pLocal);
	RandomVotekick(pLocal);

	if (Vars::Misc::MannVsMachine::BuyBot.Value)
		ExecBuyBot(pLocal);

	if (!pLocal->IsAlive() || pLocal->IsAGhost() || pLocal->m_MoveType() != MOVETYPE_WALK || pLocal->IsSwimming() || pLocal->InCond(TF_COND_SHIELD_CHARGE) || pLocal->InCond(TF_COND_HALLOWEEN_KART))
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
	if (!pLocal || !pLocal->IsAlive() || pLocal->IsAGhost() || pLocal->m_MoveType() != MOVETYPE_WALK || pLocal->IsSwimming() || pLocal->InCond(TF_COND_SHIELD_CHARGE))
		return;

	EdgeJump(pLocal, pCmd, true);
	TauntKartControl(pLocal, pCmd);
	FastMovement(pLocal, pCmd);
	AutoPeek(pLocal, pCmd, true);
	BreakShootSound(pLocal, pCmd);
	MovementLock(pLocal, pCmd);
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

	if (pCmd->buttons & (IN_MOVELEFT | IN_MOVERIGHT | IN_FORWARD | IN_BACK) || !pLocal->IsAlive())
		tTimer.Update();
	else if (Vars::Misc::Automation::AntiAFK.Value && iIdleMethod && tTimer.Check(flMaxIdleTime * 60.f - 10.f)) // trigger 10 seconds before kick
	{
		pCmd->buttons |= I::GlobalVars->tickcount % 2 ? IN_FORWARD : IN_BACK;
		m_bAntiAFK = true;
	}
}

void CMisc::InstantRespawnMVM(CTFPlayer* pLocal)
{
	if (Vars::Misc::MannVsMachine::InstantRespawn.Value && I::EngineClient->IsInGame() && !pLocal->IsAlive())
	{
		KeyValues* kv = new KeyValues("MVM_Revive_Response");
		kv->SetInt("accepted", 1);
		I::EngineClient->ServerCmdKeyValues(kv);
	}
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
	// Handle Taunt Slide
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
			pCmd->sidemove = pCmd->viewangles.x != 90.f ? -50.f : -450.f;
		else if (pCmd->buttons & IN_MOVERIGHT)
			pCmd->sidemove = pCmd->viewangles.x != 90.f ? 50.f : 450.f;

		Vec3 vAngle = I::EngineClient->GetViewAngles();
		pCmd->viewangles.y = vAngle.y;

		G::SilentAngles = true;
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
		if ((pLocal->IsDucking() ? !Vars::Misc::Movement::CrouchSpeed.Value : !Vars::Misc::Movement::FastAccelerate.Value)
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
	}
}

int CMisc::AntiBackstab(CTFPlayer* pLocal, CUserCmd* pCmd, bool bSendPacket)
{
	if (!Vars::Misc::Automation::AntiBackstab.Value || !bSendPacket || G::Attacking == 1 || !pLocal || pLocal->m_MoveType() != MOVETYPE_WALK || pLocal->InCond(TF_COND_HALLOWEEN_KART))
		return 0;

	std::vector<std::pair<Vec3, CBaseEntity*>> vTargets = {};
	for (auto pEntity : H::Entities.GetGroup(EGroupType::PLAYERS_ENEMIES))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		if (pPlayer->IsDormant() || !pPlayer->IsAlive() || pPlayer->IsAGhost() || pPlayer->InCond(TF_COND_STEALTHED))
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
					Vec3 vToTarget = (pLocal->m_vecOrigin() - pTargetPos.first).To2D();
					const float flDist = vToTarget.Normalize();
					if (!flDist)
						return true;

					float flTolerance = 0.0625f;
					float flExtra = 2.f * flTolerance / flDist; // account for origin compression
					float flPosVsTargetViewMinDot = 0.f - 0.0031f - flExtra;

					Vec3 vTargetForward; Math::AngleVectors(pCmd->viewangles, &vTargetForward);
					vTargetForward.Normalize2D();

					return vToTarget.Dot(vTargetForward) > flPosVsTargetViewMinDot;
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
	const auto& pResource = H::Entities.GetPR( );
	if (!pNetChan || !pResource)
		return;

	static auto cl_cmdrate = U::ConVars.FindVar("cl_cmdrate");
	const int iCmdRate = cl_cmdrate->GetInt();
	const int Ping = pResource->m_iPing( I::EngineClient->GetLocalPlayer( ) );
	const int iTarget = Vars::Misc::Exploits::PingReducer.Value && ( Ping > Vars::Misc::Exploits::PingTarget.Value ) ? -1 : iCmdRate;

	NET_SetConVar cmd("cl_cmdrate", std::to_string(m_iWishCmdrate = iTarget).c_str());
	pNetChan->SendNetMsg(cmd);
}

void CMisc::UnlockAchievements()
{
	const auto pAchievementMgr = U::Memory.CallVirtual<114, IAchievementMgr*>(I::EngineClient);
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
	const auto pAchievementMgr = U::Memory.CallVirtual<114, IAchievementMgr*>(I::EngineClient);
	if (pAchievementMgr)
	{
		I::SteamUserStats->RequestCurrentStats();
		for (int i = 0; i < pAchievementMgr->GetAchievementCount(); i++)
			I::SteamUserStats->ClearAchievement(pAchievementMgr->GetAchievementByIndex(i)->GetName());
		I::SteamUserStats->StoreStats();
		I::SteamUserStats->RequestCurrentStats();
	}
}

bool CMisc::SteamRPC()
{
	/*
	if (!Vars::Misc::Steam::EnableRPC.Value)
	{
		if (!bSteamCleared) // stupid way to return back to normal rpc
		{
			I::SteamFriends->SetRichPresence("steam_display", ""); // this will only make it say "Team Fortress 2" until the player leaves/joins some server. its bad but its better than making 1000 checks to recreate the original
			bSteamCleared = true;
		}
		return false;
	}

	bSteamCleared = false;
	*/


	if (!Vars::Misc::SteamRPC::Enabled.Value)
		return false;

	I::SteamFriends->SetRichPresence("steam_display", "#TF_RichPresence_Display");
	if (!I::EngineClient->IsInGame() && !Vars::Misc::SteamRPC::OverrideInMenu.Value)
		I::SteamFriends->SetRichPresence("state", "MainMenu");
	else
	{
		I::SteamFriends->SetRichPresence("state", "PlayingMatchGroup");

		switch (Vars::Misc::SteamRPC::MatchGroup.Value)
		{
		case Vars::Misc::SteamRPC::MatchGroupEnum::SpecialEvent: I::SteamFriends->SetRichPresence("matchgrouploc", "SpecialEvent"); break;
		case Vars::Misc::SteamRPC::MatchGroupEnum::MvMMannUp: I::SteamFriends->SetRichPresence("matchgrouploc", "MannUp"); break;
		case Vars::Misc::SteamRPC::MatchGroupEnum::Competitive: I::SteamFriends->SetRichPresence("matchgrouploc", "Competitive6v6"); break;
		case Vars::Misc::SteamRPC::MatchGroupEnum::Casual: I::SteamFriends->SetRichPresence("matchgrouploc", "Casual"); break;
		case Vars::Misc::SteamRPC::MatchGroupEnum::MvMBootCamp: I::SteamFriends->SetRichPresence("matchgrouploc", "BootCamp"); break;
		}
	}
	I::SteamFriends->SetRichPresence("currentmap", Vars::Misc::SteamRPC::MapText.Value.c_str());
	I::SteamFriends->SetRichPresence("steam_player_group_size", std::to_string(Vars::Misc::SteamRPC::GroupSize.Value).c_str());

	return true;
}


void CMisc::NoiseSpam(CTFPlayer* pLocal)
{
	if (!Vars::Misc::Automation::NoiseSpam.Value || !pLocal)
		return;
	
	if (pLocal->m_bUsingActionSlot())
		return;
	
	static float flLastSpamTime = 0.0f;
	float flCurrentTime = SDK::PlatFloatTime();
	if (flCurrentTime - flLastSpamTime < 0.2f) 
		return;
	
	flLastSpamTime = flCurrentTime;
	I::EngineClient->ServerCmdKeyValues(new KeyValues("use_action_slot_item_server"));
}

void CMisc::VoiceCommandSpam(CTFPlayer* pLocal)
{
	if (!Vars::Misc::Automation::VoiceCommandSpam.Value || !pLocal || !pLocal->IsAlive())
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
				int menu = SDK::RandomInt(0, 2);
				int command = SDK::RandomInt(0, 8);
				std::string cmd = "voicemenu " + std::to_string(menu) + " " + std::to_string(command);
				I::EngineClient->ClientCmd_Unrestricted(cmd.c_str());
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

void CMisc::RandomVotekick(CTFPlayer* pLocal)
{
	if (!Vars::Misc::Automation::RandomVotekick.Value || !I::EngineClient->IsInGame() || !I::EngineClient->IsConnected())
		return;

	if (!m_tRandomVotekickTimer.Run(1.0f))
		return;

	std::vector<int> vPotentialTargets;

	for (int i = 1; i <= I::EngineClient->GetMaxClients(); i++)
	{
		if (i == I::EngineClient->GetLocalPlayer())
			continue;

		PlayerInfo_t pi{};
		if (!I::EngineClient->GetPlayerInfo(i, &pi) || pi.fakeplayer)
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


	PlayerInfo_t pi{};
	if (I::EngineClient->GetPlayerInfo(iTarget, &pi))
	{
		I::ClientState->SendStringCmd(std::format("callvote Kick \"{} other\"", pi.userID).c_str());
	}
}


// lmaobox if(crash){(dontcrash)} method down here
void CMisc::ChatSpam(CTFPlayer* pLocal)
{
	if (!Vars::Misc::Automation::ChatSpam::Enable.Value || !pLocal || !I::EngineClient)
	{
		m_tChatSpamTimer.Update();
		return;
	}
	
	try
	{
		if (!I::EngineClient->IsInGame() || !I::EngineClient->IsConnected())
		{
			m_tChatSpamTimer.Update();
			return;
		}
		
		if (!pLocal->IsAlive() || pLocal->m_iTeamNum() <= 1 || pLocal->m_iClass() == TF_CLASS_UNDEFINED)
		{
			m_tChatSpamTimer.Update();
			return;
		}
	}
	catch (...)
	{
		m_tChatSpamTimer.Update();
		return;
	}
		
	if (m_vChatSpamLines.empty())
	{
		try
		{
			char gamePath[MAX_PATH];
			GetModuleFileNameA(GetModuleHandleA("tf_win64.exe"), gamePath, MAX_PATH);
			std::string gameDir = gamePath;
			size_t lastSlash = gameDir.find_last_of("\\/");
			if (lastSlash != std::string::npos)
				gameDir = gameDir.substr(0, lastSlash);
				
			// Debug info
			SDK::Output("ChatSpam", "Loading chat lines from file", {}, true, true);
			
			std::vector<std::string> pathsToTry = {
				"cat_chatspam.txt",
				gameDir + "\\tf\\cat_chatspam.txt"
			};
			
			bool fileLoaded = false;
			std::string successfulPath;
			
			for (const auto& path : pathsToTry)
			{
				try
				{
					std::ifstream file(path);
					if (file.is_open())
					{
						std::string line;
						while (std::getline(file, line))
						{
							if (!line.empty())
								m_vChatSpamLines.push_back(line);
						}
						file.close();
						
						SDK::Output("ChatSpam", std::format("Loaded {} lines from {}", m_vChatSpamLines.size(), path).c_str(), {}, true, true);
						fileLoaded = true;
						successfulPath = path;
						break;
					}
				}
				catch (...)
				{
					continue;
				}
			}
			
			if (!fileLoaded)
			{
				try
				{
					std::string defaultPath = gameDir + "\\tf\\cat_chatspam.txt";
					std::ofstream newFile(defaultPath);
					
					if (newFile.is_open())
					{
						newFile << "This is a default message from cat_chatspam.txt\n";
						newFile << "Edit this file tf/cat_chatspam.txt\n";
						newFile << "Each line will be sent as a separate message\n";
						newFile << "[Amalgam] Chat Spam is working!\n";
						newFile << "Put your chat spam lines in this file\n";
						newFile.close();
						
						std::ifstream checkFile(defaultPath);
						if (checkFile.is_open())
						{
							std::string line;
							while (std::getline(checkFile, line))
							{
								if (!line.empty())
									m_vChatSpamLines.push_back(line);
							}
							checkFile.close();
							
							SDK::Output("ChatSpam", std::format("Created and loaded default file at {}", defaultPath).c_str(), {}, true, true);
							fileLoaded = true;
						}
					}
				}
				catch (...)
				{
					// File operations failed, continue to fallback
				}
			}
			
			if (!fileLoaded || m_vChatSpamLines.empty())
			{
				SDK::Output("ChatSpam", "Failed to load or create chat spam file, using built-in messages", {}, true, true);
				m_vChatSpamLines.push_back("Put your chat spam lines in cat_chatspam.txt");
				m_vChatSpamLines.push_back("ChatSpam is running but couldn't find cat_chatspam.txt");
				m_vChatSpamLines.push_back("[Amalgam] Chat Spam is working!");
			}
		}
		catch (...)
		{
			// If we failed completely with file handling, add a single fallback message
			m_vChatSpamLines.push_back("[Amalgam] Chat Spam is working!");
		}
	}
	
	if (m_vChatSpamLines.empty())
	{
		return;
	}
	
	// Enforce reasonable spam interval to prevent overwhelming the engine
	float spamInterval = Vars::Misc::Automation::ChatSpam::Interval.Value;
	if (spamInterval <= 0.2f) // Increased minimum interval to be safer
		spamInterval = 0.2f; 
	
	if (!m_tChatSpamTimer.Run(spamInterval))
		return;
		
	// Get chat line safely
	std::string chatLine;
	const size_t lineCount = m_vChatSpamLines.size();
	
	if (lineCount == 0)
		return;
	
	try
	{
		if (Vars::Misc::Automation::ChatSpam::Randomize.Value)
		{
			// Safer random index generation
			int max = static_cast<int>(lineCount) - 1;
			if (max < 0) max = 0;
			int randomIndex = SDK::RandomInt(0, max);
			
			// Double-check bounds for safety
			if (randomIndex >= 0 && randomIndex < static_cast<int>(lineCount))
				chatLine = m_vChatSpamLines[randomIndex];
			else
				chatLine = "[ChatSpam]";
		}
		else
		{
			// Reset index if invalid
			if (m_iCurrentChatSpamIndex < 0 || m_iCurrentChatSpamIndex >= static_cast<int>(lineCount))
				m_iCurrentChatSpamIndex = 0;
			
			// Extra safety check
			if (m_iCurrentChatSpamIndex >= 0 && m_iCurrentChatSpamIndex < static_cast<int>(lineCount))
			{
				chatLine = m_vChatSpamLines[m_iCurrentChatSpamIndex];
				m_iCurrentChatSpamIndex = (m_iCurrentChatSpamIndex + 1) % static_cast<int>(lineCount);
			}
			else
			{
				chatLine = "[ChatSpam]";
				m_iCurrentChatSpamIndex = 0;
			}
		}
		
		// Limit message length to prevent buffer issues
		if (chatLine.length() > 150) 
		{
			chatLine = chatLine.substr(0, 150);
		}
		
		// Build the chat command
		std::string chatCommand;
		if (Vars::Misc::Automation::ChatSpam::TeamChat.Value)
			chatCommand = "say_team \"" + chatLine + "\"";
		else
			chatCommand = "say \"" + chatLine + "\"";
		
		// Final null check before calling engine
		if (I::EngineClient)
		{
			SDK::Output("ChatSpam", std::format("Sending: {}", chatCommand).c_str(), {}, true, true);
			I::EngineClient->ClientCmd_Unrestricted(chatCommand.c_str());
		}
	}
	catch (...)
	{
		// Silently fail if we encounter any exception during command execution
		return;
	}
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
	{
		I::EngineClient->ClientCmd_Unrestricted("tournament_player_readystate 1");
	}
}

void CMisc::ExecBuyBot(CTFPlayer* pLocal)
{
	if (!Vars::Misc::MannVsMachine::BuyBot.Value)
		return;
	auto pGameRules = I::TFGameRules();
	if (!pGameRules || !pGameRules->m_bPlayingMannVsMachine())
		return;
	if (!pLocal)
		return;
	// cash threshold
	if (Vars::Misc::MannVsMachine::MaxCash.Value > 0 && pLocal->m_nCurrency() >= Vars::Misc::MannVsMachine::MaxCash.Value)
		return;
	if (!pLocal->m_bInUpgradeZone())
		return;
	static auto tfMvmRespec = U::ConVars.FindVar("tf_mvm_respec_enabled");
	if (!tfMvmRespec || tfMvmRespec->GetInt() != 1)
		return;
	float curTime = I::GlobalVars->curtime;
	if (m_buybot_clock > curTime)
		return;
	switch (m_buybot_step)
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
	m_buybot_step = m_buybot_step % 3 + 1;
	m_buybot_clock = curTime + 0.2f;
}

void CMisc::ResetBuyBot()
{
	m_buybot_step = 1;
	m_buybot_clock = 0.0f;
}