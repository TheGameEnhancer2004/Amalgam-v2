#pragma once
#include "../NavBotCore.h"
#include <optional>

class CNavBotCapture
{
private:
	Timer m_tCaptureClaimRefresh{};
	std::optional<int> m_iCurrentCapturePointIdx;
	std::optional<Vector> m_vCurrentCaptureSpot;
	std::optional<Vector> m_vCurrentCaptureCenter;
	std::optional<Vector> m_vLastClaimedCaptureSpot;

	struct PayloadCache_t
	{
		Vector vPos = {};
		float flTime = 0.0f;
	};
	std::array<PayloadCache_t, 2> m_aPayloadCache;

public:
	// Overwrite to return true for payload carts as an example
	bool m_bOverwriteCapture = false;

	std::wstring m_sCaptureStatus = L"";

private:
	bool ShouldAvoidPlayer(int iIndex);
	void ClaimCaptureSpot(const Vector& vSpot, int iPointIdx);
	void ReleaseCaptureSpotClaim();

public:
	bool GetPayloadGoal(const Vector vLocalOrigin, int iOurTeam, Vector& vOut);
	bool GetControlPointGoal(const Vector vLocalOrigin, int iOurTeam, Vector& vOut);
	bool GetCtfGoal(CTFPlayer* pLocal, int iOurTeam, int iEnemyTeam, Vector& vOut);
	bool GetDoomsdayGoal(CTFPlayer* pLocal, int iOurTeam, int iEnemyTeam, Vector& vOut);
	bool Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);
	void Reset();
};

ADD_FEATURE(CNavBotCapture, NavBotCapture);
