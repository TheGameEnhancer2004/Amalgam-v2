#pragma once

#ifdef TEXTMODE

#include "../../../SDK/SDK.h"
#include <thread>
#include <atomic>
#include <fstream>
#include <shared_mutex>

class CNamedPipe
{
private:
	HANDLE m_hPipe = INVALID_HANDLE_VALUE;
	std::atomic<bool> m_shouldRun = false;
	std::atomic<bool> m_pipeThreadRunning = false;
	std::thread m_pipeThread;
	std::ofstream m_logFile;
	int m_iBotId = -1;

	int m_iCurrentReconnectAttempts = 0;
	DWORD m_dwLastConnectAttemptTime = 0;

	std::mutex m_messageQueueMutex;
	struct PendingMessage
	{
		std::string m_sType;
		std::string m_sContent;
		bool m_bIsPriority;
	};
	std::vector<PendingMessage> m_vMessageQueue;

	struct CaptureSpotReservation
	{
		std::string m_sMap;
		int m_iPointIndex = -1;
		Vector m_vSpot{};
		uint32_t m_uOwnerAccountID = 0;
		int m_iBotId = -1;
		double m_flExpiresAt = 0.0;
	};
	std::mutex m_captureMutex;
	std::vector<CaptureSpotReservation> m_vCaptureReservations;

	std::mutex m_localBotsMutex;
	std::unordered_map<uint32_t, bool> m_mLocalBots;

	std::shared_mutex m_infoMutex;
	struct ClientInfo
	{
		int m_iCurrentHealth = -1;
		int m_iCurrentClass = TF_CLASS_UNDEFINED;
		std::string m_sCurrentServer = "N/A";
		std::string m_sCurrentMapName = "N/A";
		uint32_t m_uAccountID = 0;

		bool m_bInGame = false;
	};
	ClientInfo tInfo;
	bool m_bSetServerName = false;
	bool m_bSetMapName = false;

	std::string GetPlayerClassName(int iPlayerClass);

	int GetBotIdFromEnv();
	int ReadBotIdFromFile();
	int GetReconnectDelayMs();

	static void ConnectAndMaintainPipe();

	void SendStatusUpdate(std::string sStatus);
	void ExecuteCommand(std::string sCommand);
	void QueueMessage(std::string sType, std::string sContent, bool bIsPriority);
	void ProcessMessageQueue();

	void ProcessLocalBotMessage(std::string sAccountID);
	void UpdateLocalBotIgnoreStatus();
	void ClearLocalBots();
	void ProcessCaptureReservationMessage(const std::string& sContent);
	void ClearCaptureReservations();
	void PurgeExpiredCaptureReservations();

	std::mutex m_logMutex;
	void Log(std::string sMessage);
	std::string GetErrorMessage(DWORD dwError);
public:
	void Initialize();
	void Shutdown();
	bool EnablePipeConnection();
	bool IsPipeConnectionActive() const { return m_pipeThreadRunning.load(); }

	bool IsLocalBot(uint32_t uAccountID);
	void AnnounceCaptureSpotClaim(const std::string& sMap, int iPointIdx, const Vector& vSpot, float flDurationSeconds = 1.0f);
	void AnnounceCaptureSpotRelease(const std::string& sMap, int iPointIdx);
	std::vector<Vector> GetReservedCaptureSpots(const std::string& sMap, int iPointIdx, uint32_t uIgnoreAccountID = 0);

	void Store(CTFPlayer* pLocal = nullptr, bool bCreateMove = false);
	void Event(IGameEvent* pEvent, uint32_t uHash);
	void Reset();
};

ADD_FEATURE(CNamedPipe, NamedPipe);
#endif