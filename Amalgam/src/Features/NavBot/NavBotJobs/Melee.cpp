#include "Melee.h"
#include "Reload.h"
#include "../NavEngine/NavEngine.h"

bool CNavBotMelee::Run(CUserCmd* pCmd, CTFPlayer* pLocal, int iSlot, ClosestEnemy_t tClosestEnemy)
{
	if (iSlot != SLOT_MELEE || F::NavBotReload.m_iLastReloadSlot != -1)
	{
		if (F::NavEngine.m_eCurrentPriority == PriorityListEnum::MeleeAttack)
			F::NavEngine.CancelPath();
		return false;
	}

	auto pEntity = I::ClientEntityList->GetClientEntity(tClosestEnemy.m_iEntIdx);
	if (!pEntity || pEntity->IsDormant())
		return F::NavEngine.m_eCurrentPriority == PriorityListEnum::MeleeAttack;

	auto pPlayer = pEntity->As<CTFPlayer>();
	if (pPlayer->IsInvulnerable() && G::SavedDefIndexes[SLOT_MELEE] != Heavy_t_TheHolidayPunch)
		return false;

	// Too far away
	if (tClosestEnemy.m_flDist > Vars::Misc::Movement::NavBot::MeleeTargetRange.Value)
		return false;

	// Too high priority, so don't try
	if (F::NavEngine.m_eCurrentPriority > PriorityListEnum::MeleeAttack)
		return false;

	static bool bIsVisible = false;
	static Timer tVischeckCooldown{};
	if (tVischeckCooldown.Run(0.2f))
	{
		CGameTrace trace;
		CTraceFilterHitscan filter{}; filter.pSkip = pLocal;
		SDK::TraceHull(pLocal->GetShootPos(), pPlayer->GetAbsOrigin(), pLocal->m_vecMins() * 0.3f, pLocal->m_vecMaxs() * 0.3f, MASK_PLAYERSOLID, &filter, &trace);
		bIsVisible = trace.DidHit() ? trace.m_pEnt && trace.m_pEnt == pPlayer : true;
	}

	Vector vTargetOrigin = pPlayer->GetAbsOrigin();
	Vector vLocalOrigin = pLocal->GetAbsOrigin();
	// If we are close enough, don't even bother with using the navparser to get there
	if (tClosestEnemy.m_flDist < 100.0f && bIsVisible)
	{
		// Crouch if we are standing on someone
		if (pLocal->m_hGroundEntity().Get() && pLocal->m_hGroundEntity().Get()->IsPlayer())
			pCmd->buttons |= IN_DUCK;

		SDK::WalkTo(pCmd, pLocal, vTargetOrigin);
		F::NavEngine.CancelPath();
		F::NavEngine.m_eCurrentPriority = PriorityListEnum::MeleeAttack;
		return true;
	}

	// Don't constantly path, it's slow.
	// The closer we are, the more we should try to path
	static Timer tMeleeCooldown{};
	if (!tMeleeCooldown.Run(tClosestEnemy.m_flDist < 100.f ? 0.2f : tClosestEnemy.m_flDist < 1000.f ? 0.5f : 2.f) && F::NavEngine.IsPathing())
		return F::NavEngine.m_eCurrentPriority == PriorityListEnum::MeleeAttack;

	// Just walk at the enemy l0l
	if (F::NavEngine.NavTo(vTargetOrigin, PriorityListEnum::MeleeAttack, true, !F::NavEngine.IsPathing()))
		return true;
	return false;
}
