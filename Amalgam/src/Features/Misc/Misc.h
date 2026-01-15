#pragma once
#include "../../SDK/SDK.h"
#include <filesystem>

class CMisc
{
private:
	void AutoJump(CTFPlayer* pLocal, CUserCmd* pCmd);
	void AutoJumpbug(CTFPlayer* pLocal, CUserCmd* pCmd);
	void AutoStrafe(CTFPlayer* pLocal, CUserCmd* pCmd);
	void MovementLock(CTFPlayer* pLocal, CUserCmd* pCmd);
	void BreakJump(CTFPlayer* pLocal, CUserCmd* pCmd);
	void BreakShootSound(CTFPlayer* pLocal, CUserCmd* pCmd);
	void AntiAFK(CTFPlayer* pLocal, CUserCmd* pCmd);
	void InstantRespawnMVM(CTFPlayer* pLocal);
	void ExecBuyBot(CTFPlayer* pLocal);
	void ResetBuyBot();
	void VoiceCommandSpam(CTFPlayer* pLocal);
	void RandomVotekick(CTFPlayer* pLocal);
	void ChatSpam(CTFPlayer* pLocal);

	void AchievementSpam(CTFPlayer* pLocal);
	void NoiseSpam(CTFPlayer* pLocal);
	void CallVoteSpam(CTFPlayer* pLocal);

	void AutoReport();
	void CheatsBypass();
	void WeaponSway();

	void TauntKartControl(CTFPlayer* pLocal, CUserCmd* pCmd);
	void FastMovement(CTFPlayer* pLocal, CUserCmd* pCmd);

	void AutoPeek(CTFPlayer* pLocal, CUserCmd* pCmd, bool bPost = false);
	void EdgeJump(CTFPlayer* pLocal, CUserCmd* pCmd, bool bPost = false);

	bool m_bPeekPlaced = false;
	Vec3 m_vPeekReturnPos = {};

	//bool bSteamCleared = false;

	std::vector<std::string> m_vChatSpamLines;
	struct AutoReply_t { std::vector<std::string> vTriggers; std::vector<std::string> vReplies; };
	std::vector<AutoReply_t> m_vAutoReplies;
	std::vector<std::string> m_vF1Messages;
	std::vector<std::string> m_vF2Messages;
	Timer m_tChatSpamTimer;
	int m_iCurrentChatSpamIndex = 0;

	bool LoadLines(const char* szFileName, std::vector<std::string>& vLines, const char* szDefaultContent = nullptr);
	std::vector<std::string> ParseTokens(std::string str, char delimiter);

	enum class AchievementSpamState
	{
		IDLE,
		CLEARING,
		WAITING,
		AWARDING
	};

	AchievementSpamState m_eAchievementSpamState = AchievementSpamState::IDLE;
	Timer m_tAchievementSpamTimer;
	Timer m_tAchievementDelayTimer;
	int m_iAchievementSpamID = 0;
	std::string m_sAchievementSpamName = "";
	Timer m_tCallVoteSpamTimer;

	int m_iBuybotStep = 1;
	float m_flBuybotClock = 0.0f;

public:
	struct ProfileDumpResult_t
	{
		bool m_bResourceAvailable = false;
		bool m_bFileOpened = false;
		bool m_bSuccess = false;
		size_t m_uCandidateCount = 0;
		size_t m_uSkippedInvalid = 0;
		size_t m_uSkippedComma = 0;
		size_t m_uSkippedSessionDuplicate = 0;
		size_t m_uSkippedFileDuplicate = 0;
		size_t m_uAppendedCount = 0;
		size_t m_uAvatarsSaved = 0;
		size_t m_uAvatarMissed = 0;
		size_t m_uAvatarFailed = 0;
		std::filesystem::path m_outputPath;
		std::filesystem::path m_avatarFolder;
	};

	void RunPre(CTFPlayer* pLocal, CUserCmd* pCmd);
	void RunPost(CTFPlayer* pLocal, CUserCmd* pCmd);

	void Event(IGameEvent* pEvent, uint32_t uNameHash);
	int AntiBackstab(CTFPlayer* pLocal, CUserCmd* pCmd, bool bSendPacket);
	void MicSpam();

	void PingReducer();
	void UnlockAchievements();
	void LockAchievements();
	void AutoMvmReadyUp();
	void OnVoteStart(int iCaller, int iTarget, const std::string& sReason, const std::string& sTarget);
	void OnChatMessage(int iEntIndex, const std::string& sName, const std::string& sMsg);
	std::string ReplaceTags(std::string sMsg, std::string sTarget = "", std::string sInitiator = "");
	ProfileDumpResult_t DumpProfiles(bool bAnnounce = true);

	int m_iWishCmdrate = -1;
	//int m_iWishUpdaterate = -1;
	bool m_bAntiAFK = false;
	std::string m_sLastKilledName = "";
	Timer m_tAutoVotekickTimer;
};

ADD_FEATURE(CMisc, Misc);