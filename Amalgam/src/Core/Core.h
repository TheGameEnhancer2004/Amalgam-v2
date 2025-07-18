#pragma once
#include "../Utils/Feature/Feature.h"

#include <sstream>
#include <fstream>

class CCore
{
public:
	void Load();
	void Loop();
	void Unload();

	const char* SearchForDLL(const char* pszDLLSearch);
	void AppendFailText(const char* sMessage);
	void LogFailText();

	bool m_bUnload = false;

	bool m_bFailed = false;
	bool m_bFailed2 = false;
	std::stringstream m_ssFailStream;

private:
};

ADD_FEATURE_CUSTOM(CCore, Core, U);