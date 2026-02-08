#pragma once
#include "Interface.h"

MAKE_SIGNATURE(CTFPartyClient_SendPartyChat, "client.dll", "48 89 5C 24 ? 48 89 6C 24 ? 57 48 83 EC ? 48 C7 C3", 0x0);
MAKE_SIGNATURE(CTFPartyClient_LoadSavedCasualCriteria, "client.dll", "48 83 79 ? ? C6 81 ? ? ? ? ? 74 ? 80 79 ? ? 74 ? C6 81 ? ? ? ? ? 48 8B 15", 0x0);
MAKE_SIGNATURE(CTFPartyClient_BInQueueForMatchGroup, "client.dll", "48 89 5C 24 ? 57 48 83 EC ? 48 8B F9 8B DA 8B CA E8 ? ? ? ? 84 C0", 0x0);
MAKE_SIGNATURE(CTFPartyClient_RequestQueueForMatch, "client.dll", "40 55 56 48 81 EC ? ? ? ? 48 63 F2", 0x0);
MAKE_SIGNATURE(CCasualCriteriaHelper_CCasualCriteriaHelper, "client.dll", "48 89 5C 24 ? 56 57 41 57 48 83 EC ? 0F 57 C0", 0x0);
MAKE_SIGNATURE(CTFGroupMatchCriteriaProto_DefaultInstance, "client.dll", "48 8B 05 ? ? ? ? 48 8B 50 ? 48 8D 4C 24 ? E8 ? ? ? ? 44 8B 44 24", 0x0);

class CTFCasualMatchCriteria
{
public:

};

class CCasualCriteriaHelper
{
public:
	CCasualCriteriaHelper(const CTFCasualMatchCriteria* criteria)
	{
		S::CCasualCriteriaHelper_CCasualCriteriaHelper.Call<CCasualCriteriaHelper*>(this, criteria);
	}

	bool AnySelected() const { return !m_mapsBits.IsAllClear(); }

	CLargeVarBitVec m_mapsBits;
};

class CTFGroupMatchCriteriaProto
{
public:
	byte pad0[16];
	uint32 has_bits[1];
	mutable int cached_size;
	bool late_join_ok;
	uint32 custom_ping_tolerance;
	std::string* mvm_mannup_tour;
	byte mvm_mannup_missions[24];
	byte mvm_bootcamp_missions[24];
	CTFCasualMatchCriteria* casual_criteria;

	static CTFGroupMatchCriteriaProto* GetDefaultInstance()
	{
		auto default_instance = *reinterpret_cast<CTFGroupMatchCriteriaProto**>(U::Memory.RelToAbs(S::CTFGroupMatchCriteriaProto_DefaultInstance()));
		return default_instance;
	}

	CTFCasualMatchCriteria* GetCasualCriteria()
	{
		return casual_criteria ? casual_criteria : GetDefaultInstance()->casual_criteria;
	}
};

class CTFGroupMatchCriteria
{
public:
	VIRTUAL(Proto, CTFGroupMatchCriteriaProto*, 1, this);
};

class CTFPartyClient
{
public:
	SIGNATURE_ARGS(SendPartyChat, void, CTFPartyClient, (const char* sMessage), this, sMessage);
	SIGNATURE(LoadSavedCasualCriteria, void, CTFPartyClient, this);
	SIGNATURE_ARGS(BInQueueForMatchGroup, bool, CTFPartyClient, (int eMatchGroup), this, eMatchGroup);
	SIGNATURE_ARGS(RequestQueueForMatch, void, CTFPartyClient, (int eMatchGroup), this, eMatchGroup);

	OFFSET_EMBED(m_localGroupCriteria, CTFGroupMatchCriteria*, 432);

	bool AnySelected()
	{
		auto pCriteria = m_localGroupCriteria();
		auto pProto = pCriteria->Proto();
		auto pCasualCriteria = pProto->GetCasualCriteria();
		CCasualCriteriaHelper tHelper(pCasualCriteria);
		return tHelper.AnySelected();
	}
};

MAKE_INTERFACE_NULL(CTFPartyClient, TFPartyClient);