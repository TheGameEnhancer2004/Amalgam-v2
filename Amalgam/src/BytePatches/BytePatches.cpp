#include "BytePatches.h"

#include "../Core/Core.h"

BytePatch::BytePatch(const char* sModule, const char* sSignature, int iOffset, const char* sPatch)
{
	m_sModule = sModule;
	m_sSignature = sSignature;
	m_iOffset = iOffset;

	auto vPatch = U::Memory.PatternToByte(sPatch);
	m_vPatch = vPatch;
	m_iSize = vPatch.size();
	m_vOriginal.resize(m_iSize);
}

void BytePatch::Write(std::vector<byte>& vBytes)
{
	DWORD flNewProtect, flOldProtect;
	VirtualProtect(m_pAddress, m_iSize, PAGE_EXECUTE_READWRITE, &flNewProtect);
	memcpy(m_pAddress, vBytes.data(), m_iSize);
	VirtualProtect(m_pAddress, m_iSize, flNewProtect, &flOldProtect);
}

bool BytePatch::Initialize()
{
	if (m_bIsPatched)
		return true;

	m_pAddress = LPVOID(U::Memory.FindSignature(m_sModule, m_sSignature));
	if (!m_pAddress)
	{
		U::Core.AppendFailText(std::format("BytePatch::Initialize() failed to initialize:\n  {}\n  {}", m_sModule, m_sSignature).c_str());
		return false;
	}

	m_pAddress = LPVOID(uintptr_t(m_pAddress) + m_iOffset);

	DWORD flNewProtect, flOldProtect;
	VirtualProtect(m_pAddress, m_iSize, PAGE_EXECUTE_READWRITE, &flNewProtect);
	memcpy(m_vOriginal.data(), m_pAddress, m_iSize);
	VirtualProtect(m_pAddress, m_iSize, flNewProtect, &flOldProtect);

	Write(m_vPatch);
	return m_bIsPatched = true;
}

void BytePatch::Unload()
{
	if (!m_bIsPatched)
		return;

	Write(m_vOriginal);
	m_bIsPatched = false;
}



bool CBytePatches::Initialize()
{
	m_vPatches = {
		BytePatch("engine.dll", "0F 82 ? ? ? ? 4A 63 84 2F", 0x0, "90 90 90 90 90 90"), // skybox fix
		//BytePatch("server.dll", "75 ? 44 38 A7 ? ? ? ? 75 ? 41 3B DD", 0x0, "EB"), // listen server speedhack
		BytePatch("vguimatsurface.dll", "66 83 FE ? 0F 84", 0x0, "66 83 FE 00"), // include '&' in text size

		// CStorePage::DoPreviewItem
		BytePatch("client.dll", "40 53 48 81 EC ? ? ? ? 0F B7 DA", 0xe8, "A7"),
		// Removes loadout switch delay
		BytePatch("client.dll", "73 ? 48 8D 0D ? ? ? ? FF 15 ? ? ? ? 32 C0", 0x0, "EB"),
		// TODO MOVE TO TEXTMODEDLL
		// Fixes crash in sub_180317DF0 (CEconItemView::GetItemDefinitionIndex)
		BytePatch("client.dll", "48 8B 41 08 48 85 C0 75 0A 48 8B 01 8B 80 BC 00 00 00 C3 8B 40 20 C3", 0x0, "48 8B 41 08 48 85 C0 75 0F 48 8B 01 48 85 C0 74 0B 8B 80 BC 00 00 00 C3 8B 40 20 C3 31 C0 C3"),

		// Fixes crash in sub_1801A0E00
		BytePatch("client.dll", "45 85 C0 78 3E 4C 8B 11 45 3B 82 E8 00 00 00 7D 32", 0x0, "4C 8B 11 4D 85 D2 74 3B 45 3B 82 E8 00 00 00 73 32"),
	};

	bool bFail = false;
	for (auto& tPatch : m_vPatches)
	{
		if (!tPatch.Initialize())
			bFail = true;
	}

	return !bFail;
}

void CBytePatches::Unload()
{
	for (auto& tPatch : m_vPatches)
		tPatch.Unload();
}