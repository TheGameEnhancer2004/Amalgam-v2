#pragma once
#include "../NavBotCore.h"
#include <optional>

class CNavBotCapture
{
private:
	std::optional<Vector> m_vCurrentCaptureSpot;
	std::optional<Vector> m_vCurrentCaptureCenter;
	std::optional<int> m_iCurrentCapturePointIdx;
	std::optional<Vector> m_vLastClaimedCaptureSpot;
	Timer m_tCaptureClaimRefresh{};

	// Overwrite to return true for payload carts as an example
	bool m_bOverwriteCapture = false;
private:
	bool ShouldAssist(CTFPlayer* pLocal, int iTargetIdx);
	bool GetCtfGoal(CTFPlayer* pLocal, int iOurTeam, int iEnemyTeam, Vector& vOut);
	bool GetDoomsdayGoal(CTFPlayer* pLocal, int iOurTeam, int iEnemyTeam, Vector& vOut);

	void ClaimCaptureSpot(const Vector& vSpot, int iPointIdx);
	void ReleaseCaptureSpotClaim();
public:
	bool GetPayloadGoal(const Vector vLocalOrigin, int iOurTeam, Vector& vOut);
	bool GetControlPointGoal(const Vector vLocalOrigin, int iOurTeam, Vector& vOut);
	bool Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);
	void Reset();
};

ADD_FEATURE(CNavBotCapture, NavBotCapture);
