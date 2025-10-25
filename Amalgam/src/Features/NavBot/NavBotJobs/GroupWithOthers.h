#pragma once
#include "../NavBotCore.h"

class CNavBotGroup
{
private:
	bool GetFormationOffset(CTFPlayer* pLocal, int iPositionIndex, Vector& vOut);

	std::vector<std::pair<uint32_t, Vector>> m_vLocalBotPositions;
	Timer m_tUpdateFormationTimer;
	int m_iPositionInFormation = -1;
	float m_flFormationDistance = 120.0f;
public:
	void UpdateLocalBotPositions(CTFPlayer* pLocal);
	bool Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);
};

ADD_FEATURE(CNavBotGroup, NavBotGroup);