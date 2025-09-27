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
	std::atomic<bool> m_shouldRun = true;
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

	std::mutex m_logMutex;
	void Log(std::string sMessage);
	std::string GetErrorMessage(DWORD dwError);
public:
	void Initialize();
	void Shutdown();

	bool IsLocalBot(uint32_t uAccountID);

	void Store(CTFPlayer* pLocal = nullptr, bool bCreateMove = false);
	void Event(IGameEvent* pEvent, uint32_t uHash);
	void Reset();
};

ADD_FEATURE(CNamedPipe, NamedPipe);
#endif