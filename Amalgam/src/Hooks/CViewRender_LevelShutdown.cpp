#ifndef TEXTMODE
#include "../SDK/SDK.h"

#include "../Features/Visuals/Materials/Materials.h"
#include "../Features/Killstreak/Killstreak.h"

MAKE_HOOK(CViewRender_LevelShutdown, U::Memory.GetVFunc(I::ViewRender, 2), void,
	void* rcx)
{
	F::Materials.UnloadMaterials();
	F::Killstreak.Reset();

	CALL_ORIGINAL(rcx);
}
#endif