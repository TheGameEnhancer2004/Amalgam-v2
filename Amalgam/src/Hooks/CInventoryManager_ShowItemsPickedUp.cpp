#ifdef TEXTMODE
#include "../SDK/SDK.h"

MAKE_SIGNATURE(CInventoryManager_ShowItemsPickedUp, "client.dll", "44 88 4C 24 ? 44 88 44 24 ? 53 55", 0x0);

MAKE_HOOK(CInventoryManager_ShowItemsPickedUp, S::CInventoryManager_ShowItemsPickedUp(), bool,
	void* rcx, bool bForce, bool bReturnToGame, bool bNoPanel)
{
	CALL_ORIGINAL(rcx, true, true, true);
	return false;
}
#endif