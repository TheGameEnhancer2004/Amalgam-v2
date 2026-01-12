#include "../SDK/SDK.h"

#include "../Features/Visuals/Materials/Materials.h"
#include "../Features/Visuals/Visuals.h"
#include "../Features/Backtrack/Backtrack.h"
#include "../Features/Ticks/Ticks.h"
#include "../Features/NoSpread/NoSpreadHitscan/NoSpreadHitscan.h"
#include "../Features/CheaterDetection/CheaterDetection.h"
#include "../Features/Resolver/Resolver.h"
#include "../Features/Spectate/Spectate.h"
#include "../Features/NavBot/NavEngine/Controllers/Controller.h"
#include "../Features/NavBot/NavBotCore.h"
#include "../Features/NavBot/NavEngine/NavEngine.h"
#include "../Features/Killstreak/Killstreak.h"
#include "../Features/FollowBot/FollowBot.h"

#ifndef TEXTMODE
MAKE_HOOK(CViewRender_LevelInit, U::Memory.GetVirtual(I::ViewRender, 1), void,
	void* rcx)
{
#ifdef DEBUG_HOOKS
	if (!Vars::Hooks::CViewRender_LevelInit[DEFAULT_BIND])
		return CALL_ORIGINAL(rcx);
#endif

	F::Materials.ReloadMaterials();
	F::Visuals.OverrideWorldTextures();
	F::Killstreak.Reset();
	F::Spectate.m_iIntendedTarget = -1;

	F::Backtrack.Reset();
	F::Ticks.Reset();
	F::NoSpreadHitscan.Reset();
	F::CheaterDetection.Reset();
	F::Resolver.Reset();
	F::GameObjectiveController.Reset();
	F::NavEngine.Reset();
	F::NavBotCore.Reset();
	F::BotUtils.Reset();
	F::FollowBot.Reset();

	CALL_ORIGINAL(rcx);
}
#endif