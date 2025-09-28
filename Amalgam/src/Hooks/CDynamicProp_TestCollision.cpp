#ifdef TEXTMODE
#include "../SDK/SDK.h"

MAKE_SIGNATURE(CDynamicProp_TestCollision, "client.dll", "4C 8B DC 49 89 6B ? 57 41 56", 0x0);

MAKE_HOOK(CDynamicProp_TestCollision, S::CDynamicProp_TestCollision(), bool,
    void* rcx, void* ray, unsigned int fContentsMask, void* trace)
{
    return false;
}
#endif