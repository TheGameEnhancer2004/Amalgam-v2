#include "Controller.h"
#include "CPController/CPController.h"
#include "FlagController/FlagController.h"
#include "PLController/PLController.h"

ETFGameType GetGameType()
{
	// Check if we're on doomsday
	auto sMapName = std::string(I::EngineClient->GetLevelName());
	F::GameObjectiveController.m_bDoomsday = sMapName.find("sd_doomsday") != std::string::npos;

	int iType = TF_GAMETYPE_UNDEFINED;
	if (auto pGameRules = I::TFGameRules())
		iType = pGameRules->m_nGameType();

	return static_cast<ETFGameType>(iType);
}

void CGameObjectiveController::Update()
{
	static float flNextGameTypeRefresh = 0.0f;
	if (m_eGameMode == TF_GAMETYPE_UNDEFINED || I::GlobalVars->curtime >= flNextGameTypeRefresh)
	{
		m_eGameMode = GetGameType();
		flNextGameTypeRefresh = I::GlobalVars->curtime + 1.0f;
	}

	auto sMapName = std::string(I::EngineClient->GetLevelName());
	size_t nLastSlash = sMapName.find_last_of("/\\");
	if (nLastSlash != std::string::npos)
		sMapName = sMapName.substr(nLastSlash + 1);

	if (sMapName.find("cppl_") == 0)
	{
		F::CPController.Update();
		F::PLController.Update();
		return;
	}
	if (sMapName.find("vsh_") == 0 || sMapName.find("2koth_") == 0 || sMapName.find("koth_") == 0 || sMapName.find("cp_") == 0 || sMapName.find("tc_") == 0)
	{
		F::CPController.Update();
		return;
	}
	if (sMapName.find("pl_") == 0 || sMapName.find("plr_") == 0)
	{
		F::PLController.Update();
		return;
	}
	if (sMapName.find("ctf_") == 0 || sMapName.find("sd_") == 0 || sMapName.find("rd_") == 0 || sMapName.find("pd_") == 0)
	{
		F::FlagController.Update();
		if (sMapName.find("sd_doomsday") == 0)
			F::CPController.Update();
		return;
	}

	switch (m_eGameMode)
	{
	case TF_GAMETYPE_CTF:
		F::FlagController.Update();
		break;
	case TF_GAMETYPE_CP:
		F::CPController.Update();
		break;
	case TF_GAMETYPE_ESCORT:
		F::PLController.Update();
		break;
	default:
		if (m_bDoomsday)
		{
			// This isnt even the right way to do this. But who plays that map anyway??
			F::FlagController.Update();
			F::CPController.Update();
		}
		break;
	}
}

void CGameObjectiveController::Reset()
{
	m_eGameMode = TF_GAMETYPE_UNDEFINED;
	F::FlagController.Init();
	F::PLController.Init();
	F::CPController.Init();
}