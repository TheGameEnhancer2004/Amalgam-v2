#pragma once
#include "../../../../../SDK/SDK.h"

class CHaarpController
{
public:
	bool GetCapturePos(Vector& vOut);
	bool GetDefensePos(Vector& vOut);

	void Update();

	std::wstring m_sHaarpStatus = L"";

private:
	Vector m_vCachedCapturePos = {};
	bool m_bHasCachedCapturePos = false;
	Vector m_vCachedBluCapturePos = {};
	bool m_bHasCachedBluCapturePos = false;
};

ADD_FEATURE(CHaarpController, HaarpController);
