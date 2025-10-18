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
#ifdef TEXTMODE
		// CCharacterInfoPanel::CCharacterInfoPanel (Prevent panel initializations)
		BytePatch("client.dll", "B9 ? ? ? ? E8 ? ? ? ? 48 85 C0 74 ? 41 B8 ? ? ? ? 48 8B D6 48 8B C8 E8 ? ? ? ? 48 8B C8 EB ? 48 8B CD 48 89 8E ? ? ? ? 48 8B D6 48 8B 01 FF 90 ? ? ? ? 48 8B 96 ? ? ? ? 4C 8D 05 ? ? ? ? 48 8B CE E8 ? ? ? ? B9", 0x0, "E9 B9 00"),
		BytePatch("client.dll", "48 8B 8E ? ? ? ? 33 D2 48 8B 01 FF 90 ? ? ? ? 4C 8D 5C 24", 0x0, "EB 15"),

		// CCharacterInfoPanel::CreateStorePanel (Do nothing)
		BytePatch("client.dll", "48 83 EC ? 48 8D 0D ? ? ? ? E8 ? ? ? ? 48 85 C0 74 ? 48 8D 0D ? ? ? ? E8 ? ? ? ? 48 8B C8 48 8B 10 FF 92 ? ? ? ? E8", 0x0, "5B C3 CC"),

		// CCharacterInfoPanel::Close (Prevent m_pLoadoutPanel call)
		BytePatch("client.dll", "B9 ? ? ? ? E8 ? ? ? ? 48 85 C0 74 ? 48 8D 15 ? ? ? ? 48 8B C8 E8 ? ? ? ? 4C 8B C0 EB ? 45 33 C0 48 8B 03 0F 57 DB 48 8B 93 ? ? ? ? 48 8B CB FF 90 ? ? ? ? 48 8B 0D", 0x0, "EB 3A"),
		
		// CCharacterInfoPanel::OnCommand (Prevent m_pLoadoutPanel calls)
		BytePatch("client.dll", "48 8D 15 ? ? ? ? 48 3B FA 74 ? 48 8B CF E8 ? ? ? ? 85 C0 74 ? 48 8B 0D ? ? ? ? 48 8B D7 48 8B 01 FF 50 ? E9", 0x0, "EB 16"),

		// CCharacterInfoPanel::ShowPanel (Prevent m_pLoadoutPanel calls)
		BytePatch("client.dll", "0F 84 ? ? ? ? 48 8B 01 FF 90 ? ? ? ? 48 8B C8", 0x2, "59"),
		BytePatch("client.dll", "0F 84 ? ? ? ? 48 8B 01 FF 90 ? ? ? ? 48 8B C8", 0x6, "E9 E2 00 00 00"),
		BytePatch("client.dll", "49 8B 8E ? ? ? ? 45 33 C0 8B 91", 0x0, "EB 64"),
		BytePatch("client.dll", "49 8B 06 49 8B CE FF 90 ? ? ? ? 84 C0 74 ? 49 8B 8E", 0x0, "E9 EA 00 00 00"),

		// CCharacterInfoPanel::IsUIPanelVisible (Prevent m_pLoadoutPanel calls)
		BytePatch("client.dll", "74 ? 83 EB ? 74 ? 83 EB ? 74 ? 83 FB ? 75 ? 48 8B 47", 0x0, "EB"),

		// CTFPlayerInventory::SOUpdated (Prevent CCharacterInfoPanel::GetBackpackPanel call)
		BytePatch("client.dll", "75 ? E8 ? ? ? ? 48 8B C8 48 8B 10 FF 52 ? 48 8B 53", 0x0, "EB"),
#endif
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