//
//
//
// TODO: REFACTOR THIS bad code
//
//
//
#ifdef TEXTMODE

#include "NamedPipe.h"
#include "../../../SDK/SDK.h"
#include "../../Players/PlayerUtils.h"

#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <exception>

const char* PIPE_NAME = "\\\\.\\pipe\\AwootismBotPipe";
const int BASE_RECONNECT_DELAY_MS = 500;
const int MAX_RECONNECT_DELAY_MS = 10000;

static double GetNowSeconds()
{
	return static_cast<double>(GetTickCount64()) / 1000.0;
}

void CNamedPipe::Initialize()
{
	m_logFile.open("C:\\pipe_log.txt", std::ios::app);
	if (!m_logFile.is_open())
		std::cerr << "Failed to open log file" << std::endl;

	Log("NamedPipe::Initialize() called");
	m_shouldRun.store(false);
	m_pipeThreadRunning.store(false);
	m_iBotId = ReadBotIdFromFile();

	if (m_iBotId == -1)
		Log("Failed to read bot ID from file");
	else
		Log("Bot ID read from file: " + std::to_string(m_iBotId));

	m_mLocalBots.clear();
	Log("Cleared local bots list on startup");
	ClearCaptureReservations();
	m_dwLastConnectAttemptTime = 0;
	m_iCurrentReconnectAttempts = 0;
	Log("Named pipe connection is idle; run cat_ipc_connect to start it");
}

void CNamedPipe::Shutdown()
{
	m_shouldRun.store(false);
	if (m_pipeThread.joinable())
		m_pipeThread.join();
	m_pipeThreadRunning.store(false);
}

bool CNamedPipe::EnablePipeConnection()
{
	if (m_pipeThreadRunning.load())
	{
		Log("EnablePipeConnection called but pipe thread already running");
		return false;
	}

	if (m_pipeThread.joinable())
		m_pipeThread.join();

	m_shouldRun.store(true);
	m_dwLastConnectAttemptTime = 0;
	m_iCurrentReconnectAttempts = 0;

	try
	{
		m_pipeThread = std::thread(ConnectAndMaintainPipe);
		m_pipeThreadRunning.store(true);
		Log("Pipe thread started on demand");
		return true;
	}
	catch (const std::exception& e)
	{
		m_shouldRun.store(false);
		Log("Failed to start pipe thread: " + std::string(e.what()));
	}
	catch (...)
	{
		m_shouldRun.store(false);
		Log("Failed to start pipe thread: unknown error");
	}

	return false;
}

void CNamedPipe::Store(CTFPlayer* pLocal, bool bCreateMove)
{
	std::lock_guard lock(m_infoMutex);
	if (bCreateMove)
	{
		tInfo.m_iCurrentHealth = pLocal->IsAlive() ? pLocal->m_iHealth() : -1;
		tInfo.m_iCurrentClass = pLocal->IsInValidTeam() ? pLocal->m_iClass() : TF_CLASS_UNDEFINED;

		UpdateLocalBotIgnoreStatus();
		return;
	}

	if (!tInfo.m_uAccountID)
		tInfo.m_uAccountID = H::Entities.GetLocalAccountID();
	tInfo.m_bInGame = I::EngineClient->IsInGame();

	if (!m_bSetMapName)
	{
		tInfo.m_sCurrentMapName = SDK::GetLevelName();
		m_bSetMapName = true;
	}

	if (!m_bSetServerName)
	{
		if (auto pNetChan = I::EngineClient->GetNetChannelInfo())
		{
			const char* cAddr = pNetChan->GetAddress();
			if (cAddr && cAddr[0] != '\0' && std::string(cAddr) != "loopback")
			{
				tInfo.m_sCurrentServer = std::string(cAddr);
				m_bSetServerName = true;
			}
		}
	}
}

void CNamedPipe::Event(IGameEvent* pEvent, uint32_t uHash)
{
	switch (uHash)
	{
	case FNV1A::Hash32Const("client_disconnect"):
	case FNV1A::Hash32Const("game_newmap"):
		Reset();
	}
}

void CNamedPipe::Reset()
{
	std::lock_guard lock(m_infoMutex);
	tInfo = ClientInfo(-1, TF_CLASS_UNDEFINED, "N/A", "N/A", tInfo.m_uAccountID, false);
	m_bSetServerName = false;
	m_bSetMapName = false;
	ClearCaptureReservations();
}

void CNamedPipe::Log(std::string sMessage)
{
	std::lock_guard lock(m_logMutex);
	if (!m_logFile.is_open())
		return;

	m_logFile << sMessage << std::endl;
	m_logFile.flush();
	OutputDebugStringA(("NamedPipe: " + sMessage + "\n").c_str());
}

std::string CNamedPipe::GetErrorMessage(DWORD dwError)
{
	char* cMessageBuffer = nullptr;
	size_t uSize = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
								  NULL, dwError, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&cMessageBuffer, 0, NULL);
	std::string sMessage(cMessageBuffer, uSize);
	LocalFree(cMessageBuffer);
	return sMessage;
}

int CNamedPipe::GetBotIdFromEnv()
{
	char* cEnvVal = nullptr;
	size_t len = 0;

	if (_dupenv_s(&cEnvVal, &len, "BOTID") == 0 && cEnvVal)
	{
		int iId = atoi(cEnvVal);
		free(cEnvVal);
		if (iId > 0)
		{
			Log("Found BOTID environment variable: " + std::to_string(iId));
			return iId;
		}
	}

	return -1;
}

int CNamedPipe::ReadBotIdFromFile()
{
	int iEnvId = GetBotIdFromEnv();
	if (iEnvId == -1)
	{
		Log("BOTID environment variable not set. Using fallback ID 1.");
		return 1;
	}
	return iEnvId;
}

int CNamedPipe::GetReconnectDelayMs()
{
	int iDelay = std::min(
		BASE_RECONNECT_DELAY_MS * (1 << std::min(m_iCurrentReconnectAttempts, 10)),
		MAX_RECONNECT_DELAY_MS
	);

	int iJitter = iDelay * 0.2 * (static_cast<double>(rand()) / RAND_MAX - 0.5);
	return iDelay + iJitter;
}

void CNamedPipe::ConnectAndMaintainPipe()
{
	F::NamedPipe.Log("ConnectAndMaintainPipe started");
	srand(static_cast<unsigned int>(time(nullptr)));

	while (F::NamedPipe.m_shouldRun.load())
	{
		DWORD dwCurrentTime = GetTickCount();
		if (F::NamedPipe.m_hPipe == INVALID_HANDLE_VALUE)
		{
			int iReconnectDelay = F::NamedPipe.GetReconnectDelayMs();
			if (dwCurrentTime - F::NamedPipe.m_dwLastConnectAttemptTime > static_cast<DWORD>(iReconnectDelay) || F::NamedPipe.m_dwLastConnectAttemptTime == 0)
			{
				F::NamedPipe.m_dwLastConnectAttemptTime = dwCurrentTime;
				F::NamedPipe.m_iCurrentReconnectAttempts++;

				F::NamedPipe.Log("Attempting to connect to pipe (attempt " + std::to_string(F::NamedPipe.m_iCurrentReconnectAttempts) +
					", delay: " + std::to_string(iReconnectDelay) + "ms)");

				OVERLAPPED tOverlapped = { 0 };
				tOverlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
				if (tOverlapped.hEvent == NULL)
				{
					F::NamedPipe.Log("Failed to create connection event: " + std::to_string(GetLastError()));
					std::this_thread::sleep_for(std::chrono::milliseconds(iReconnectDelay));
					continue;
				}

				F::NamedPipe.m_hPipe = CreateFile(
					PIPE_NAME,
					GENERIC_READ | GENERIC_WRITE,
					0,
					NULL,
					OPEN_EXISTING,
					FILE_FLAG_OVERLAPPED,
					NULL);

				if (F::NamedPipe.m_hPipe != INVALID_HANDLE_VALUE)
				{
					F::NamedPipe.m_iCurrentReconnectAttempts = 0;
					F::NamedPipe.Log("Connected to pipe");

					DWORD dwPipeMode = PIPE_READMODE_MESSAGE;
					SetNamedPipeHandleState(F::NamedPipe.m_hPipe, &dwPipeMode, NULL, NULL);

					F::NamedPipe.QueueMessage("Status", "Connected", true);
					F::NamedPipe.ProcessMessageQueue();

					if (F::NamedPipe.m_iBotId != -1)
						F::NamedPipe.Log("Using Bot ID: " + std::to_string(F::NamedPipe.m_iBotId));
					else
						F::NamedPipe.Log("Warning: Bot ID not set");
					F::NamedPipe.ClearLocalBots();
					F::NamedPipe.ClearCaptureReservations();
				}
				else
				{
					DWORD dwError = GetLastError();
					F::NamedPipe.Log("Failed to connect to pipe: " + std::to_string(dwError) + " - " + F::NamedPipe.GetErrorMessage(dwError));
				}
				CloseHandle(tOverlapped.hEvent);
			}
			else
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}

		if (F::NamedPipe.m_hPipe != INVALID_HANDLE_VALUE)
		{
			DWORD dwBytesAvail = 0;
			if (!PeekNamedPipe(F::NamedPipe.m_hPipe, NULL, 0, NULL, &dwBytesAvail, NULL))
			{
				DWORD dwError = GetLastError();
				if (dwError == ERROR_BROKEN_PIPE || dwError == ERROR_PIPE_NOT_CONNECTED || dwError == ERROR_NO_DATA)
				{
					F::NamedPipe.Log("Pipe disconnected: " + std::to_string(dwError) + " - " + F::NamedPipe.GetErrorMessage(dwError));
					CloseHandle(F::NamedPipe.m_hPipe);
					F::NamedPipe.m_hPipe = INVALID_HANDLE_VALUE;
					continue;
				}
			}
			F::NamedPipe.ProcessMessageQueue();

			F::NamedPipe.QueueMessage("Status", "Heartbeat", true);
			F::NamedPipe.ProcessMessageQueue();

			static int iUpdateCounter = 0;
			if (++iUpdateCounter % 3 == 0)
			{
				std::unique_lock lock(F::NamedPipe.m_infoMutex);

				// Client info
				F::NamedPipe.QueueMessage("Map", F::NamedPipe.tInfo.m_sCurrentMapName, false);
				F::NamedPipe.QueueMessage("Server", F::NamedPipe.tInfo.m_sCurrentServer, false);
				F::NamedPipe.QueueMessage("PlayerClass", F::NamedPipe.GetPlayerClassName(F::NamedPipe.tInfo.m_iCurrentClass), false);
				F::NamedPipe.QueueMessage("Health", std::to_string(F::NamedPipe.tInfo.m_iCurrentHealth), false);
				F::NamedPipe.ProcessMessageQueue();

				if (!F::NamedPipe.tInfo.m_bInGame)
				{
					// Not in game: ensure panel receives disconnect-ish state
					F::NamedPipe.QueueMessage("Status", "NotInGame", true);
					F::NamedPipe.ProcessMessageQueue();
				}
				else
				{
					uint32_t uAccountID = F::NamedPipe.tInfo.m_uAccountID;
					if (F::NamedPipe.tInfo.m_uAccountID != 0)
					{
						F::NamedPipe.QueueMessage("LocalBot", std::to_string(F::NamedPipe.tInfo.m_uAccountID), true);
						F::NamedPipe.Log("Queued local bot ID broadcast: " + std::to_string(F::NamedPipe.tInfo.m_uAccountID));

						if (F::NamedPipe.m_hPipe != INVALID_HANDLE_VALUE)
							F::NamedPipe.ProcessMessageQueue();
					}
				}
				lock.unlock();
			}

			char cBuffer[4096] = { 0 };
			DWORD dwBytesRead = 0;
			OVERLAPPED tOverlapped = { 0 };
			tOverlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
			if (tOverlapped.hEvent != NULL)
			{
				if (dwBytesAvail > 0)
				{
					BOOL bReadSuccess = ReadFile(F::NamedPipe.m_hPipe, cBuffer, sizeof(cBuffer) - 1, &dwBytesRead, &tOverlapped);
					if (!bReadSuccess && GetLastError() == ERROR_IO_PENDING)
					{
						DWORD waitResult = WaitForSingleObject(tOverlapped.hEvent, 1000);
						if (waitResult == WAIT_OBJECT_0)
						{
							if (!GetOverlappedResult(F::NamedPipe.m_hPipe, &tOverlapped, &dwBytesRead, FALSE))
							{
								F::NamedPipe.Log("GetOverlappedResult failed: " + std::to_string(GetLastError()));
								CloseHandle(tOverlapped.hEvent);
								CloseHandle(F::NamedPipe.m_hPipe);
								F::NamedPipe.m_hPipe = INVALID_HANDLE_VALUE;
								continue;
							}
						}
						else if (waitResult == WAIT_TIMEOUT)
						{
							CancelIo(F::NamedPipe.m_hPipe);
							CloseHandle(tOverlapped.hEvent);
							continue;
						}
						else
						{
							F::NamedPipe.Log("Wait failed: " + std::to_string(GetLastError()));
							CloseHandle(tOverlapped.hEvent);
							CloseHandle(F::NamedPipe.m_hPipe);
							F::NamedPipe.m_hPipe = INVALID_HANDLE_VALUE;
							continue;
						}
					}
					else if (!bReadSuccess)
					{
						F::NamedPipe.Log("ReadFile failed immediately: " + std::to_string(GetLastError()));
						CloseHandle(tOverlapped.hEvent);
						CloseHandle(F::NamedPipe.m_hPipe);
						F::NamedPipe.m_hPipe = INVALID_HANDLE_VALUE;
						continue;
					}

					if (dwBytesRead > 0)
					{
						cBuffer[dwBytesRead] = '\0'; // Ensure null termination
						std::string sMessage(cBuffer, dwBytesRead);
						F::NamedPipe.Log("Received message: " + sMessage);

						std::stringstream ss(sMessage);
						std::string sLine;
						while (std::getline(ss, sLine))
						{
							if (sLine.empty())
								continue;

							std::istringstream iss(sLine);
							std::string sBotNumber, sMessageType, sContent;
							std::getline(iss, sBotNumber, ':');
							std::getline(iss, sMessageType, ':');
							std::getline(iss, sContent);

							if (sMessageType == "Command")
							{
								F::NamedPipe.Log("Executing command: " + sContent);
								F::NamedPipe.ExecuteCommand(sContent);
							}
							else if (sMessageType == "LocalBot")
								F::NamedPipe.ProcessLocalBotMessage(sContent);
							else if (sMessageType == "CPCapture")
								F::NamedPipe.ProcessCaptureReservationMessage(sContent);
							else
								F::NamedPipe.Log("Received unknown message type: " + sMessageType);
						}
					}
				}
				CloseHandle(tOverlapped.hEvent);
			}
		}

		if (F::NamedPipe.m_hPipe == INVALID_HANDLE_VALUE)
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		else
			std::this_thread::sleep_for(std::chrono::milliseconds(333));
	}

	{
		std::lock_guard lock(F::NamedPipe.m_messageQueueMutex);
		F::NamedPipe.m_vMessageQueue.clear();
	}

	if (F::NamedPipe.m_hPipe != INVALID_HANDLE_VALUE)
	{
		try
		{
			if (F::NamedPipe.m_iBotId != -1)
			{
				std::string sMessage = std::to_string(F::NamedPipe.m_iBotId) + ":Status:Disconnecting\n";
				DWORD dwBytesWritten = 0;
				WriteFile(F::NamedPipe.m_hPipe, sMessage.c_str(), static_cast<DWORD>(sMessage.length()), &dwBytesWritten, NULL);
			}
		}
		catch (...)
		{
			// Ignore any errors during shutdown
		}

		CloseHandle(F::NamedPipe.m_hPipe);
		F::NamedPipe.m_hPipe = INVALID_HANDLE_VALUE;
	}
	F::NamedPipe.Log("ConnectAndMaintainPipe ended");
		F::NamedPipe.m_pipeThreadRunning.store(false);
}

void CNamedPipe::SendStatusUpdate(std::string sStatus)
{
	QueueMessage("Status", sStatus, true);
	if (m_hPipe != INVALID_HANDLE_VALUE)
		ProcessMessageQueue();
}

void CNamedPipe::ExecuteCommand(std::string sCommand)
{
	Log("ExecuteCommand called with: " + sCommand);

	Log("EngineClient is available, sending command to TF2 console");
	I::EngineClient->ClientCmd_Unrestricted(sCommand.c_str());
	Log("Command sent to TF2 console: " + sCommand);
	SendStatusUpdate("CommandExecuted:" + sCommand);
}

void CNamedPipe::QueueMessage(std::string sType, std::string sContent, bool bIsPriority)
{
	std::lock_guard lock(m_messageQueueMutex);

	if (bIsPriority || m_vMessageQueue.size() < 100)
		m_vMessageQueue.push_back({ sType, sContent, bIsPriority });
	else
	{
		for (auto it = m_vMessageQueue.begin(); it != m_vMessageQueue.end(); ++it)
		{
			if (!it->m_bIsPriority)
			{
				m_vMessageQueue.erase(it);
				m_vMessageQueue.push_back({ sType, sContent, bIsPriority });
				break;
			}
		}
	}
}

void CNamedPipe::ProcessMessageQueue()
{
	if (m_hPipe == INVALID_HANDLE_VALUE)
		return;

	std::lock_guard lock(m_messageQueueMutex);
	if (m_vMessageQueue.empty()) 
		return;

	int processCount = 0;
	auto it = m_vMessageQueue.begin();
	while (it != m_vMessageQueue.end() && processCount < 10)
	{
		std::string sMessage;
		if (m_iBotId != -1)
			sMessage = std::to_string(m_iBotId) + ":" + it->m_sType + ":" + it->m_sContent + "\n";
		else
			sMessage = "0:" + it->m_sType + ":" + it->m_sContent + "\n";

		DWORD dwBytesWritten = 0;
		BOOL bSuccess = WriteFile(m_hPipe, sMessage.c_str(), static_cast<DWORD>(sMessage.length()), &dwBytesWritten, NULL);
		if (bSuccess && dwBytesWritten == sMessage.length())
		{
			it = m_vMessageQueue.erase(it);
			processCount++;
		}
		else
		{
			Log("Failed to write queued message: " + std::to_string(GetLastError()));
			break;
		}
	}
}

bool CNamedPipe::IsLocalBot(uint32_t uAccountID)
{
	if (uAccountID == 0)
		return false;

	std::lock_guard lock(m_localBotsMutex);
	return m_mLocalBots.find(uAccountID) != m_mLocalBots.end();
}

void CNamedPipe::AnnounceCaptureSpotClaim(const std::string& sMap, int iPointIdx, const Vector& vSpot, float flDurationSeconds)
{
	if (sMap.empty() || iPointIdx < 0)
		return;

	const double flExpiry = GetNowSeconds() + flDurationSeconds;
	{
		std::lock_guard lock(m_captureMutex);
		bool bUpdated = false;
		for (auto& reservation : m_vCaptureReservations)
		{
			if (reservation.m_sMap == sMap && reservation.m_iPointIndex == iPointIdx && reservation.m_uOwnerAccountID == tInfo.m_uAccountID)
			{
				reservation.m_vSpot = vSpot;
				reservation.m_flExpiresAt = flExpiry;
				reservation.m_iBotId = m_iBotId;
				bUpdated = true;
				break;
			}
		}
		if (!bUpdated)
		{
			CaptureSpotReservation tReservation{};
			tReservation.m_sMap = sMap;
			tReservation.m_iPointIndex = iPointIdx;
			tReservation.m_vSpot = vSpot;
			tReservation.m_uOwnerAccountID = tInfo.m_uAccountID;
			tReservation.m_iBotId = m_iBotId;
			tReservation.m_flExpiresAt = flExpiry;
			m_vCaptureReservations.emplace_back(tReservation);
		}
	}

	std::ostringstream oss;
	oss << std::fixed << std::setprecision(2) << "Claim|" << sMap << '|' << iPointIdx << '|' << vSpot.x << '|' << vSpot.y << '|' << vSpot.z
		<< '|' << tInfo.m_uAccountID << '|' << flDurationSeconds << '|' << m_iBotId;
	QueueMessage("CPCapture", oss.str(), true);
	if (m_hPipe != INVALID_HANDLE_VALUE)
		ProcessMessageQueue();
}

void CNamedPipe::AnnounceCaptureSpotRelease(const std::string& sMap, int iPointIdx)
{
	if (sMap.empty() || iPointIdx < 0)
		return;

	{
		std::lock_guard lock(m_captureMutex);
		m_vCaptureReservations.erase(std::remove_if(m_vCaptureReservations.begin(), m_vCaptureReservations.end(),
			[&](const CaptureSpotReservation& reservation)
			{
				return reservation.m_sMap == sMap && reservation.m_iPointIndex == iPointIdx && reservation.m_uOwnerAccountID == tInfo.m_uAccountID;
			}), m_vCaptureReservations.end());
	}

	std::ostringstream oss;
	oss << "Release|" << sMap << '|' << iPointIdx << '|' << tInfo.m_uAccountID;
	QueueMessage("CPCapture", oss.str(), true);
	if (m_hPipe != INVALID_HANDLE_VALUE)
		ProcessMessageQueue();
}

std::vector<Vector> CNamedPipe::GetReservedCaptureSpots(const std::string& sMap, int iPointIdx, uint32_t uIgnoreAccountID)
{
	PurgeExpiredCaptureReservations();
	std::vector<Vector> vReserved;
	std::lock_guard lock(m_captureMutex);
	for (const auto& reservation : m_vCaptureReservations)
	{
		if (reservation.m_sMap != sMap || reservation.m_iPointIndex != iPointIdx)
			continue;
		if (uIgnoreAccountID != 0 && reservation.m_uOwnerAccountID == uIgnoreAccountID)
			continue;
		vReserved.push_back(reservation.m_vSpot);
	}
	return vReserved;
}

void CNamedPipe::ProcessLocalBotMessage(std::string sAccountID)
{
	if (!sAccountID.empty())
	{
		try
		{
			uint32_t uAccountID = static_cast<uint32_t>(std::stoull(sAccountID));

			// Don't skip messages from our own bot ID - we might have multiple instances
			// with the same ID running

			{
				std::lock_guard lock(m_localBotsMutex);
				m_mLocalBots[uAccountID] = true;
			}
			Log("Added local bot with friendsID: " + sAccountID);
		}
		catch (const std::exception& e)
		{
			Log("Error processing LocalBot message: " + std::string(e.what()));
		}
	}
}

void CNamedPipe::UpdateLocalBotIgnoreStatus()
{
	// Copy keys under lock to avoid holding lock while tagging
	std::vector<uint32_t> vLocalBotIds;
	{
		std::unique_lock lock(m_localBotsMutex);
		vLocalBotIds.reserve(m_mLocalBots.size());
		for (const auto& kv : m_mLocalBots)
			vLocalBotIds.push_back(kv.first);
		lock.unlock();
	}

	for (const auto& uAccountID : vLocalBotIds)
	{
		if (!F::PlayerUtils.HasTag(uAccountID, F::PlayerUtils.TagToIndex(IGNORED_TAG)) ||
			!F::PlayerUtils.HasTag(uAccountID, F::PlayerUtils.TagToIndex(FRIEND_TAG)))
		{
			const char* szName = I::SteamFriends->GetFriendPersonaName(CSteamID(uAccountID, k_EUniversePublic, k_EAccountTypeIndividual));
			if (!szName) szName = "";

			F::PlayerUtils.AddTag(uAccountID, F::PlayerUtils.TagToIndex(IGNORED_TAG), true, szName);
			F::PlayerUtils.AddTag(uAccountID, F::PlayerUtils.TagToIndex(FRIEND_TAG), true, szName);
			Log("Marked local bot as ignored and friend: " + std::string(szName));
			break;
		}
	}
}

void CNamedPipe::ClearLocalBots()
{
	std::lock_guard lock(m_localBotsMutex);
	m_mLocalBots.clear();
	Log("Cleared local bots list");
}

void CNamedPipe::ClearCaptureReservations()
{
	std::lock_guard lock(m_captureMutex);
	m_vCaptureReservations.clear();
}

void CNamedPipe::PurgeExpiredCaptureReservations()
{
	std::lock_guard lock(m_captureMutex);
	const double flNow = GetNowSeconds();
	m_vCaptureReservations.erase(std::remove_if(m_vCaptureReservations.begin(), m_vCaptureReservations.end(),
		[&](const CaptureSpotReservation& reservation)
		{
			return reservation.m_flExpiresAt < flNow;
		}), m_vCaptureReservations.end());
}

void CNamedPipe::ProcessCaptureReservationMessage(const std::string& sContent)
{
	if (sContent.empty())
		return;

	std::vector<std::string> vTokens;
	{
		std::stringstream ss(sContent);
		std::string sToken;
		while (std::getline(ss, sToken, '|'))
			vTokens.emplace_back(sToken);
	}

	if (vTokens.empty())
		return;

	const std::string& sCommand = vTokens.front();
	if (sCommand == "Claim")
	{
		if (vTokens.size() < 9)
			return;

		const std::string& sMap = vTokens[1];
		int iPointIdx = std::stoi(vTokens[2]);
		Vector vSpot{};
		vSpot.x = std::stof(vTokens[3]);
		vSpot.y = std::stof(vTokens[4]);
		vSpot.z = std::stof(vTokens[5]);
		uint32_t uOwner = static_cast<uint32_t>(std::stoull(vTokens[6]));
		float flDuration = std::stof(vTokens[7]);
		int iBotId = std::stoi(vTokens[8]);

		const double flExpiry = GetNowSeconds() + flDuration;

		{
			std::lock_guard lock(m_captureMutex);
			bool bUpdated = false;
			for (auto& reservation : m_vCaptureReservations)
			{
				if (reservation.m_sMap == sMap && reservation.m_iPointIndex == iPointIdx && reservation.m_uOwnerAccountID == uOwner)
				{
					reservation.m_vSpot = vSpot;
					reservation.m_flExpiresAt = flExpiry;
					reservation.m_iBotId = iBotId;
					bUpdated = true;
					break;
				}
			}

			if (!bUpdated)
			{
				CaptureSpotReservation tReservation{};
				tReservation.m_sMap = sMap;
				tReservation.m_iPointIndex = iPointIdx;
				tReservation.m_vSpot = vSpot;
				tReservation.m_uOwnerAccountID = uOwner;
				tReservation.m_iBotId = iBotId;
				tReservation.m_flExpiresAt = flExpiry;
				m_vCaptureReservations.emplace_back(tReservation);
			}
		}
	}
	else if (sCommand == "Release")
	{
		if (vTokens.size() < 4)
			return;

		const std::string& sMap = vTokens[1];
		int iPointIdx = std::stoi(vTokens[2]);
		uint32_t uOwner = static_cast<uint32_t>(std::stoull(vTokens[3]));

		std::lock_guard lock(m_captureMutex);
		m_vCaptureReservations.erase(std::remove_if(m_vCaptureReservations.begin(), m_vCaptureReservations.end(),
			[&](const CaptureSpotReservation& reservation)
			{
				return reservation.m_sMap == sMap && reservation.m_iPointIndex == iPointIdx && reservation.m_uOwnerAccountID == uOwner;
			}), m_vCaptureReservations.end());
	}
}

std::string CNamedPipe::GetPlayerClassName(int iPlayerClass)
{
	switch (iPlayerClass)
	{
	case 1: return "Scout";
	case 2: return "Sniper";
	case 3: return "Soldier";
	case 4: return "Demoman";
	case 5: return "Medic";
	case 6: return "Heavy";
	case 7: return "Pyro";
	case 8: return "Spy";
	case 9: return "Engineer";
	default: return "N/A";
	}
}

#endif // TEXTMODE