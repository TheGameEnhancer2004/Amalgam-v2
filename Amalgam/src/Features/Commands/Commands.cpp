#include "Commands.h"

#include "../../Core/Core.h"
#include "../ImGui/Menu/Menu.h"
#include "../NavBot/NavEngine/NavEngine.h"
#include "../Configs/Configs.h"
#include "../Players/PlayerUtils.h"
#include "../Misc/AutoItem/AutoItem.h"
#include "../Misc/Misc.h"
#include <utility>
#include <boost/algorithm/string/replace.hpp>

static std::unordered_map<uint32_t, CommandCallback> s_mCommands = {
	{
		FNV1A::Hash32Const("cat_setcvar"), 
		[](const std::deque<const char*>& vArgs)
		{
			if (vArgs.size() < 2)
			{
				SDK::Output("Usage:\n\tcat_setcvar <cvar> <value>");
				return;
			}

			const char* sCVar = vArgs[0];
			auto pCVar = I::CVar->FindVar(sCVar);
			if (!pCVar)
			{
				SDK::Output(std::format("Could not find {}", sCVar).c_str());
				return;
			}

			std::string sValue = "";
			for (int i = 1; i < vArgs.size(); i++)
				sValue += std::format("{} ", vArgs[i]);
			sValue.pop_back();
			boost::replace_all(sValue, "\"", "");

			pCVar->SetValue(sValue.c_str());
			SDK::Output(std::format("Set {} to {}", sCVar, sValue).c_str());
		}
	},
	{
		FNV1A::Hash32Const("cat_getcvar"), 
		[](const std::deque<const char*>& vArgs)
		{
			if (vArgs.size() != 1)
			{
				SDK::Output("Usage:\n\tcat_getcvar <cvar>");
				return;
			}

			const char* sCVar = vArgs[0];
			auto pCVar = I::CVar->FindVar(sCVar);
			if (!pCVar)
			{
				SDK::Output(std::format("Could not find {}", sCVar).c_str());
				return;
			}

			SDK::Output(std::format("Value of {} is {}", sCVar, pCVar->GetString()).c_str());
		}
	},
	{
		FNV1A::Hash32Const("cat_queue"), 
		[](const std::deque<const char*>& vArgs)
		{
			if (!I::TFPartyClient)
				return;
			if (I::TFPartyClient->BInQueueForMatchGroup(k_eTFMatchGroup_Casual_Default))
				return;
			if (I::EngineClient->IsDrawingLoadingImage())
				return;

			static bool bHasLoaded = false;
			if (!bHasLoaded)
			{
				I::TFPartyClient->LoadSavedCasualCriteria();
				bHasLoaded = true;
			}
			I::TFPartyClient->RequestQueueForMatch(k_eTFMatchGroup_Casual_Default);
		}
	},
	{
		FNV1A::Hash32Const("cat_criteria"),
		[](const std::deque<const char*>& vArgs)
		{
			if (!I::TFPartyClient)
			{
				SDK::Output("TFPartyClient interface unavailable");
				return;
			}

			I::TFPartyClient->LoadSavedCasualCriteria();
			SDK::Output("Loaded saved casual criteria.");
		}
	},
	{
		FNV1A::Hash32Const("cat_abandon"),
		[](const std::deque<const char*>& vArgs)
		{
			if (!I::TFGCClientSystem)
			{
				SDK::Output("TFGCClientSystem interface unavailable");
				return;
			}

			I::TFGCClientSystem->AbandonCurrentMatch();
			SDK::Output("Requested match abandon.");
		}
	},
	{
		FNV1A::Hash32Const("cat_load"), 
		[](const std::deque<const char*>& vArgs)
		{
			if (vArgs.size() != 1)
			{
				SDK::Output("Usage:\n\tcat_load <config_name>");
				return;
			}

			F::Configs.LoadConfig(vArgs[0], true);
		}
	},
	{
		FNV1A::Hash32Const("cat_path_to"), 
		[](const std::deque<const char*>& vArgs)
		{
			// Check if the user provided at least 3 args
			if (vArgs.size() < 3)
			{
				SDK::Output("Usage:\n\tcat_path_to <x> <y> <z>");
				return;
			}

			Vector vDest;
			try
			{	
				// Get the Vec3
				vDest = Vec3(atoi(vArgs[0]), atoi(vArgs[1]), atoi(vArgs[2]));
			}
			catch (...)
			{
				SDK::Output("Usage:\n\tcat_path_to <x> <y> <z>");
				return;
			}

			auto pLocal = H::Entities.GetLocal();
			if (!pLocal || !pLocal->IsAlive())
			{
				SDK::Output("cat_path_to", "Local player unavailable");
				return;
			}

			F::NavEngine.GetLocalNavArea(pLocal->GetAbsOrigin());
			F::NavEngine.NavTo(vDest);
		}
	},
	{
		FNV1A::Hash32Const("cat_save_nav_mesh"), 
		[](const std::deque<const char*>& vArgs)
		{
			if (auto pNavFile = F::NavEngine.GetNavFile())
				pNavFile->Write();
		}
	},
	{
		FNV1A::Hash32Const("cat_clearchat"), 
		[](const std::deque<const char*>& vArgs)
		{
			I::ClientModeShared->m_pChatElement->SetText("");
		}
	},
	{
		FNV1A::Hash32Const("cat_menu"), 
		[](const std::deque<const char*>& vArgs)
		{
			I::MatSystemSurface->SetCursorAlwaysVisible(F::Menu.m_bIsOpen = !F::Menu.m_bIsOpen);
		}
	},
	{
		FNV1A::Hash32Const("cat_unload"), 
		[](const std::deque<const char*>& vArgs)
		{
			if (F::Menu.m_bIsOpen)
				I::MatSystemSurface->SetCursorAlwaysVisible(F::Menu.m_bIsOpen = false);
			U::Core.m_bUnload = G::Unload = true;
		}
	},
	{
		FNV1A::Hash32Const("cat_ignore"), 
		[](const std::deque<const char*>& vArgs)
		{
			if (vArgs.size() < 2)
			{
				SDK::Output("Usage:\n\tcat_ignore <id32> <tag>");
				return;
			}

			uint32_t uFriendsID = 0;
			try
			{
				uFriendsID = std::stoul(vArgs[0]);
			}
			catch (...)
			{
				SDK::Output("Invalid ID32 format");
				return;
			}

			if (!uFriendsID)
			{
				SDK::Output("Invalid ID32");
				return;
			}

			const char* sTag = vArgs[1];
			int iTagID = F::PlayerUtils.GetTag(sTag);
			if (iTagID == -1)
			{
				SDK::Output(std::format("Invalid tag: {}", sTag).c_str());
				return;
			}

			auto pTag = F::PlayerUtils.GetTag(iTagID);
			if (!pTag || !pTag->m_bAssignable)
			{
				SDK::Output(std::format("Tag {} is not assignable", sTag).c_str());
				return;
			}

			if (F::PlayerUtils.HasTag(uFriendsID, iTagID))
			{
				SDK::Output(std::format("ID32 {} already has tag {}", uFriendsID, sTag).c_str());
				return;
			}

			F::PlayerUtils.AddTag(uFriendsID, iTagID, true);
			SDK::Output(std::format("Added tag {} to ID32 {}", sTag, uFriendsID).c_str());
		}
	},
	{
		FNV1A::Hash32Const("cat_dumpnames"),
		[](const std::deque<const char*>& vArgs)
		{
			F::Misc.DumpNames();
		}
	},
	{
		FNV1A::Hash32Const("cat_crash"), 
		[](const std::deque<const char*>& vArgs)
		{	// if you want to time out of a server and rejoin
			switch (vArgs.empty() ? 0 : FNV1A::Hash32(vArgs.front()))
			{
			case FNV1A::Hash32Const("true"):
			case FNV1A::Hash32Const("t"):
			case FNV1A::Hash32Const("1"):
				break;
			default:
				Vars::Debug::CrashLogging.Value = false; // we are voluntarily crashing, don't give out log if we don't want one
			}
			reinterpret_cast<void(*)()>(0)();
		}
	},
	{
		FNV1A::Hash32Const("cat_rent_item"), 
		[](const std::deque<const char*>& vArgs)
		{	
			if (vArgs.size() != 1)
			{
				SDK::Output("Usage:\n\tcat_rent_item <item_def_index>");
				return;
			}

			item_definition_index_t iDefIdx;
			try
			{
				iDefIdx = atoi(vArgs[0]);
			}
			catch (const std::invalid_argument&)
			{
				SDK::Output("Invalid item_def_index");
				return;
			}

			F::AutoItem.Rent(iDefIdx);
		}
	},
	{
		FNV1A::Hash32Const("cat_achievement_unlock"), 
		[](const std::deque<const char*>& vArgs)
		{
			F::Misc.UnlockAchievements();
		}
	},
};

bool CCommands::Run(const char* sCmd, std::deque<const char*>& vArgs)
{
	std::string sLower = sCmd;
	std::transform(sLower.begin(), sLower.end(), sLower.begin(), ::tolower);

	auto uHash = FNV1A::Hash32(sLower.c_str());
	if (!s_mCommands.contains(uHash))
		return false;

	s_mCommands[uHash](vArgs);
	return true;
}