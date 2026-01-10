#pragma once
#include "../../../../../SDK/SDK.h"

class CDoomsdayController
{
public:
	CCaptureFlag* GetFlag();
	bool GetCapturePos(Vector& vOut);
	bool GetGoal(Vector& vOut);

	void Update();

	std::wstring m_sDoomsdayStatus = L"";

private:
	Vector m_vCachedCapturePos = {};
	bool m_bHasCachedCapturePos = false;
};

ADD_FEATURE(CDoomsdayController, DoomsdayController);
