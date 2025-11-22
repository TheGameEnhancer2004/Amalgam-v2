#pragma once
#include "../Macros/Macros.h"
#include <Windows.h>

class CErrorLog
{
public:
	void Initialize(LPVOID lpParam = nullptr);
	void Unload();
};

ADD_FEATURE_CUSTOM(CErrorLog, ErrorLog, U);