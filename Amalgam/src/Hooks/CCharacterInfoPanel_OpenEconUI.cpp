#ifdef TEXTMODE
#include "../SDK/SDK.h"

MAKE_SIGNATURE(CCharacterInfoPanel_OpenEconUI, "client.dll", "44 88 4C 24 ? 44 88 44 24 ? 53 55", 0x0);

MAKE_HOOK(CCharacterInfoPanel_OpenEconUI, S::CCharacterInfoPanel_OpenEconUI(), void*,
	void* rcx, int iDirectToPage, bool bCheckForInventorySpaceOnExit)
{
	return rcx;
}
#endif