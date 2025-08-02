#include "Core.h"

#include "../SDK/SDK.h"
#include "../BytePatches/BytePatches.h"
#include "../Features/Visuals/Materials/Materials.h"
#include "../Features/Configs/Configs.h"
#include "../Features/Commands/Commands.h"
#include "../Features/ImGui/Menu/Menu.h"
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

	char buffer[MAX_PATH];
	if (!GetModuleBaseName(hProcess, nullptr, buffer, sizeof(buffer) / sizeof(char)))
	{
		CloseHandle(hProcess);
		return "";
	}

	CloseHandle(hProcess);
	return buffer;
}

static inline bool CheckDXLevel()
{
	auto mat_dxlevel = U::ConVars.FindVar("mat_dxlevel");
	if (mat_dxlevel->GetInt() < 90)
	{
		//const char* sMessage = "You are running with graphics options that Amalgam does not support.\n-dxlevel must be at least 90.";
		const char* sMessage = "You are running with graphics options that Amalgam does not support.\nIt is recommended for -dxlevel to be at least 90.";
		U::Core.AppendFailText(sMessage);
		F::Menu.ShowDeferredNotification("Graphics Warning", sMessage, 15.0f);
		SDK::Output("Amalgam", sMessage, { 175, 150, 255 }, true, true);
		//return false;
	}

	return true;
}

const char* CCore::SearchForDLL(const char* pszDLLSearch)
{
	HANDLE hProcessSnap = INVALID_HANDLE_VALUE;
	PROCESSENTRY32 pe32;
	hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (hProcessSnap == INVALID_HANDLE_VALUE)
		return pszDLLSearch;

	pe32.dwSize = sizeof(PROCESSENTRY32);
	if (!Process32First(hProcessSnap, &pe32))
	{
		CloseHandle(hProcessSnap);
		return pszDLLSearch;
	}

	do
	{
		if (pe32.szExeFile == strstr(pe32.szExeFile, "tf_win64.exe"))
		{
			HANDLE hModuleSnap = INVALID_HANDLE_VALUE;
			MODULEENTRY32 me32;
			hModuleSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pe32.th32ProcessID);
			if (hModuleSnap == INVALID_HANDLE_VALUE)
				break;

			me32.dwSize = sizeof(MODULEENTRY32);
			if (!Module32First(hModuleSnap, &me32))
			{
				CloseHandle(hModuleSnap);
				break;
			}

			do
			{
				if (strstr(me32.szModule, pszDLLSearch))
				{
					CloseHandle(hProcessSnap);
					CloseHandle(hModuleSnap);
					return me32.szModule;
				}
			} while (Module32Next(hModuleSnap, &me32));

			CloseHandle(hModuleSnap);
			break;
		}
	} while (Process32Next(hProcessSnap, &pe32));

	CloseHandle(hProcessSnap);
	return pszDLLSearch;
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
	SDK::Output("Failed to load", m_ssFailStream.str().c_str(), {}, false, true, false, false, false, false, MB_OK | MB_ICONERROR);
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
		FNV1A::Hash32(U::Core.SearchForDLL("shaderapi")) != FNV1A::Hash32Const("shaderapi");
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
	U::ConVars.Initialize();
	F::Commands.Initialize();
#ifdef TEXTMODE
	F::NamedPipe::Initialize();
#endif

	F::Configs.LoadConfig(F::Configs.m_sCurrentConfig, false);
	F::Configs.m_bConfigLoaded = true;

	SDK::Output("Amalgam", "Loaded", { 175, 150, 255 }, true, true, true);
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
	F::NamedPipe::Shutdown();
#endif
	U::ConVars.Unload();
	F::Materials.UnloadMaterials();

	if (m_bFailed2)
	{
		LogFailText();
		return;
	}

	SDK::Output("Amalgam", "Unloaded", { 175, 150, 255 }, true, true);
}