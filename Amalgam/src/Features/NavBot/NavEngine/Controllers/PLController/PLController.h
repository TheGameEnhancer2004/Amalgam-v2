#pragma once
#include "../../../../../SDK/SDK.h"
#include <optional>

class CPLController
{
private:
	// Valid_ents that controls all the payloads for each team. Red team is first, then comes blue team.
	std::array<std::vector<CBaseEntity*>, 2> m_aPayloads;

public:
	// Get the closest Control Payload
	bool GetClosestPayload(Vector vPos, int iTeam, Vector& vOut);

	void Init();
	void Update();
};

ADD_FEATURE(CPLController, PLController);