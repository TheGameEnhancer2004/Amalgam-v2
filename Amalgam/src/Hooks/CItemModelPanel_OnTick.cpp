#include "../SDK/SDK.h"

// i feel like its making more crashes than it is fixing

// MAKE_SIGNATURE(CItemModelPanel_OnTick, "client.dll", "48 89 5C 24 ? 56 48 83 EC ? 48 8B F1 E8 ? ? ? ? 48 8B 8E", 0x0);

// MAKE_HOOK(CItemModelPanel_OnTick, S::CItemModelPanel_OnTick(), void,
// 	void* rcx, void* a1)
// {
// #ifdef DEBUG_HOOKS
// 	if (!Vars::Hooks::CItemModelPanel_OnTick[DEFAULT_BIND])
// 		return CALL_ORIGINAL(rcx, a1);
// #endif

// 	return;
// }
