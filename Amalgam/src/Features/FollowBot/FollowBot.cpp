#include "FollowBot.h"
#include "../Misc/Misc.h"
#include "../Players/PlayerUtils.h"
#include "../NavBot/BotUtils.h"

void CFollowBot::UpdateTargets(CTFPlayer* pLocal)
{
	m_vTargets.clear();
	auto pResource = H::Entities.GetResource();
	if (!pResource)
		return;

	auto vLocalOrigin = pLocal->GetAbsOrigin();
	const EGroupType eGroup = Vars::Misc::Movement::FollowBot::Targets.Value & Vars::Misc::Movement::FollowBot::TargetsEnum::Teammates &&
		Vars::Misc::Movement::FollowBot::Targets.Value & Vars::Misc::Movement::FollowBot::TargetsEnum::Enemies ? EGroupType::PLAYERS_ALL :
		Vars::Misc::Movement::FollowBot::Targets.Value & Vars::Misc::Movement::FollowBot::TargetsEnum::Teammates ? EGroupType::PLAYERS_TEAMMATES : EGroupType::PLAYERS_ENEMIES;

	float flMaxDist = Vars::Misc::Movement::FollowBot::UseNav.Value && F::NavEngine.IsNavMeshLoaded() ? Vars::Misc::Movement::FollowBot::NavAbandonDistance.Value : Vars::Misc::Movement::FollowBot::ActivationDistance.Value;
	bool bTryDormant = Vars::Misc::Movement::FollowBot::UseNav.Value == Vars::Misc::Movement::FollowBot::UseNavEnum::Dormant && F::NavEngine.IsNavMeshLoaded();
	for (auto pEntity : H::Entities.GetGroup(eGroup))
	{
		int iEntIndex = pEntity->entindex();
		if (pLocal->entindex() != iEntIndex && pResource->m_bValid(iEntIndex))
		{
			auto pPlayer = pEntity->As<CTFPlayer>();
			bool bDormant = pPlayer->IsDormant();
			int iPriority = F::PlayerUtils.GetFollowPriority(iEntIndex);
			if (iPriority >= Vars::Misc::Movement::FollowBot::MinPriority.Value && (bTryDormant || !bDormant) && pPlayer->IsAlive() && !pPlayer->IsAGhost())
			{
				Vec3 vOrigin;
				float flDistance = FLT_MAX;
				if (bDormant)
				{
					auto vDormantOrigin = F::NavParser.GetDormantOrigin(iEntIndex);
					if (vDormantOrigin.has_value())
						vOrigin = *vDormantOrigin;
				}
				else
					vOrigin = pPlayer->GetAbsOrigin();

				if (!vOrigin.IsZero())
					flDistance = vLocalOrigin.DistTo(vOrigin);

				if (flDistance <= flMaxDist)
					m_vTargets.emplace_back(iEntIndex, pResource->m_iUserID(iEntIndex), iPriority, flDistance, false, true, bDormant, vOrigin, FNV1A::Hash32(F::PlayerUtils.GetPlayerName(iEntIndex, pResource->GetName(iEntIndex))), pPlayer);
			}
		}
	}

	std::sort(m_vTargets.begin(), m_vTargets.end(), [&](const FollowTarget_t& a, const FollowTarget_t& b) -> bool
			  {
				  if (a.m_iPriority != b.m_iPriority)
					  return a.m_iPriority > b.m_iPriority;

				  return a.m_flDistance < b.m_flDistance;
			  });
}

void CFollowBot::UpdateLockedTarget(CTFPlayer* pLocal)
{
	if (m_tLockedTarget.m_iUserID == -1)
		return;

	if ((m_tLockedTarget.m_iEntIndex = I::EngineClient->GetPlayerForUserID(m_tLockedTarget.m_iUserID)) <= 0)
	{
		Reset(FB_RESET_NAV);
		return;
	}

	// Did our target leave and someone took their uid? 
	// Should never happen unless we had a huge network lag
	// Actually i dont think this will ever happen in any case as i havent seen uids being reused in the same match
	/*
	auto pResource = H::Entities.GetResource();
	if (pResource && pResource->m_bValid(m_tLockedTarget.m_iEntIndex) && 
		FNV1A::Hash32(F::PlayerUtils.GetPlayerName(m_tLockedTarget.m_iEntIndex, pResource->GetName(m_tLockedTarget.m_iEntIndex))) != m_tLockedTarget.m_uNameHash)
	{
		Reset(FB_RESET_NAV);
		return;
	}
	*/

	if (!(m_tLockedTarget.m_pPlayer = I::ClientEntityList->GetClientEntity(m_tLockedTarget.m_iEntIndex)->As<CTFPlayer>()))
		return;

	if (!IsValidTarget(pLocal, m_tLockedTarget.m_pPlayer))
	{
		Reset(FB_RESET_NAV);
		return;
	}

	auto vLocalOrigin = pLocal->GetAbsOrigin();
	float flDistance = FLT_MAX;
	bool bCanNav = Vars::Misc::Movement::FollowBot::UseNav.Value && F::NavEngine.IsNavMeshLoaded();
	if (!(m_tLockedTarget.m_bDormant = m_tLockedTarget.m_pPlayer->IsDormant()))
		flDistance = vLocalOrigin.DistTo(m_tLockedTarget.m_vLastKnownPos = m_tLockedTarget.m_pPlayer->GetAbsOrigin());
	else if (Vars::Misc::Movement::FollowBot::UseNav.Value == Vars::Misc::Movement::FollowBot::UseNavEnum::Dormant && bCanNav)
	{
		auto vOrigin = F::NavParser.GetDormantOrigin(m_tLockedTarget.m_iEntIndex);
		if (vOrigin.has_value())
			m_tLockedTarget.m_vLastKnownPos = *vOrigin;

		if (m_tLockedTarget.m_vLastKnownPos.IsZero())
		{
			Reset(FB_RESET_NAV);
			return;
		}
		flDistance = Vars::Misc::Movement::FollowBot::NavAbandonDistance.Value;
	}

	float flMaxDist = bCanNav ? Vars::Misc::Movement::FollowBot::NavAbandonDistance.Value : Vars::Misc::Movement::FollowBot::AbandonDistance.Value;
	if (flDistance > flMaxDist)
	{
		Reset(FB_RESET_NAV);
		return;
	}

	m_tLockedTarget.m_flDistance = flDistance;
	m_tLockedTarget.m_bNew = false;
}

bool CFollowBot::IsValidTarget(CTFPlayer* pLocal, CTFPlayer* pPlayer)
{
	if (!pPlayer || !pPlayer->IsPlayer() || !pPlayer->IsAlive() || pPlayer->IsAGhost())
		return false;

	if (pPlayer->m_iTeamNum() != pLocal->m_iTeamNum())
	{
		if (Vars::Misc::Movement::FollowBot::Targets.Value & Vars::Misc::Movement::FollowBot::TargetsEnum::Enemies)
			return true;
	}
	else if (Vars::Misc::Movement::FollowBot::Targets.Value & Vars::Misc::Movement::FollowBot::TargetsEnum::Teammates)
		return true;

	return false;
}

void CFollowBot::LookAtPath(CTFPlayer* pLocal, CUserCmd* pCmd, std::deque<Vec3>* vIn, bool bSmooth)
{
	bool bSilent = Vars::Misc::Movement::FollowBot::LookAtPath.Value == Vars::Misc::Movement::FollowBot::LookAtPathEnum::Silent;
	if (!bSilent || !G::AntiAim)
	{
		if (G::Attacking != 1)
		{
			switch (Vars::Misc::Movement::FollowBot::LookAtPathMode.Value)
			{
			case Vars::Misc::Movement::FollowBot::LookAtPathModeEnum::Path:
				F::BotUtils.LookAtPath(pCmd, vIn->front().Get2D(), pLocal->GetEyePosition(), bSilent);
				break;
			case Vars::Misc::Movement::FollowBot::LookAtPathModeEnum::Copy:
				if (!vIn->size())
					return;
				[[fallthrough]];
			default:
				F::BotUtils.LookAtPath(pCmd, vIn->front(), pLocal->GetEyePosition(), bSilent, bSmooth);
				break;
			}
		}
	}
	if (Vars::Misc::Movement::FollowBot::LookAtPathMode.Value == Vars::Misc::Movement::FollowBot::LookAtPathModeEnum::Copy && vIn->size())
		vIn->pop_front();
}

void CFollowBot::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (!Vars::Misc::Movement::FollowBot::Enabled.Value ||
		!Vars::Misc::Movement::FollowBot::Targets.Value || 
		!pLocal->IsAlive() || pLocal->IsTaunting())
	{
		Reset(FB_RESET_TARGETS | FB_RESET_NAV);
		return;
	}

	if (pCmd->buttons & (IN_FORWARD | IN_BACK | IN_MOVERIGHT | IN_MOVELEFT) && !F::Misc.m_bAntiAFK)
	{
		Reset(FB_RESET_TARGETS | FB_RESET_NAV);
		return;
	}

	if (!Vars::Misc::Movement::NavBot::Enabled.Value && pWeapon && F::BotUtils.m_iCurrentSlot != F::BotUtils.m_iBestSlot)
		F::BotUtils.SetSlot(pLocal, Vars::Misc::Movement::BotUtils::WeaponSlot.Value ? F::BotUtils.m_iBestSlot : -1);

	UpdateTargets(pLocal);
	UpdateLockedTarget(pLocal);

	if (!m_vTargets.size())
	{
		if (m_tLockedTarget.m_iUserID == -1)
		{
			Reset(FB_RESET_NAV);
			return;
		}
	}
	else
	{
		if (m_tLockedTarget.m_iUserID == -1 || (m_tLockedTarget.m_iUserID && m_tLockedTarget.m_uNameHash != m_vTargets.front().m_uNameHash))
		{
			// Our target is invalid or no longer at highest priority
			Reset(FB_RESET_NAV);
			m_tLockedTarget = m_vTargets.front();
		}
	}

	Vec3 vLocalOrigin = pLocal->GetAbsOrigin();
	if (F::NavEngine.current_priority == followbot)
	{
		bool bClose = vLocalOrigin.DistTo(m_tLockedTarget.m_vLastKnownPos) < Vars::Misc::Movement::FollowBot::FollowDistance.Value + 150.f;
		
		// Target is too close or we require repathing, cancel pathing
		if ((!m_tLockedTarget.m_bNew && bClose) ||
			(!m_tLockedTarget.m_vLastKnownPos.IsZero() &&
			F::NavEngine.crumbs.size() &&
			m_tLockedTarget.m_vLastKnownPos.DistTo(F::NavEngine.crumbs.back().vec) >= Vars::Misc::Movement::FollowBot::AbandonDistance.Value))
		{
			if (bClose && m_tLockedTarget.m_bDormant)
			{
				// We reached our goal but the target is nowhere to be found
				Reset(FB_RESET_NAV);
				return;
			}
			m_tLockedTarget.m_bUnreachable = false;
			F::NavEngine.cancelPath();
		}
	}

	if (F::NavEngine.current_priority != followbot)
	{
		if (m_tLockedTarget.m_bUnreachable ||
			m_tLockedTarget.m_bDormant ||
			(m_tLockedTarget.m_bNew && (m_tLockedTarget.m_flDistance >= Vars::Misc::Movement::FollowBot::FollowDistance.Value)) ||
			(m_tLockedTarget.m_flDistance >= Vars::Misc::Movement::FollowBot::AbandonDistance.Value) ||
			(m_vCurrentPath.size() >= Vars::Misc::Movement::FollowBot::MaxNodes.Value))
		{
			bool bNav = false;
			if (Vars::Misc::Movement::FollowBot::UseNav.Value && F::NavEngine.IsNavMeshLoaded() && !m_tLockedTarget.m_vLastKnownPos.IsZero() &&
				((!m_tLockedTarget.m_bDormant && Vars::Misc::Movement::FollowBot::UseNav.Value) ||
				(m_tLockedTarget.m_bDormant && (Vars::Misc::Movement::FollowBot::UseNav.Value == Vars::Misc::Movement::FollowBot::UseNavEnum::Dormant))))
				bNav = F::NavEngine.navTo(m_tLockedTarget.m_vLastKnownPos, followbot);

			// We couldn't find a path to the target
			if (!bNav)
				Reset(FB_RESET_NONE);
			else
			{
				m_bActive = false;
				m_vCurrentPath.clear();
				m_vTempAngles.clear();
			}

			return;
		}
	}
	else
	{
		if (F::BotUtils.m_iCurrentSlot != F::BotUtils.m_iBestSlot)
			F::BotUtils.SetSlot(pLocal, Vars::Misc::Movement::BotUtils::WeaponSlot.Value ? F::BotUtils.m_iBestSlot : -1);

		// Already pathing, no point in running everything else
		return;
	}

	if (m_tLockedTarget.m_pPlayer)
	{
		auto vCurrentOrigin = m_tLockedTarget.m_pPlayer->GetAbsOrigin();
		if (vLocalOrigin.DistTo(vCurrentOrigin) >= Vars::Misc::Movement::FollowBot::AbandonDistance.Value)
		{
			Reset(FB_RESET_NONE);
			return;
		}
		m_bActive = true;

		float flDistToCurrent = vLocalOrigin.DistTo2D(vCurrentOrigin);
		if (flDistToCurrent <= Vars::Misc::Movement::FollowBot::FollowDistance.Value ||
			(m_vCurrentPath.size() > 8 && flDistToCurrent < vLocalOrigin.DistTo2D(m_vCurrentPath.front().m_vOrigin)))
		{
			m_vCurrentPath.clear();
			m_vTempAngles.clear();
		}
		else
		{
			if (Vars::Misc::Movement::FollowBot::LookAtPath.Value && Vars::Misc::Movement::FollowBot::LookAtPathMode.Value == Vars::Misc::Movement::FollowBot::LookAtPathModeEnum::Copy)
				m_vTempAngles.push_back(m_tLockedTarget.m_pPlayer->GetEyeAngles());
			else
				m_vTempAngles.clear();

			if (m_vCurrentPath.size())
			{
				if (m_vCurrentPath.back().m_vOrigin.DistTo2D(vCurrentOrigin) >= 20.f)
				{
					m_vCurrentPath.push_back({ vCurrentOrigin, m_vTempAngles });
					m_vTempAngles.clear();
				}
			}
			else
			{
				m_vCurrentPath.push_back({ vCurrentOrigin, m_vTempAngles });
				m_vTempAngles.clear();
			}
		}
	}

	Vec3 vDest;
	bool bShouldWalk = false;
	std::deque<Vec3>* pCurrentAngles = nullptr;
	if (m_vCurrentPath.size() > 1)
	{
		auto begin = m_vCurrentPath.rbegin();
		auto eraseAt = begin;
		
		// Iterate in reverse so we can optimize the path by erasing nodes older than a close one
		for (auto it = begin, end = m_vCurrentPath.rend(); it != end; ++it)
		{
			if (vLocalOrigin.DistTo2D(it->m_vOrigin) <= 20.f)
			{
				eraseAt = it;

				// Go back to the last checked node
				if (it != begin)
					--it;
			}

			// We found a closest node or reached the beginning of the path
			if (eraseAt != begin || it == end-1)
			{
				vDest = it->m_vOrigin;
				if ((vDest.z - vLocalOrigin.z) <= PLAYER_CROUCHED_JUMP_HEIGHT)
				{
					pCurrentAngles = &it->m_vAngles;
					bShouldWalk = true;
				}
				// Our goal is too high up we cant reach our target
				else m_tLockedTarget.m_bUnreachable = true;
				break;
			}
		}	
		if (m_tLockedTarget.m_bUnreachable)
			m_vCurrentPath.clear();
		else if (eraseAt != begin)
			m_vCurrentPath.erase(m_vCurrentPath.begin(), eraseAt.base());
	}
	else if (m_vCurrentPath.size())
	{
		vDest = m_vCurrentPath.front().m_vOrigin;
		pCurrentAngles = &m_vCurrentPath.front().m_vAngles;
		bShouldWalk = true;
	}

	if (Vars::Misc::Movement::FollowBot::LookAtPath.Value)
	{
		std::deque<Vec3> vCurrentAngles;
		if (!pCurrentAngles || Vars::Misc::Movement::FollowBot::LookAtPathMode.Value == Vars::Misc::Movement::FollowBot::LookAtPathModeEnum::CopyImmediate)
			vCurrentAngles.push_back(m_tLockedTarget.m_pPlayer ? m_vLastTargetAngles = m_tLockedTarget.m_pPlayer->GetEyeAngles() : m_tLockedTarget.m_iEntIndex != -1 ? m_vLastTargetAngles : I::EngineClient->GetViewAngles());
		else if (Vars::Misc::Movement::FollowBot::LookAtPathMode.Value == Vars::Misc::Movement::FollowBot::LookAtPathModeEnum::Path)
			vCurrentAngles.push_back(vDest);
		
		std::deque<Vec3>* pFinalAngles = vCurrentAngles.size() ? &vCurrentAngles : pCurrentAngles;
		LookAtPath(pLocal, pCmd, pFinalAngles, Vars::Misc::Movement::FollowBot::LookAtPathNoSnap.Value && Math::CalcFov(pFinalAngles->front(), F::BotUtils.m_vLastAngles) > 3.f);
	}

	if (F::BotUtils.m_iCurrentSlot != F::BotUtils.m_iBestSlot)
		F::BotUtils.SetSlot(pLocal, Vars::Misc::Movement::BotUtils::WeaponSlot.Value ? F::BotUtils.m_iBestSlot : -1);

	if (!bShouldWalk)
		return;

	static Vec2 vLastLocalOrigin;
	static float flLast2DVel;
	float flCurrent2DVel = pLocal->m_vecVelocity().Length2D(), flDot = vLastLocalOrigin.DistTo(vLocalOrigin.Get2D()) <= 20.f && (flCurrent2DVel <= 5.f && flLast2DVel <= 5.f) ? 1.f : 0.f;
	if (flCurrent2DVel > flLast2DVel)
	{
		// Check if we are currently moving in a right direction so that our jumps dont slow us down as much
		auto vToDest = Math::CalcAngle(vLocalOrigin, vDest);
		Vec3 vForward; Math::AngleVectors(pLocal->m_vecVelocity().ToAngle(), &vForward);
		flDot = vToDest.Normalized().Dot(vForward.Normalized2D());
	}
	static Timer tOriginUpdateTimer;
	if (tOriginUpdateTimer.Run(0.05f))
		vLastLocalOrigin = vLocalOrigin.Get2D();
	flLast2DVel = flCurrent2DVel;

	float flHeightDiff = vDest.z - vLocalOrigin.z;
	if (flDot > 0.65f && flHeightDiff > pLocal->m_flStepSize() && vLocalOrigin.DistTo(vDest) <= 140.f)
	{
		static bool bLastAttempted = false;
		const bool bCurGrounded = pLocal->m_hGroundEntity();
		if (!bCurGrounded || !pLocal->IsDucking())
		{
			if (bCurGrounded)
			{
				if (!bLastAttempted)
					pCmd->buttons |= IN_JUMP;
			}
			else if (flHeightDiff > PLAYER_JUMP_HEIGHT)
				pCmd->buttons |= IN_DUCK;
		}
		bLastAttempted = pCmd->buttons & IN_JUMP;
	}

	SDK::WalkTo(pCmd, pLocal, vDest);
}

void CFollowBot::Reset(int iFlags)
{
	if (iFlags & FB_RESET_NAV && F::NavEngine.current_priority == followbot)
		F::NavEngine.cancelPath();
	if (iFlags & FB_RESET_TARGETS)
		m_vTargets.clear();

	m_vCurrentPath.clear();
	m_vTempAngles.clear();
	m_tLockedTarget = FollowTarget_t{};
	m_bActive = false;
}

void CFollowBot::Render()
{
	if (!Vars::Misc::Movement::FollowBot::DrawPath.Value || !m_vCurrentPath.size())
		return;

	if (m_vCurrentPath.size() > 1)
	{
		for (size_t i = 0; i < m_vCurrentPath.size() - 1; i++)
		{
			H::Draw.RenderBox(m_vCurrentPath[i].m_vOrigin, Vector(-1.0f, -1.0f, -1.0f), Vector(1.0f, 1.0f, 1.0f), Vector(), Vars::Colors::FollowbotPathBox.Value, false);
			H::Draw.RenderLine(m_vCurrentPath[i].m_vOrigin, m_vCurrentPath[i + 1].m_vOrigin, Vars::Colors::FollowbotPathLine.Value, false);
		}
	}
	H::Draw.RenderBox(m_vCurrentPath.back().m_vOrigin, Vector(-1.0f, -1.0f, -1.0f), Vector(1.0f, 1.0f, 1.0f), Vector(), Vars::Colors::FollowbotPathBox.Value, false);
}
