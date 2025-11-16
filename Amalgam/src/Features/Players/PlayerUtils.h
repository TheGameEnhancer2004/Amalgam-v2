#pragma once
#include "../../SDK/SDK.h"
#include <shared_mutex>

#define DEFAULT_TAG 0
#define IGNORED_TAG (DEFAULT_TAG-1)
#define CHEATER_TAG (IGNORED_TAG-1)
#define FRIEND_TAG (CHEATER_TAG-1)
#define PARTY_TAG (FRIEND_TAG-1)
#define F2P_TAG (PARTY_TAG-1)
#define FRIEND_IGNORE_TAG (F2P_TAG-1)
#define BOT_IGNORE_TAG (FRIEND_IGNORE_TAG-1)
#define TAG_COUNT (-BOT_IGNORE_TAG)

struct ListPlayer_t
{
	std::string m_sName;
	uint32_t m_uAccountID;
	int m_iUserID;
	int m_iTeam;
	bool m_bAlive;
	bool m_bLocal;
	bool m_bFake;
	bool m_bFriend;
	bool m_bParty;
	bool m_bF2P;
	int m_iLevel;
	int m_iParty;
};

struct BotIgnoreData_t
{
	int m_iKillCount = 0;
	bool m_bIsIgnored = false;
};

struct CheaterRecord_t
{
	uint32_t m_uAccountID = 0;
	std::string m_sName = "";
	std::string m_sReason = "tagged by the player";
	int m_iDetections = 0;
	bool m_bAuto = false;
	int m_iTimestamp = 0;
};

struct PriorityLabel_t
{
	std::string m_sName = "";
	Color_t m_tColor = {};
	int m_iPriority = 0;
	int m_iFollowPriority = 0;

	bool m_bLabel = false;
	bool m_bAssignable = true;
	bool m_bLocked = false; // don't allow it to be removed
};

Enum(NameType, None = 0, Local = 1 << 0, Friend = 1 << 1, Party = 1 << 2, Player = 1 << 3, Custom = 1 << 4, Privacy = Local | Friend | Party | Player);

class CPlayerlistUtils
{
public:
	std::unordered_map<uint32_t, std::vector<int>> m_mPlayerTags = {};
	std::unordered_map<uint32_t, std::string> m_mPlayerAliases = {};
	std::unordered_map<uint32_t, BotIgnoreData_t> m_mBotIgnoreData = {};
	std::unordered_map<uint32_t, CheaterRecord_t> m_mCheaterRecords = {};

	std::vector<PriorityLabel_t> m_vTags = {
		{ "Default", { 200, 200, 200, 255 }, 0, 0, false, false, true },
		{ "Ignored", { 200, 200, 200, 255 }, -1, 0, false, true, true },
		{ "Cheater", { 255, 100, 100, 255 }, 1, 0, false, true, true },
		{ "Friend", { 100, 255, 100, 255 }, 0, 2, true, false, true },
		{ "Party", { 100, 100, 255, 255 }, 0, 1, true, false, true },
		{ "F2P", { 255, 255, 255, 255 }, 0, 0, true, false, true },
		{ "Friend Ignore", { 255, 100, 100, 255 }, -1, 0, false, true, true },
		{ "Bot Ignore", { 255, 100, 100, 255 }, -1, 0, false, true, true }
	};

	std::vector<ListPlayer_t> m_vPlayerCache = {};
	std::unordered_map<uint32_t, ListPlayer_t> m_mPriorityCache = {};

	bool m_bLoad = true;
	bool m_bSave = false;
	bool m_bCheaterLoad = true;
	bool m_bCheaterSave = false;

	// Thai characters to check for auto-tagging
	const std::vector<unsigned char> m_vSpecialChars = { 
		0xE0, 0xB9, 0x87,  // '็'
		0xE0, 0xB9, 0x88,  // '่'
		0xE0, 0xB9, 0x8A,  // '๊'
		0xE0, 0xB9, 0x8B,  // '๋'
		0xE0, 0xB9, 0x8C,  // '์'
		0xE0, 0xB9, 0xB9   // 'ู'
	};
	mutable std::shared_mutex m_mutex;

private:
	std::vector<int> m_vDummy = {};
	std::string ResolveAccountName(uint32_t uAccountID) const;

public:
	void Store();

	uint32_t GetAccountID(int iIndex);
	int GetIndex(uint32_t uAccountID);

	PriorityLabel_t* GetTag(int iID);
	int GetTag(const std::string& sTag);
	inline int TagToIndex(int iTag)
	{
		if (iTag <= 0)
			iTag = -iTag;
		else
			iTag += TAG_COUNT;
		return iTag;
	}
	inline int IndexToTag(int iID)
	{
		if (iID <= TAG_COUNT)
			iID = -iID;
		else
			iID -= TAG_COUNT;
		return iID;
	}

	void AddTag(uint32_t uAccountID, int iID, bool bSave, const char* sName, std::unordered_map<uint32_t, std::vector<int>>& mPlayerTags, const char* sReason = nullptr, int iDetections = 0, bool bAuto = false);
	void AddTag(uint32_t uAccountID, int iID, bool bSave = true, const char* sName = nullptr, const char* sReason = nullptr, int iDetections = 0, bool bAuto = false);
	void AddTag(int iIndex, int iID, bool bSave, const char* sName, std::unordered_map<uint32_t, std::vector<int>>& mPlayerTags, const char* sReason = nullptr, int iDetections = 0, bool bAuto = false);
	void AddTag(int iIndex, int iID, bool bSave = true, const char* sName = nullptr, const char* sReason = nullptr, int iDetections = 0, bool bAuto = false);
	void RemoveTag(uint32_t uAccountID, int iID, bool bSave, const char* sName, std::unordered_map<uint32_t, std::vector<int>>& mPlayerTags);
	void RemoveTag(uint32_t uAccountID, int iID, bool bSave = true, const char* sName = nullptr);
	void RemoveTag(int iIndex, int iID, bool bSave, const char* sName, std::unordered_map<uint32_t, std::vector<int>>& mPlayerTags);
	void RemoveTag(int iIndex, int iID, bool bSave = true, const char* sName = nullptr);
	bool HasTags(uint32_t uAccountID, std::unordered_map<uint32_t, std::vector<int>>& mPlayerTags);
	bool HasTags(uint32_t uAccountID);
	bool HasTags(int iIndex, std::unordered_map<uint32_t, std::vector<int>>& mPlayerTags);
	bool HasTags(int iIndex);
	bool HasTag(uint32_t uAccountID, int iID, std::unordered_map<uint32_t, std::vector<int>>& mPlayerTags);
	bool HasTag(uint32_t uAccountID, int iID);
	bool HasTag(int iIndex, int iID, std::unordered_map<uint32_t, std::vector<int>>& mPlayerTags);
	bool HasTag(int iIndex, int iID);

	int GetPriority(uint32_t uAccountID, bool bCache = true);
	int GetPriority(int iIndex, bool bCache = true);
	int GetFollowPriority(uint32_t uAccountID, bool bCache = true);
	int GetFollowPriority(int iIndex, bool bCache = true);
	PriorityLabel_t* GetSignificantTag(uint32_t uAccountID, int iMode = 1); // iMode: 0 - Priorities & Labels, 1 - Priorities, 2 - Labels
	PriorityLabel_t* GetSignificantTag(int iIndex, int iMode = 1); // iMode: 0 - Priorities & Labels, 1 - Priorities, 2 - Labels
	bool IsIgnored(uint32_t uAccountID);
	bool IsIgnored(int iIndex);
	bool IsPrioritized(uint32_t uAccountID);
	bool IsPrioritized(int iIndex);

	int GetNameType(int iIndex);
	int GetNameType(uint32_t uAccountID);
	const char* GetPlayerName(int iIndex, const char* sDefault, int* pType = nullptr);
	const char* GetPlayerName(uint32_t uAccountID, const char* sDefault, int* pType = nullptr);
	
	// bool ContainsSpecialChars(const std::string& sName);
	// void ProcessSpecialCharsInName(uint32_t uAccountID, const std::string& sName);
	void IncrementBotIgnoreKillCount(uint32_t uAccountID);

	std::vector<int>& GetPlayerTags(uint32_t uAccountID) { return m_mPlayerTags.contains(uAccountID) ? m_mPlayerTags[uAccountID] : m_vDummy; }
	std::string* GetPlayerAlias(uint32_t uAccountID) { return m_mPlayerAliases.contains(uAccountID) ? &m_mPlayerAliases[uAccountID] : nullptr; }
	const CheaterRecord_t* GetCheaterRecord(uint32_t uAccountID) const { return m_mCheaterRecords.contains(uAccountID) ? &m_mCheaterRecords.at(uAccountID) : nullptr; }
	bool HasCheaterRecord(uint32_t uAccountID) const { return m_mCheaterRecords.contains(uAccountID); }
	void UpdateCheaterRecord(uint32_t uAccountID, const char* sName, const char* sReason, int iDetections, bool bAuto);
	void RemoveCheaterRecord(uint32_t uAccountID, bool bMarkSave = true);
	std::vector<std::pair<uint32_t, CheaterRecord_t>> GetCheaterVector();
	bool ImportCheatersFromJson(const std::string& sJson, bool bMarkDirty);
	std::string ExportCheatersToJson() const;
};

ADD_FEATURE(CPlayerlistUtils, PlayerUtils);