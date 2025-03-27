#include "../SDK/SDK.h"

#include "../Features/Visuals/Materials/Materials.h"
#include "../Features/Visuals/Visuals.h"
#include "../Features/Backtrack/Backtrack.h"
#include "../Features/CheaterDetection/CheaterDetection.h"
#include "../Features/NoSpread/NoSpreadHitscan/NoSpreadHitscan.h"
#include "../Features/Resolver/Resolver.h"
#include "../Features/TickHandler/TickHandler.h"
#include "../Features/NavBot/NavEngine/Controllers/Controller.h"
#include "../Features/NavBot/NavEngine/NavEngine.h"
#include "../Features/NavBot/NavBot.h"

MAKE_HOOK(CViewRender_LevelInit, U::Memory.GetVFunc(I::ViewRender, 1), void,
	void* rcx)
{
#ifdef DEBUG_HOOKS
	if (!Vars::Hooks::CViewRender_LevelInit.Map[DEFAULT_BIND])
		return CALL_ORIGINAL(rcx);
#endif
#ifndef TEXTMODE
	F::Materials.ReloadMaterials();
	F::Visuals.OverrideWorldTextures();
#endif

	F::Backtrack.Reset();
	F::Resolver.Reset();
	F::Ticks.Reset();
	F::NoSpreadHitscan.Reset();
	F::CheaterDetection.Reset();
	F::GameObjectiveController.Reset();
	F::NavEngine.Reset();
	F::NavBot.Reset();

	CALL_ORIGINAL(rcx);
}