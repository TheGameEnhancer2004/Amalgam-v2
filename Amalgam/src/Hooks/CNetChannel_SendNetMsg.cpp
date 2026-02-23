#include "../SDK/SDK.h"

#include "../Features/Ticks/Ticks.h"
#include "../Features/Backtrack/Backtrack.h"
#include "../Features/Misc/Misc.h"

MAKE_SIGNATURE(CNetChannel_SendNetMsg, "engine.dll", "48 89 5C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 41 56 48 83 EC ? 48 8B F1 45 0F B6 F1", 0x0);

MAKE_HOOK(CNetChannel_SendNetMsg, S::CNetChannel_SendNetMsg(), bool,
	CNetChannel* pNetChan, INetMessage& msg, bool bForceReliable, bool bVoice)
{
	DEBUG_RETURN(CNetChannel_SendNetMsg, pNetChan, msg, bForceReliable, bVoice);
	
	if (G::Unload)
		return CALL_ORIGINAL(pNetChan, msg, bForceReliable, bVoice);

	switch (msg.GetType())
	{
	case net_SetConVar:
	{
		auto pMsg = reinterpret_cast<NET_SetConVar*>(&msg);
		for (int i = 0; i < pMsg->m_ConVars.Count(); i++)
		{
			NET_SetConVar::CVar_t* localCvar = &pMsg->m_ConVars[i];

			// intercept and change any vars we want to control
			switch (FNV1A::Hash32(localCvar->Name))
			{
			case FNV1A::Hash32Const("cl_interp"):
				if (F::Backtrack.m_flSentInterp != -1.f)
					strncpy_s(localCvar->Value, std::to_string(F::Backtrack.m_flSentInterp).c_str(), MAX_OSPATH);
				if (Vars::Misc::Game::AntiCheatCompatibility.Value)
				{ try {
					float flValue = std::stof(localCvar->Value);
					strncpy_s(localCvar->Value, std::to_string(std::min(flValue, 0.1f)).c_str(), MAX_OSPATH);
				} catch (...) {}; }
				break;
			case FNV1A::Hash32Const("cl_cmdrate"):
				if (F::Misc.m_iWishCmdrate != -1)
					strncpy_s(localCvar->Value, std::to_string(F::Misc.m_iWishCmdrate).c_str(), MAX_OSPATH);
				if (Vars::Misc::Game::AntiCheatCompatibility.Value)
				{ try {
					int iValue = std::stof(localCvar->Value);
					strncpy_s(localCvar->Value, std::to_string(std::max(iValue, 10)).c_str(), MAX_OSPATH);
				} catch (...) {}; }
				break;
			/*
			case FNV1A::Hash32Const("cl_updaterate"):
				if (F::Misc.m_iWishUpdaterate != -1)
					strncpy_s(localCvar->Value, std::to_string(F::Misc.m_iWishUpdaterate).c_str(), MAX_OSPATH);
				break;
			*/
			case FNV1A::Hash32Const("cl_interp_ratio"):
			case FNV1A::Hash32Const("cl_interpolate"):
				strncpy_s(localCvar->Value, "1", MAX_OSPATH);
			}
		}
		break;
	}
	case clc_VoiceData:
		// stop lag with voice chat
		bVoice = true;
		break;
	case clc_RespondCvarValue:
		if (Vars::Misc::Game::AntiCheatCompatibility.Value)
		{
			auto pMsg = reinterpret_cast<uintptr_t*>(&msg);
			if (!pMsg) break;

			auto cvarName = reinterpret_cast<const char*>(pMsg[6]);
			if (!cvarName) break;

			auto pConVar = H::ConVars.FindVar(cvarName);
			if (!pConVar) break;

			static std::string sValue = "";
			switch (FNV1A::Hash32(cvarName))
			{
			case FNV1A::Hash32Const("cl_interp"):
				if (F::Backtrack.m_flSentInterp != -1.f)
					sValue = std::to_string(std::min(F::Backtrack.m_flSentInterp, 0.1f));
				else
					sValue = pConVar->GetString();
				break;
			case FNV1A::Hash32Const("cl_interp_ratio"):
				sValue = "1";
				break;
			case FNV1A::Hash32Const("cl_cmdrate"):
				if (F::Misc.m_iWishCmdrate != -1)
					sValue = std::to_string(F::Misc.m_iWishCmdrate);
				else
					sValue = pConVar->GetString();
				break;
			/*
			case FNV1A::Hash32Const("cl_updaterate"):
				if (F::Misc.m_iWishUpdaterate != -1)
					sValue = std::to_string(F::Misc.m_iWishUpdaterate);
				else
					sValue = pConVar->GetString();
				break;
			*/
			case FNV1A::Hash32Const("mat_dxlevel"):
				sValue = pConVar->GetString();
				break;
			default:
				sValue = pConVar->m_pParent->m_pszDefaultValue;
			}
			pMsg[7] = uintptr_t(sValue.c_str());
			if (Vars::Debug::Logging.Value)
				SDK::Output("Convar spoof", msg.ToString(), Vars::Menu::Theme::Accent.Value, OUTPUT_CONSOLE | OUTPUT_DEBUG);
		}
		break;
	}

	return CALL_ORIGINAL(pNetChan, msg, bForceReliable, bVoice);
}