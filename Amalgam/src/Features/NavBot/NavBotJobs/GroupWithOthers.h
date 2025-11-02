#pragma once
#include "../NavBotCore.h"

class CNavBotGroup
{
private:
	bool GetFormationOffset(CTFPlayer* pLocal, int iPositionIndex, Vector& vOut);

	int m_iPositionInFormation = -1;
	float m_flFormationDistance = 120.0f;
	Timer m_tUpdateFormationTimer;
	std::vector<std::pair<uint32_t, Vector>> m_vLocalBotPositions;
public:
	void UpdateLocalBotPositions(CTFPlayer* pLocal);
	bool Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);
};

ADD_FEATURE(CNavBotGroup, NavBotGroup);