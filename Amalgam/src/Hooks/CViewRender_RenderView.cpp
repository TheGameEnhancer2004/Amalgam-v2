#include "../SDK/SDK.h"

#include "../Features/Visuals/CameraWindow/CameraWindow.h"

#ifndef TEXTMODE
MAKE_HOOK(CViewRender_RenderView, U::Memory.GetVirtual(I::ViewRender, 6), void,
	void* rcx, const CViewSetup& view, ClearFlags_t nClearFlags, RenderViewInfo_t whatToDraw)
{
#ifdef DEBUG_HOOKS
	if (!Vars::Hooks::CViewRender_RenderView[DEFAULT_BIND])
		return CALL_ORIGINAL(rcx, view, nClearFlags, whatToDraw);
#endif

	CALL_ORIGINAL(rcx, view, nClearFlags, whatToDraw);
	if (SDK::CleanScreenshot() || G::Unload)
		return;

	F::CameraWindow.RenderView(rcx, view);
}
#endif
