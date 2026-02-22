#include "../SDK/SDK.h"

#include "../Features/Spectate/Spectate.h"

MAKE_SIGNATURE(CBasePlayer_CalcView, "client.dll", "40 57 48 83 EC ? 44 8B 91", 0x0);

MAKE_HOOK(CBasePlayer_CalcView, S::CBasePlayer_CalcView(), void,
	void* rcx, Vector& eyeOrigin, QAngle& eyeAngles, float& zNear, float& zFar, float& fov)
{
	DEBUG_RETURN(CBasePlayer_CalcView, rcx, eyeOrigin, eyeAngles, zNear, zFar, fov);

	if (!Vars::Visuals::Removals::ViewPunch.Value && !F::Spectate.HasTarget())
		return CALL_ORIGINAL(rcx, eyeOrigin, eyeAngles, zNear, zFar, fov);

	auto pPlayer = reinterpret_cast<CBasePlayer*>(rcx);

	Vec3 vOldPunch = pPlayer->m_vecPunchAngle();
	pPlayer->m_vecPunchAngle() = {};
	CALL_ORIGINAL(rcx, eyeOrigin, eyeAngles, zNear, zFar, fov);
	pPlayer->m_vecPunchAngle() = vOldPunch;
}