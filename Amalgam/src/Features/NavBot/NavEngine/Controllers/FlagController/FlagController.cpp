#include "FlagController.h"

FlagInfo CFlagController::GetFlag(int iTeam)
{
	for (auto tFlag : m_vFlags)
	{
		if (!tFlag.m_pFlag)
			continue;

		if (tFlag.m_iTeam == iTeam)
			return tFlag;
	}

	return {};
}

Vector CFlagController::GetPosition(CCaptureFlag* pFlag)
{
	return pFlag->GetAbsOrigin();
}

bool CFlagController::GetPosition(int iTeam, Vector& vOut)
{
	auto tFlag = GetFlag(iTeam);
	if (tFlag.m_pFlag)
	{
		vOut = GetPosition(tFlag.m_pFlag);
		return true;
	}

	return false;
}

bool CFlagController::GetSpawnPosition(int iTeam, Vector& vOut)
{
	auto tFlag = GetFlag(iTeam);
	if (tFlag.m_pFlag && m_mSpawnPositions.contains(tFlag.m_pFlag->entindex()))
	{
		vOut = m_mSpawnPositions[tFlag.m_pFlag->entindex()];
		return true;
	}

	return false;
}

int CFlagController::GetCarrier(CCaptureFlag* pFlag)
{
	if (!pFlag)
		return -1;

	auto pOwnerEnt = pFlag->m_hOwnerEntity().Get();
	if (!pOwnerEnt)
		return -1;

	auto pPlayer = pOwnerEnt->As<CTFPlayer>();
	if (pPlayer->IsDormant() || !pPlayer->IsPlayer() || !pPlayer->IsAlive())
		return -1;

	return pPlayer->entindex();
}

int CFlagController::GetCarrier(int iTeam)
{
	auto tFlag = GetFlag(iTeam);
	if (tFlag.m_pFlag)
		return GetCarrier(tFlag.m_pFlag);

	return -1;
}

int CFlagController::GetStatus(CCaptureFlag* pFlag)
{
	return pFlag->m_nFlagStatus();
}

int CFlagController::GetStatus(int iTeam)
{
	auto tFlag = GetFlag(iTeam);
	if (tFlag.m_pFlag)
		return GetStatus(tFlag.m_pFlag);

	// Mark as home if nothing is found
	return TF_FLAGINFO_HOME;
}

void CFlagController::Init()
{
	// Reset everything
	m_vFlags.clear();
	m_mSpawnPositions.clear();
}

void CFlagController::Update()
{
	m_vFlags.clear();

	// Find flags and get info
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldObjective))
	{
		if (pEntity->GetClassID() != ETFClassID::CCaptureFlag)
			continue;

		auto pFlag = pEntity->As<CCaptureFlag>();
		// Cannot use dormant flag, but it is still potentially valid
		if (pFlag->IsDormant())
			continue;

		// Only CTF support for now (and sd_doomsday)
		if (pFlag->m_nType() != TF_FLAGTYPE_CTF && pFlag->m_nType() != TF_FLAGTYPE_RESOURCE_CONTROL)
			continue;

		FlagInfo tFlag{};
		tFlag.m_pFlag = pFlag;
		tFlag.m_iTeam = pFlag->m_iTeamNum();

		if (pFlag->m_nFlagStatus() == TF_FLAGINFO_HOME)
			m_mSpawnPositions[pFlag->entindex()] = pFlag->GetAbsOrigin();

		m_vFlags.push_back(tFlag);
	}
}