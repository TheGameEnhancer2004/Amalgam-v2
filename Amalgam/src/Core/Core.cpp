#include "Core.h"

#include "../SDK/SDK.h"
#include "../BytePatches/BytePatches.h"
#include "../Features/Configs/Configs.h"
#include "../Features/ImGui/Menu/Menu.h"
#include "../Features/EnginePrediction/EnginePrediction.h"
#include "../Features/Visuals/Materials/Materials.h"
#include "../Features/Visuals/Visuals.h"
#include "../SDK/Events/Events.h"
#ifdef TEXTMODE
#include "../Features/Misc/NamedPipe/NamedPipe.h"
#endif
#include "../Utils/Hash/FNV1A.h"

#include <Psapi.h>
#include <TlHelp32.h>

static inline std::string GetProcessName(DWORD dwProcessID)
{
	HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, dwProcessID);
	if (!hProcess)
		return "";

	if (char buffer[MAX_PATH]; GetModuleBaseName(hProcess, nullptr, buffer, sizeof(buffer) / sizeof(char)))
	{
		CloseHandle(hProcess);
		return buffer;
	}

	CloseHandle(hProcess);
	return "";
}

static inline bool CheckDXLevel()
{
	auto mat_dxlevel = U::ConVars.FindVar("mat_dxlevel");
	if (mat_dxlevel->GetInt() < 90)
	{
		//const char* sMessage = "You are running with graphics options that Amalgam does not support. -dxlevel must be at least 90.";
		const char* sMessage = "You are running with graphics options that Amalgam does not support. It is recommended for -dxlevel to be at least 90.";
		U::Core.AppendFailText(sMessage);
		F::Menu.ShowDeferredNotification("Graphics Warning", sMessage);
		SDK::Output("Amalgam", sMessage, { 175, 150, 255 }, OUTPUT_CONSOLE | OUTPUT_DEBUG | OUTPUT_TOAST | OUTPUT_MENU);
		//return false;
	}

	return true;
}

void CCore::AppendFailText(const char* sMessage)
{
	m_ssFailStream << std::format("{}\n", sMessage);
	OutputDebugStringA(std::format("{}\n", sMessage).c_str());
}

void CCore::LogFailText()
{
	m_ssFailStream << "\nBuilt @ " __DATE__ ", " __TIME__ ", " __CONFIGURATION__ "\n";
	m_ssFailStream << "Ctrl + C to copy. \n";
	try
	{
		std::ofstream file;
		file.open(F::Configs.m_sConfigPath + "fail_log.txt", std::ios_base::app);
		file << m_ssFailStream.str() + "\n\n\n";
		file.close();
		m_ssFailStream << "Logged to Amalgam\\fail_log.txt. ";
	}
	catch (...) {}
#ifndef TEXTMODE
	SDK::Output("Failed to load", m_ssFailStream.str().c_str(), {}, OUTPUT_DEBUG, MB_OK | MB_ICONERROR);
#endif
}

static bool ModulesLoaded()
{
#ifndef TEXTMODE
	if (!SDK::GetTeamFortressWindow())
		return false;
#endif

	if (GetModuleHandleA("client.dll"))
	{
		// Check I::ClientModeShared and I::UniformRandomStream here so that we wait until they get initialized
		auto dwDest = U::Memory.FindSignature("client.dll", "48 8B 0D ? ? ? ? 48 8B 10 48 8B 19 48 8B C8 FF 92");
		if (!dwDest || !*reinterpret_cast<void**>(U::Memory.RelToAbs(dwDest)))
			return false;

		dwDest = U::Memory.FindSignature("client.dll", "48 8B 0D ? ? ? ? F3 0F 59 CA 44 8D 42");
		if (!dwDest || !*reinterpret_cast<void**>(U::Memory.RelToAbs(dwDest)))
			return false;
	}
	else 
		return false;

	return GetModuleHandleA("engine.dll") &&
		GetModuleHandleA("server.dll") &&
		GetModuleHandleA("tier0.dll") &&
		GetModuleHandleA("vstdlib.dll") &&
		GetModuleHandleA("vgui2.dll") &&
		GetModuleHandleA("vguimatsurface.dll") &&
		GetModuleHandleA("materialsystem.dll") &&
		GetModuleHandleA("inputsystem.dll") &&
		GetModuleHandleA("vphysics.dll") &&
		GetModuleHandleA("steamclient64.dll") &&
		(GetModuleHandleA("shaderapidx9.dll") || GetModuleHandleA("shaderapivk.dll"));
}

void CCore::Load()
{
	if (m_bUnload = m_bFailed = FNV1A::Hash32(GetProcessName(GetCurrentProcessId()).c_str()) != FNV1A::Hash32Const("tf_win64.exe"))
	{
		AppendFailText("Invalid process");
		return;
	}

	float flTime = 0.f;
	while (!ModulesLoaded())
	{
		Sleep(500), flTime += 0.5f;
		if (m_bUnload = m_bFailed = flTime >= 60.f)
		{
			AppendFailText("Failed to load");
			return;
		}
		if (m_bUnload = m_bFailed = U::KeyHandler.Down(VK_F11, true))
		{
			AppendFailText("Cancelled load");
			return;
		}
	}

	if (m_bUnload = m_bFailed = !U::Signatures.Initialize() || !U::Interfaces.Initialize() || !CheckDXLevel())
		return;

	if (m_bUnload = m_bFailed2 = !U::Hooks.Initialize() || !U::BytePatches.Initialize() || !H::Events.Initialize())
		return;

#ifndef TEXTMODE
	F::Materials.LoadMaterials();
#endif
	U::ConVars.Unlock();
#ifdef TEXTMODE
	F::NamedPipe.Initialize();
#endif

	F::Configs.LoadConfig(F::Configs.m_sCurrentConfig, false);
	I::EngineClient->ClientCmd_Unrestricted("exec catexec");
	SDK::Output("Amalgam", "Loaded", { 175, 150, 255 }, OUTPUT_CONSOLE | OUTPUT_DEBUG | OUTPUT_TOAST | OUTPUT_MENU);
}

void CCore::Loop()
{
	while (true)
	{
#ifdef TEXTMODE
		if (m_bUnload)
			break;
#else
		bool bShouldUnload = U::KeyHandler.Down(VK_F11) && SDK::IsGameWindowInFocus() || m_bUnload;
		if (bShouldUnload)
			break;
#endif 
		Sleep(15);
	}
}

void CCore::Unload()
{
	if (m_bFailed)
	{
		LogFailText();
		return;
	}

	G::Unload = true;
	m_bFailed2 = !U::Hooks.Unload() || m_bFailed2;
	U::BytePatches.Unload();
	H::Events.Unload();

	if (F::Menu.m_bIsOpen)
		I::MatSystemSurface->SetCursorAlwaysVisible(false);
	F::Visuals.RestoreWorldModulation();
	if (I::Input->CAM_IsThirdPerson())
	{
		if (auto pLocal = H::Entities.GetLocal())
		{
			I::Input->CAM_ToFirstPerson();
			pLocal->ThirdPersonSwitch();
		}
	}
	U::ConVars.FindVar("cl_wpn_sway_interp")->SetValue(0.f);
	U::ConVars.FindVar("cl_wpn_sway_scale")->SetValue(0.f);

	Sleep(250);
#ifdef TEXTMODE
	F::NamedPipe.Shutdown();
#endif
	F::EnginePrediction.Unload();
	U::ConVars.Restore();
	F::Materials.UnloadMaterials();

	if (m_bFailed2)
	{
		LogFailText();
		return;
	}

	SDK::Output("Amalgam", "Unloaded", { 175, 150, 255 }, OUTPUT_CONSOLE | OUTPUT_DEBUG);
}