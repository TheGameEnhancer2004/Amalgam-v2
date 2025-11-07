#ifndef TEXTMODE
#include "../SDK/SDK.h"

#include "../Features/Players/PlayerUtils.h"

MAKE_SIGNATURE(GetPlayerNameForSteamID_GetFriendPersonaName_Call, "client.dll", "41 B9 ? ? ? ? 44 8B C3 48 8B C8", 0x0);

MAKE_HOOK(ISteamFriends_GetFriendPersonaName, U::Memory.GetVirtual(I::SteamFriends, 7), const char*,
	void* rcx, CSteamID steamIDFriend)
{
#ifdef DEBUG_HOOKS
	if (!Vars::Hooks::ISteamFriends_GetFriendPersonaName[DEFAULT_BIND])
		return CALL_ORIGINAL(rcx, steamIDFriend);
#endif

	static const auto dwDesired = S::GetPlayerNameForSteamID_GetFriendPersonaName_Call();
	const auto dwRetAddr = uintptr_t(_ReturnAddress());

	if (Vars::Visuals::UI::StreamerMode.Value && dwRetAddr == dwDesired)
	{
		switch (F::PlayerUtils.GetNameType(steamIDFriend.GetAccountID()))
		{
		case NameTypeEnum::Local: return "Local";
		case NameTypeEnum::Friend: return "Friend";
		case NameTypeEnum::Party: return "Party";
		}
	}

	return CALL_ORIGINAL(rcx, steamIDFriend);
}
#endif