#pragma once
#include "../Feature/Feature.h"
#include <Windows.h>
#include <cstdint>
#include <vector>
#include <string>

class CMemory
{
public:
	std::vector<byte> PatternToByte(const char* szPattern);
	std::vector<int> PatternToInt(const char* szPattern);
	uintptr_t FindSignature(const char* szModule, const char* szPattern);
	uintptr_t FindSignatureAtAddress(uintptr_t uAddress, const char* szPattern, uintptr_t uSkipAddress = 0x0, bool* bRetFound = nullptr);
	PVOID FindInterface(const char* szModule, const char* szObject);
	std::string GetModuleOffset(uintptr_t uAddress);
	std::string GetModuleName(uintptr_t uAddress);
	std::string GenerateSignatureAtAddress(uintptr_t address, size_t maxLength = 120);

	inline void* GetVirtual(void* p, size_t i)
	{
		auto vTable = *static_cast<void***>(p);
		return vTable[i];
	}

	template <size_t I, typename T, typename... Args>
	inline T CallVirtual(void* p, Args... args) const
	{
		auto vTable = *static_cast<void***>(p);
		return reinterpret_cast<T(__fastcall*)(void*, Args...)>(vTable[I])(p, args...);
	}

	template <size_t I, typename T, typename... Args>
	inline T CallVirtual(uintptr_t u, Args... args) const
	{
		auto p = reinterpret_cast<void*>(u);
		auto vTable = *static_cast<void***>(p);
		return reinterpret_cast<T(__fastcall*)(void*, Args...)>(vTable[I])(p, args...);
	}

	inline uintptr_t RelToAbs(const uintptr_t address, const uintptr_t offset = 0x3)
	{
		return *reinterpret_cast<std::int32_t*>(address + offset) + address + offset + 0x4;
	}

	template <typename T>
	inline T GetModuleExport(const char* szModule, const char* szExport)
	{
		if (auto hModule = GetModuleHandle(szModule))
			return reinterpret_cast<T>(GetProcAddress(hModule, szExport));
		return reinterpret_cast<T>(nullptr);
	}
};

ADD_FEATURE_CUSTOM(CMemory, Memory, U);

#define OFFSET(name, type, offset) inline type& name() \
{ \
	return *reinterpret_cast<type*>(uintptr_t(this) + offset); \
}

#define CONDGET(name, conditions, cond) inline bool name() \
{ \
	return conditions & cond; \
}

#define VIRTUAL(name, type, index, ...) inline type name() \
{ \
	return U::Memory.CallVirtual<index, type>(##__VA_ARGS__); \
}

#define VIRTUAL_ARGS(name, type, index, args, ...) inline type name##args \
{ \
	return U::Memory.CallVirtual<index, type>(##__VA_ARGS__); \
}