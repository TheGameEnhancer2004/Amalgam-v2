#include "Commands.h"

#include "../../Core/Core.h"
#include "../ImGui/Menu/Menu.h"
#include "../NavBot/NavEngine/NavEngine.h"
#include <utility>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/join.hpp>

bool CCommands::Run(const std::string& cmd, std::deque<std::string>& args)
{
	auto uHash = FNV1A::Hash32(cmd.c_str());
	if (!CommandMap.contains(uHash))
		return false;

	CommandMap[uHash](args);
	return true;
}

void CCommands::Register(const std::string& name, CommandCallback callback)
{
	CommandMap[FNV1A::Hash32(name.c_str())] = std::move(callback);
}

void CCommands::Initialize()
{
	Register("queue", [](const std::deque<std::string>& args)
		{
			static bool bHasLoaded = false;
			if (!bHasLoaded)
			{
				I::TFPartyClient->LoadSavedCasualCriteria();
				bHasLoaded = true;
			}
			I::TFPartyClient->RequestQueueForMatch(k_eTFMatchGroup_Casual_Default);
		});

	Register("setcvar", [](const std::deque<std::string>& args)
		{
			if (args.size() < 2)
			{
				SDK::Output("Usage:\n\tsetcvar <cvar> <value>");
				return;
			}

			const auto foundCVar = I::CVar->FindVar(args[0].c_str());
			const std::string cvarName = args[0];
			if (!foundCVar)
			{
				SDK::Output(std::format("Could not find {}", cvarName).c_str());
				return;
			}

			auto vArgs = args; vArgs.pop_front();
			std::string newValue = boost::algorithm::join(vArgs, " ");
			boost::replace_all(newValue, "\"", "");
			foundCVar->SetValue(newValue.c_str());
			SDK::Output(std::format("Set {} to {}", cvarName, newValue).c_str());
		});

	Register("getcvar", [](const std::deque<std::string>& args)
		{
			if (args.size() != 1)
			{
				SDK::Output("Usage:\n\tgetcvar <cvar>");
				return;
			}

			const auto foundCVar = I::CVar->FindVar(args[0].c_str());
			const std::string cvarName = args[0];
			if (!foundCVar)
			{
				SDK::Output(std::format("Could not find {}", cvarName).c_str());
				return;
			}

			SDK::Output(std::format("Value of {} is {}", cvarName, foundCVar->GetString()).c_str());
		});

	Register("menu", [](const std::deque<std::string>& args)
		{
			I::MatSystemSurface->SetCursorAlwaysVisible(F::Menu.m_bIsOpen = !F::Menu.m_bIsOpen);
		});

	Register("path_to", [](std::deque<std::string> args)
		{
			// Check if the user provided at least 3 args
			if (args.size() < 3)
			{
				I::CVar->ConsoleColorPrintf({ 255, 255, 255, 255 }, "Usage: path_to <x> <y> <z>\n");
				return;
			}

			// Get the Vec3
			const auto Vec = Vec3( atoi( args[ 0 ].c_str( ) ), atoi( args[ 1 ].c_str( ) ), atoi( args[ 2 ].c_str( ) ) );

			F::NavEngine.navTo( Vec );
		});

	Register("nav_search_spawnrooms", [](std::deque<std::string> args)
		{
			if ( F::NavEngine.map && F::NavEngine.map->state == CNavParser::NavState::Active )
				F::NavEngine.map->UpdateRespawnRooms( );
		});

	Register("save_nav_mesh", [](std::deque<std::string> args)
		{
			if ( auto pNavFile = F::NavEngine.getNavFile( ) )
				pNavFile->Write( );
		});

	Register("unload", [](const std::deque<std::string>& args)
		{
			if (F::Menu.m_bIsOpen)
				I::MatSystemSurface->SetCursorAlwaysVisible(F::Menu.m_bIsOpen = false);
			U::Core.m_bUnload = true;
		});
}