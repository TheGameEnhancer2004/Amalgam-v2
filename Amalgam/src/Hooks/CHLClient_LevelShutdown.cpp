#include "../SDK/SDK.h"

#include "../Features/EnginePrediction/EnginePrediction.h"
#include "../Features/Spectate/Spectate.h"
#include "../Features/NavBot/NavEngine/NavEngine.h"
#include "../Features/NavBot/DangerManager/DangerManager.h"
#include "../Features/NavBot/NavBotJobs/GetSupplies.h"

MAKE_HOOK(CHLClient_LevelShutdown, U::Memory.GetVirtual(I::Client, 7), void,
	void* rcx)
{
#ifdef DEBUG_HOOKS
	if (!Vars::Hooks::CHLClient_LevelShutdown[DEFAULT_BIND])
		return CALL_ORIGINAL(rcx);
#endif

	H::Entities.Clear(true);
	F::EnginePrediction.Unload();
	F::Spectate.m_iIntendedTarget = -1;
#ifndef TEXTMODE
	G::TriggerStorage.clear();
#endif
	F::NavEngine.ClearRespawnRooms();
	F::DangerManager.Reset();
	F::NavBotSupplies.ResetCachedOrigins();

	CALL_ORIGINAL(rcx);
}