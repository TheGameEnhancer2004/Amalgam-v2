#pragma once
#include "../../SDK/SDK.h"

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
	void MicSpam(CTFPlayer* pLocal);
	void AchievementSpam(CTFPlayer* pLocal);
	void NoiseSpam(CTFPlayer* pLocal);
	void CallVoteSpam(CTFPlayer* pLocal);

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
	Timer m_tChatSpamTimer;
	int m_iCurrentChatSpamIndex = 0;

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
	void RunPre(CTFPlayer* pLocal, CUserCmd* pCmd);
	void RunPost(CTFPlayer* pLocal, CUserCmd* pCmd, bool pSendPacket);

	void Event(IGameEvent* pEvent, uint32_t uNameHash);
	int AntiBackstab(CTFPlayer* pLocal, CUserCmd* pCmd, bool bSendPacket);

	void PingReducer();
	void UnlockAchievements();
	void LockAchievements();
	void AutoMvmReadyUp();

	int m_iWishCmdrate = -1;
	//int m_iWishUpdaterate = -1;
	bool m_bAntiAFK = false;
	Timer m_tAutoVotekickTimer;
};

ADD_FEATURE(CMisc, Misc);