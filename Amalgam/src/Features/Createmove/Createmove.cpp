#include "Createmove.h"

#include "../Aimbot/Aimbot.h"
#include "../Backtrack/Backtrack.h"
#include "../CritHack/CritHack.h"
#include "../EnginePrediction/EnginePrediction.h"
#include "../Misc/Misc.h"
#include "../NoSpread/NoSpread.h"
#include "../NoSpread/NoSpreadHitscan/NoSpreadHitscan.h"
#include "../PacketManip/PacketManip.h"
#include "../Resolver/Resolver.h"
#include "../Ticks/Ticks.h"
#include "../Visuals/Visuals.h"
#include "../Visuals/FakeAngle/FakeAngle.h"
#include "../Spectate/Spectate.h"
#include "../NavBot/NavEngine/Controllers/Controller.h"
#include "../NavBot/NavBotCore.h"
#include "../NavBot/NavEngine/NavEngine.h"
#include "../FollowBot/FollowBot.h"
#include "../AutoJoin/AutoJoin.h"
#include "../Misc/AutoItem/AutoItem.h"

MAKE_SIGNATURE(IHasGenericMeter_GetMeterMultiplier, "client.dll", "F3 0F 10 81 ? ? ? ? C3 CC CC CC CC CC CC CC 48 85 D2", 0x0);
MAKE_SIGNATURE(C_BaseAnimating_AutoAllowBoneAccess, "client.dll", "40 53 48 83 EC ? 41 0F B6 C0 44 0F B6 CA", 0x0);
MAKE_SIGNATURE(C_BaseAnimating_AutoAllowBoneAccessOnDelete, "client.dll", "B9 ? ? ? ? E9 ? ? ? ? CC CC CC CC CC CC 48 89 5C 24", 0x0);

void CCreateMove::UpdateInfo(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	G::PSilentAngles = G::SilentAngles = G::Attacking = G::Throwing = false;
	G::LastUserCmd = G::CurrentUserCmd ? G::CurrentUserCmd : pCmd;
	G::CurrentUserCmd = pCmd;
	G::OriginalCmd = *pCmd;

	if (!pWeapon)
		return;

	G::CanPrimaryAttack = G::CanSecondaryAttack = G::Reloading = false;

	if (pWeapon->GetMaxClip1() != WEAPON_NOCLIP && !pWeapon->m_bReloadsSingly())
	{	// dumb fix
		float flOldCurtime = I::GlobalVars->curtime;
		I::GlobalVars->curtime = TICKS_TO_TIME(pLocal->m_nTickBase());
		pWeapon->CheckReload();
		I::GlobalVars->curtime = flOldCurtime;
	}

	for (int i = 0; i <= SLOT_MELEE; i++)
	{
		auto pWeaponInSlot = pLocal->GetWeaponFromSlot(i);
		if (pWeaponInSlot)
		{
			int iDefIndex = pWeaponInSlot->m_iItemDefinitionIndex(), iWeaponID = pWeaponInSlot->GetWeaponID();
			bool bWeaponChanged = G::SavedDefIndexes[i] != iDefIndex || G::SavedWepIds[i] != iWeaponID;
			int iActualWeaponSlot = pWeaponInSlot->GetSlot(); // this whole thing is fucked up
			G::SavedWepSlots[i] = iActualWeaponSlot;
			G::SavedDefIndexes[iActualWeaponSlot] = iDefIndex;
			G::SavedWepIds[iActualWeaponSlot] = iWeaponID;

			if (iActualWeaponSlot != SLOT_MELEE)
			{
				G::AmmoInSlot[iActualWeaponSlot].m_iClip = pWeaponInSlot->m_iClip1();
				G::AmmoInSlot[iActualWeaponSlot].m_iReserve = pLocal->GetAmmoCount(pWeaponInSlot->m_iPrimaryAmmoType());
				if (bWeaponChanged)
				{
					G::AmmoInSlot[iActualWeaponSlot].m_iMaxClip = pWeaponInSlot->GetWeaponInfo() ? pWeaponInSlot->GetWeaponInfo()->iMaxClip1 : 0;
					G::AmmoInSlot[iActualWeaponSlot].m_iMaxReserve = SDK::GetWeaponMaxReserveAmmo(iWeaponID, iDefIndex);
					G::AmmoInSlot[iActualWeaponSlot].m_bUsesAmmo = !SDK::WeaponDoesNotUseAmmo(iWeaponID, iDefIndex);
				}
			}
			else if (i != SLOT_MELEE)
				G::AmmoInSlot[i].m_bUsesAmmo = false;
		}
	}

	bool bCanAttack = pLocal->CanAttack();
	{
		static int iStaticItemDefinitionIndex = 0;
		int iOldItemDefinitionIndex = iStaticItemDefinitionIndex;
		int iNewItemDefinitionIndex = iStaticItemDefinitionIndex = pWeapon->m_iItemDefinitionIndex();

		if (iNewItemDefinitionIndex != iOldItemDefinitionIndex || !bCanAttack || !pWeapon->m_iClip1())
			F::Ticks.m_iWait = -1;
	}
	if (bCanAttack)
	{
		G::CanPrimaryAttack = pWeapon->CanPrimaryAttack();
		G::CanSecondaryAttack = pWeapon->CanSecondaryAttack();

		switch (pWeapon->GetWeaponID())
		{
		case TF_WEAPON_FLAME_BALL:
			if (G::CanPrimaryAttack)
			{
				// do this, otherwise it will be a tick behind
				float flFrametime = TICK_INTERVAL * 100;
				float flMeterMult = S::IHasGenericMeter_GetMeterMultiplier.Call<float>(pWeapon->m_pMeter());
				float flRate = SDK::AttribHookValue(1.f, "item_meter_charge_rate", pWeapon) - 1;
				float flMult = SDK::AttribHookValue(1.f, "mult_item_meter_charge_rate", pWeapon);
				float flTankPressure = pLocal->m_flTankPressure() + flFrametime * flMeterMult / (flRate * flMult);

				if (G::CanPrimaryAttack && flTankPressure < 100.f)
					G::CanPrimaryAttack = G::CanSecondaryAttack = false;
			}
			break;
		case TF_WEAPON_MINIGUN:
		{
			int iState = pWeapon->As<CTFMinigun>()->m_iWeaponState();
			if (iState != AC_STATE_FIRING && iState != AC_STATE_SPINNING || !pWeapon->HasPrimaryAmmoForShot())
				G::CanPrimaryAttack = false;
			break;
		}
		case TF_WEAPON_FLAREGUN_REVENGE:
			if (pCmd->buttons & IN_ATTACK2)
				G::CanPrimaryAttack = false;
			break;
		case TF_WEAPON_BAT_WOOD:
		case TF_WEAPON_BAT_GIFTWRAP:
			if (!pWeapon->HasPrimaryAmmoForShot())
				G::CanSecondaryAttack = false;
			break;
		case TF_WEAPON_MEDIGUN:
		case TF_WEAPON_BUILDER:
		case TF_WEAPON_LASER_POINTER:
			break;
		case TF_WEAPON_PARTICLE_CANNON:
		{
			float flChargeBeginTime = pWeapon->As<CTFParticleCannon>()->m_flChargeBeginTime();
			if (flChargeBeginTime > 0)
			{
				float flTotalChargeTime = TICKS_TO_TIME(pLocal->m_nTickBase()) - flChargeBeginTime;
				if (flTotalChargeTime < TF_PARTICLE_MAX_CHARGE_TIME)
				{
					G::CanPrimaryAttack = G::CanSecondaryAttack = false;
					break;
				}
			}
			[[fallthrough]];
		}
		default:
			if (pWeapon->GetSlot() != SLOT_MELEE)
			{
				bool bAmmo = pWeapon->HasPrimaryAmmoForShot();
				bool bReload = pWeapon->IsInReload();
				if (!bAmmo && pWeapon->m_iItemDefinitionIndex() != Soldier_m_TheBeggarsBazooka)
					G::CanPrimaryAttack = G::CanSecondaryAttack = false;
				if (bReload && bAmmo && !G::CanPrimaryAttack)
					G::Reloading = true;
			}
		}
		if (G::CanPrimaryAttack)
		{
			switch (pWeapon->GetWeaponID())
			{
			case TF_WEAPON_FLAMETHROWER:
			case TF_WEAPON_FLAME_BALL:
			case TF_WEAPON_FLAREGUN:
			case TF_WEAPON_FLAREGUN_REVENGE:
				if (pLocal->IsUnderwater())
					G::CanPrimaryAttack = G::CanSecondaryAttack = false;
			}
		}
	}

	G::Attacking = SDK::IsAttacking(pLocal, pWeapon, pCmd);
	G::PrimaryWeaponType = SDK::GetWeaponType(pWeapon, &G::SecondaryWeaponType);
	G::CanHeadshot = pWeapon->CanHeadshot() || pWeapon->AmbassadorCanHeadshot(TICKS_TO_TIME(pLocal->m_nTickBase()));
}

#ifndef TEXTMODE
void CCreateMove::LocalAnimations(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	static std::vector<Vec3> vAngles = {};
	vAngles.push_back(pCmd->viewangles);
	auto pAnimState = pLocal->m_PlayerAnimState();
	if (G::SendPacket && pAnimState)
	{
		float flOldFrametime = I::GlobalVars->frametime;
		float flOldCurtime = I::GlobalVars->curtime;
		I::GlobalVars->frametime = TICK_INTERVAL;
		I::GlobalVars->curtime = TICKS_TO_TIME(pLocal->m_nTickBase());
		for (auto& vAngle : vAngles)
		{
			if (pLocal->IsTaunting() && pLocal->m_bAllowMoveDuringTaunt())
				pLocal->m_flTauntYaw() = vAngle.y;
			pAnimState->Update(pAnimState->m_flEyeYaw = vAngle.y, vAngle.x);
			pLocal->FrameAdvance(TICK_INTERVAL);
		}
		I::GlobalVars->frametime = flOldFrametime;
		I::GlobalVars->curtime = flOldCurtime;
		vAngles.clear();

		F::FakeAngle.Run(pLocal);
	}
}
#endif


#define MATH_EPSILON (1.f / 16)
#define PSILENT_EPSILON (1.f - MATH_EPSILON)
#define REAL_EPSILON (0.1f + MATH_EPSILON)
#define SNAP_SIZE_EPSILON (10.f - MATH_EPSILON)
#define SNAP_NOISE_EPSILON (0.5f + MATH_EPSILON)

void CCreateMove::AntiCheatCompatibility(CUserCmd* pCmd)
{
	if (!Vars::Misc::Game::AntiCheatCompatibility.Value)
		return;

	Math::ClampAngles(pCmd->viewangles); // shouldn't happen, but failsafe

	static std::deque<CmdHistory_t> vHistory;
	vHistory.emplace_front(pCmd->viewangles, pCmd->buttons & IN_ATTACK, pCmd->buttons & IN_ATTACK2, G::SendPacket);
	if (vHistory.size() > 5)
		vHistory.pop_back();

	if (vHistory.size() < 3)
		return;

	// prevent trigger checks, though this shouldn't happen ordinarily
	if (!vHistory[0].m_bAttack1 && vHistory[1].m_bAttack1 && !vHistory[2].m_bAttack1)
		pCmd->buttons |= IN_ATTACK;
	if (!vHistory[0].m_bAttack2 && vHistory[1].m_bAttack2 && !vHistory[2].m_bAttack2)
		pCmd->buttons |= IN_ATTACK2;

	// don't care if we are actually attacking or not, a miss is less important than a detection
	if (vHistory[0].m_bAttack1 || vHistory[1].m_bAttack1 || vHistory[2].m_bAttack1)
	{
		// prevent silent aim checks
		if (Math::CalcFov(vHistory[0].m_vAngle, vHistory[1].m_vAngle) > PSILENT_EPSILON
			&& Math::CalcFov(vHistory[0].m_vAngle, vHistory[2].m_vAngle) < REAL_EPSILON)
		{
			pCmd->viewangles = vHistory[1].m_vAngle.LerpAngle(vHistory[0].m_vAngle, 0.5f);
			if (Math::CalcFov(pCmd->viewangles, vHistory[2].m_vAngle) < REAL_EPSILON)
				pCmd->viewangles = vHistory[0].m_vAngle + Vec3(0.f, REAL_EPSILON * 2);
			vHistory[0].m_vAngle = pCmd->viewangles;
			vHistory[0].m_bSendingPacket = G::SendPacket = vHistory[1].m_bSendingPacket;
		}

		// prevent aim snap checks
		if (vHistory.size() == 5)
		{
			float flDelta01 = Math::CalcFov(vHistory[0].m_vAngle, vHistory[1].m_vAngle);
			float flDelta12 = Math::CalcFov(vHistory[1].m_vAngle, vHistory[2].m_vAngle);
			float flDelta23 = Math::CalcFov(vHistory[2].m_vAngle, vHistory[3].m_vAngle);
			float flDelta34 = Math::CalcFov(vHistory[3].m_vAngle, vHistory[4].m_vAngle);

			if ((
				flDelta12 > SNAP_SIZE_EPSILON && flDelta23 < SNAP_NOISE_EPSILON && vHistory[2].m_vAngle != vHistory[3].m_vAngle
				|| flDelta23 > SNAP_SIZE_EPSILON && flDelta12 < SNAP_NOISE_EPSILON && vHistory[1].m_vAngle != vHistory[2].m_vAngle
				)
				&& flDelta01 < SNAP_NOISE_EPSILON && vHistory[0].m_vAngle != vHistory[1].m_vAngle
				&& flDelta34 < SNAP_NOISE_EPSILON && vHistory[3].m_vAngle != vHistory[4].m_vAngle)
			{
				pCmd->viewangles.y += SNAP_NOISE_EPSILON * 2;
				vHistory[0].m_vAngle = pCmd->viewangles;
				vHistory[0].m_bSendingPacket = G::SendPacket = vHistory[1].m_bSendingPacket;
			}
		}
	}
}



void CCreateMove::Run(int nSequenceNum, float flInputSampleFrametime, bool bActive)
{
	{
		char autoallow[16];
		S::C_BaseAnimating_AutoAllowBoneAccess.Call<void>(autoallow, true, false);
		I::MDLCache->BeginLock();
		I::Input->CreateMove(nSequenceNum, flInputSampleFrametime, bActive);
		I::MDLCache->EndLock();
		S::C_BaseAnimating_AutoAllowBoneAccessOnDelete.Call<void>(autoallow);
	}

#ifdef DEBUG_HOOKS
	if (!Vars::Hooks::CHLClient_CreateMove[DEFAULT_BIND])
		return;
#endif

	auto pLocal = H::Entities.GetLocal();
	if (!pLocal)
		return;

	auto pWeapon = H::Entities.GetWeapon();
	CUserCmd* pCmd = &I::Input->m_pCommands[nSequenceNum % MULTIPLAYER_BACKUP];
	I::Prediction->Update(I::ClientState->m_nDeltaTick, I::ClientState->m_nDeltaTick > 0, I::ClientState->last_command_ack, I::ClientState->lastoutgoingcommand + I::ClientState->chokedcommands);

	UpdateInfo(pLocal, pWeapon, pCmd);
#ifndef TEXTMODE
	F::Spectate.CreateMove(pCmd);
#endif
	F::Misc.RunPre(pLocal, pCmd);
	F::AutoJoin.Run(pLocal);
	F::AutoItem.Run(pLocal);
	F::GameObjectiveController.Update();

	F::BotUtils.Run(pLocal, pWeapon, pCmd);
	F::Ticks.Start(pLocal, pCmd);
	F::Aimbot.Run(pLocal, pWeapon, pCmd);
	F::Backtrack.CreateMove(pLocal, pWeapon, pCmd);
	F::Ticks.End(pLocal, pCmd);
	F::FollowBot.Run(pLocal, pWeapon, pCmd);
	F::NavBotCore.Run(pLocal, pWeapon, pCmd);
	F::NavEngine.Run(pLocal, pWeapon, pCmd);
	F::BotUtils.HandleSmartJump(pLocal, pCmd);
	F::CritHack.Run(pLocal, pWeapon, pCmd);
	F::NoSpread.Run(pLocal, pWeapon, pCmd);
	F::Resolver.CreateMove(pLocal);
	F::Misc.RunPost(pLocal, pCmd);
	F::PacketManip.Run(pLocal, pWeapon, pCmd);
#ifndef TEXTMODE
	F::Visuals.CreateMove(pLocal, pWeapon);
#endif
	F::Ticks.CreateMove(pLocal, pWeapon, pCmd);
	F::AntiAim.Run(pLocal, pWeapon, pCmd);
	F::NoSpreadHitscan.AskForPlayerPerf();
	F::EnginePrediction.End(pLocal, pCmd);

	AntiCheatCompatibility(pCmd);
#ifndef TEXTMODE
	LocalAnimations(pLocal, pCmd);
#endif

	G::Choking = !G::SendPacket;
	G::LastUserCmd = pCmd;
}