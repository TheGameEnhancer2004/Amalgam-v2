#include "PLController.h"

void CPLController::Init()
{
	// Reset entries
	for (auto& vEntities : m_aPayloads)
		vEntities.clear();
}

void CPLController::Update()
{
	// We should update the payload list
	{
		// Reset entries
		for (auto& vEntities : m_aPayloads)
			vEntities.clear();

		for (auto pPayload : H::Entities.GetGroup(EntityEnum::WorldObjective))
		{
			if (pPayload->GetClassID() != ETFClassID::CObjectCartDispenser)
				continue;

			int iTeam = pPayload->m_iTeamNum();

			// Not the object we need
			if (iTeam < TF_TEAM_RED || iTeam > TF_TEAM_BLUE)
				continue;

			// Add new entry for the team
			m_aPayloads.at(iTeam - TF_TEAM_RED).push_back(pPayload);
		}
	}
}

bool CPLController::GetClosestPayload(Vector vPos, int iTeam, Vector& vOut)
{
	// Invalid team
	if (iTeam < TF_TEAM_RED || iTeam > TF_TEAM_BLUE)
		return false;

	float flBestDist = FLT_MAX;
	Vector vBestPos;
	// Find best payload
	for (auto pEntity : m_aPayloads[iTeam - TF_TEAM_RED])
	{
		if (pEntity->GetClassID() != ETFClassID::CObjectCartDispenser || pEntity->IsDormant())
			continue;

		const auto vOrigin = pEntity->GetAbsOrigin();
		const auto flDist = vOrigin.DistToSqr(vPos);
		if (flDist < flBestDist)
		{
			vBestPos = vOrigin;
			flBestDist = flDist;
		}
	}
	vOut = vBestPos;
	return flBestDist != FLT_MAX;
}