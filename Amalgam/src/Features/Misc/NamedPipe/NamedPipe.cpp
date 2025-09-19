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

#include <windows.h>
#include <thread>
#include <atomic>
#include <sstream>
#include <iostream>
#include <fstream>
#include <string>
#include <unordered_map>
#include <cstdlib>
#include <mutex>
#include <vector>

namespace F::NamedPipe
{
	HANDLE hPipe = INVALID_HANDLE_VALUE;
	std::atomic<bool> shouldRun(true);
	std::thread pipeThread;
	std::ofstream logFile("C:\\pipe_log.txt", std::ios::app);
	int botId = -1;

	const int BASE_RECONNECT_DELAY_MS = 500;
	const int MAX_RECONNECT_DELAY_MS = 10000;
	int currentReconnectAttempts = 0;
	DWORD lastConnectAttemptTime = 0;

	std::mutex messageQueueMutex;
	struct PendingMessage
	{
		std::string type;
		std::string content;
		bool isPriority;
	};
	std::vector<PendingMessage> messageQueue;

	std::unordered_map<uint32_t, bool> localBots;
    std::mutex localBotsMutex;


	void ConnectAndMaintainPipe();
	void QueueMessage(const std::string& type, const std::string& content, bool isPriority);
	void ProcessMessageQueue();
	int GetReconnectDelayMs();

	std::string GetPlayerClassName()
	{
		int playerClass = GetCurrentPlayerClass();
		switch (playerClass)
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

	std::string GetHealthString()
	{
		if (I::EngineClient->IsInGame())
		{
			auto pLocal = H::Entities.GetLocal();
			if (pLocal)
				return std::to_string(pLocal->As<CTFPlayer>()->m_iHealth());
		}
		return "N/A";
	}

	std::string GetServerAddressString()
	{
		if (auto pNetChan = I::EngineClient->GetNetChannelInfo())
		{
			const char* addr = pNetChan->GetAddress();
			if (addr && addr[0] != '\0' && std::string(addr) != "loopback")
				return std::string(addr);
		}
		return "NONE";
	}

	void Log(const std::string& message)
	{
		if (!logFile.is_open())
		{
			std::cerr << "Failed to open log file" << std::endl;
			return;
		}
		logFile << message << std::endl;
		logFile.flush();
		OutputDebugStringA(("NamedPipe: " + message + "\n").c_str());
	}

	const char* PIPE_NAME = "\\\\.\\pipe\\AwootismBotPipe";

	std::string GetErrorMessage(DWORD error)
	{
		char* messageBuffer = nullptr;
		size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
									 NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);
		std::string message(messageBuffer, size);
		LocalFree(messageBuffer);
		return message;
	}

	int GetBotIdFromEnv()
	{
		char* envVal = nullptr;
		size_t len = 0;

		if (_dupenv_s(&envVal, &len, "BOTID") == 0 && envVal)
		{
			int id = atoi(envVal);
			free(envVal);
			if (id > 0)
			{
				Log("Found BOTID environment variable: " + std::to_string(id));
				return id;
			}
		}

		return -1;
	}

	int ReadBotIdFromFile()
	{
		int envId = GetBotIdFromEnv();
		if (envId == -1)
		{
			Log("BOTID environment variable not set. Using fallback ID 1.");
			return 1;
		}
		return envId;
	}

	void Initialize()
	{
		Log("NamedPipe::Initialize() called");
		botId = ReadBotIdFromFile();

		std::stringstream ss;
		if (botId == -1)
		{
			ss << "Failed to read bot ID from file";
			Log(ss.str());
		}
		else
		{
			ss << "Bot ID read from file: " << botId;
			Log(ss.str());
		}

		localBots.clear();
		Log("Cleared local bots list on startup");

		pipeThread = std::thread(ConnectAndMaintainPipe);
		Log("Pipe thread started");
	}

	void Shutdown()
	{
		shouldRun = false;
		if (pipeThread.joinable())
			pipeThread.join();
	}

	void SendStatusUpdate(const std::string& status)
	{
		QueueMessage("Status", status, true);

		if (hPipe != INVALID_HANDLE_VALUE)
			ProcessMessageQueue();
	}

	void ExecuteCommand(const std::string& command)
	{
		Log("ExecuteCommand called with: " + command);

		Log("EngineClient is available, sending command to TF2 console");
		I::EngineClient->ClientCmd_Unrestricted(command.c_str());
		Log("Command sent to TF2 console: " + command);
		SendStatusUpdate("CommandExecuted:" + command);
	}

	int GetCurrentPlayerClass()
	{
		Log("GetCurrentPlayerClass called");
		
		// You should make a caching system and run it in createmove so you dont have to check for in-game status every time
		if (I::EngineClient->IsInGame())
		{
			Log("In game");
			auto pLocal = H::Entities.GetLocal();
			if (pLocal)
			{
				Log("Local player found");
				int playerClass = pLocal->As<CTFPlayer>()->m_iClass();
				Log("Player class: " + std::to_string(playerClass));
				return playerClass;
			}
			else
				Log("Local player not found");
		}
		else
			Log("Not in game");
		
		return -1;
	}

	std::string GetCurrentLevelName()
	{
		return SDK::GetLevelName();
	}

	void BroadcastLocalBotId()
	{
		if (!I::EngineClient->IsInGame())
			return;

		auto pResource = H::Entities.GetResource();
		if (!pResource)
			return;

		int localIdx = I::EngineClient->GetLocalPlayer();
		int maxClients = I::EngineClient->GetMaxClients();
		if (maxClients <= 0 || maxClients > 128) maxClients = 64;
		if (localIdx <= 0 || localIdx > maxClients)
			return;
		if (!pResource->m_bValid(localIdx))
			return;

		uint32_t uAccountID = pResource->m_iAccountID(localIdx);
		if (uAccountID != 0)
		{
			QueueMessage("LocalBot", std::to_string(uAccountID), true);
			Log("Queued local bot ID broadcast: " + std::to_string(uAccountID));

			if (hPipe != INVALID_HANDLE_VALUE)
				ProcessMessageQueue();
		}
	}

	void ProcessLocalBotMessage(const std::string& message)
	{
		std::istringstream iss(message);
		std::string botNumber, messageType, friendsIDstr;
		std::getline(iss, botNumber, ':');
		std::getline(iss, messageType, ':');
		std::getline(iss, friendsIDstr);

		if (messageType == "LocalBot" && !friendsIDstr.empty())
		{
			try
			{
				uint32_t friendsID = static_cast<uint32_t>(std::stoull(friendsIDstr));

				// Don't skip messages from our own bot ID - we might have multiple instances
				// with the same ID running

				{
					std::lock_guard<std::mutex> lk(localBotsMutex);
					localBots[friendsID] = true;
				}
				Log("Added local bot with friendsID: " + friendsIDstr);

				bool tagAdded = false;
				auto engine = I::EngineClient;
				if (engine->IsInGame())
				{
					if (auto pResource = H::Entities.GetResource())
					{
						int maxClients = engine->GetMaxClients();
						if (maxClients <= 0 || maxClients > 128) maxClients = 64;
						for (int i = 1; i <= maxClients; i++)
						{
							if (!pResource->m_bValid(i))
								continue;
							if (pResource->m_iAccountID(i) == friendsID)
							{
								const char* szName = pResource->m_szName(i);
								if (!szName) szName = "";
								F::PlayerUtils.AddTag(friendsID, F::PlayerUtils.TagToIndex(IGNORED_TAG), true, szName);
								F::PlayerUtils.AddTag(friendsID, F::PlayerUtils.TagToIndex(FRIEND_TAG), true, szName);
								Log("Marked local bot as ignored and friend: " + std::string(szName));
								tagAdded = true;
								break;
							}
						}
					}
				}

				if (!tagAdded)
					Log("Could not find player info for friendsID: " + friendsIDstr + " to add tags");
			}
			catch (const std::exception& e)
			{
				Log("Error processing LocalBot message: " + std::string(e.what()));
			}
		}
	}

	bool IsLocalBot(uint32_t friendsID)
	{
		if (friendsID == 0)
			return false;

		std::lock_guard<std::mutex> lk(localBotsMutex);
		return localBots.find(friendsID) != localBots.end();
	}

	void UpdateLocalBotIgnoreStatus()
	{
		BroadcastLocalBotId();

		auto engine = I::EngineClient;
		if (!engine->IsInGame())
			return;

		auto pResource = H::Entities.GetResource();
		if (!pResource)
			return;

		int maxClients = engine->GetMaxClients();
		if (maxClients <= 0 || maxClients > 128) maxClients = 64;

		// Copy keys under lock to avoid holding lock while tagging
		std::vector<uint32_t> localBotIds;
		{
			std::lock_guard<std::mutex> lk(localBotsMutex);
			localBotIds.reserve(localBots.size());
			for (const auto& kv : localBots)
				localBotIds.push_back(kv.first);
		}

		for (const auto& friendsID : localBotIds)
		{
			if (!F::PlayerUtils.HasTag(friendsID, F::PlayerUtils.TagToIndex(IGNORED_TAG)) ||
				!F::PlayerUtils.HasTag(friendsID, F::PlayerUtils.TagToIndex(FRIEND_TAG)))
			{
				for (int i = 1; i <= maxClients; ++i)
				{
					if (!engine->IsInGame())
						return;

					if (pResource->m_bValid(i) && pResource->m_iAccountID(i) == friendsID)
					{
						const char* szName = pResource->m_szName(i);
						if (!szName) szName = "";
						F::PlayerUtils.AddTag(friendsID, F::PlayerUtils.TagToIndex(IGNORED_TAG), true, szName);
						F::PlayerUtils.AddTag(friendsID, F::PlayerUtils.TagToIndex(FRIEND_TAG), true, szName);
						Log("Marked local bot as ignored and friend: " + std::string(szName));
						break;
					}
				}
			}
		}
	}

	void ClearLocalBots()
	{
		std::lock_guard<std::mutex> lk(localBotsMutex);
		localBots.clear();
		Log("Cleared local bots list");
	}

	void QueueMessage(const std::string& type, const std::string& content, bool isPriority = false)
	{
		std::lock_guard<std::mutex> lock(messageQueueMutex);

		if (isPriority || messageQueue.size() < 100)
			messageQueue.push_back({ type, content, isPriority });
		else
		{
			for (auto it = messageQueue.begin(); it != messageQueue.end(); ++it)
			{
				if (!it->isPriority)
				{
					messageQueue.erase(it);
					messageQueue.push_back({ type, content, isPriority });
					break;
				}
			}
		}
	}

	void ProcessMessageQueue()
	{
		if (hPipe == INVALID_HANDLE_VALUE) return;

		std::lock_guard<std::mutex> lock(messageQueueMutex);
		if (messageQueue.empty()) return;

		int processCount = 0;
		auto it = messageQueue.begin();
		while (it != messageQueue.end() && processCount < 10)
		{
			std::string message;
			if (botId != -1)
				message = std::to_string(botId) + ":" + it->type + ":" + it->content + "\n";
			else
				message = "0:" + it->type + ":" + it->content + "\n";

			DWORD bytesWritten = 0;
			BOOL success = WriteFile(hPipe, message.c_str(), static_cast<DWORD>(message.length()), &bytesWritten, NULL);

			if (success && bytesWritten == message.length())
			{
				it = messageQueue.erase(it);
				processCount++;
			}
			else
			{
				Log("Failed to write queued message: " + std::to_string(GetLastError()));
				break;
			}
		}
	}

	int GetReconnectDelayMs()
	{
		int delay = std::min(
			BASE_RECONNECT_DELAY_MS * (1 << std::min(currentReconnectAttempts, 10)),
			MAX_RECONNECT_DELAY_MS
		);

		int jitter = delay * 0.2 * (static_cast<double>(rand()) / RAND_MAX - 0.5);
		return delay + jitter;
	}

	void ConnectAndMaintainPipe()
	{
		Log("ConnectAndMaintainPipe started");
		srand(static_cast<unsigned int>(time(nullptr)));

		while (shouldRun)
		{
			DWORD currentTime = GetTickCount();

			if (hPipe == INVALID_HANDLE_VALUE)
			{

				int reconnectDelay = GetReconnectDelayMs();
				if (currentTime - lastConnectAttemptTime > static_cast<DWORD>(reconnectDelay) || lastConnectAttemptTime == 0)
				{
					lastConnectAttemptTime = currentTime;
					currentReconnectAttempts++;

					Log("Attempting to connect to pipe (attempt " + std::to_string(currentReconnectAttempts) +
						", delay: " + std::to_string(reconnectDelay) + "ms)");


					OVERLAPPED overlapped = { 0 };
					overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

					if (overlapped.hEvent == NULL)
					{
						Log("Failed to create connection event: " + std::to_string(GetLastError()));
						std::this_thread::sleep_for(std::chrono::milliseconds(reconnectDelay));
						continue;
					}

					hPipe = CreateFile(
						PIPE_NAME,
						GENERIC_READ | GENERIC_WRITE,
						0,
						NULL,
						OPEN_EXISTING,
						FILE_FLAG_OVERLAPPED,
						NULL);

					if (hPipe != INVALID_HANDLE_VALUE)
					{

						currentReconnectAttempts = 0;
						Log("Connected to pipe");


						DWORD pipeMode = PIPE_READMODE_MESSAGE;
						SetNamedPipeHandleState(hPipe, &pipeMode, NULL, NULL);


						QueueMessage("Status", "Connected", true);
						ProcessMessageQueue();

						if (botId != -1)
							Log("Using Bot ID: " + std::to_string(botId));
						else
							Log("Warning: Bot ID not set");

						ClearLocalBots();
					}
					else
					{
						DWORD error = GetLastError();
						Log("Failed to connect to pipe: " + std::to_string(error) + " - " + GetErrorMessage(error));
					}

					CloseHandle(overlapped.hEvent);
				}
				else
					std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}

			if (hPipe != INVALID_HANDLE_VALUE)
			{

				DWORD bytesAvail = 0;
				if (!PeekNamedPipe(hPipe, NULL, 0, NULL, &bytesAvail, NULL))
				{
					DWORD error = GetLastError();
					if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED || error == ERROR_NO_DATA)
					{
						Log("Pipe disconnected: " + std::to_string(error) + " - " + GetErrorMessage(error));
						CloseHandle(hPipe);
						hPipe = INVALID_HANDLE_VALUE;
						continue;
					}
				}


				ProcessMessageQueue();


				QueueMessage("Status", "Heartbeat", true);
				ProcessMessageQueue();


				static int updateCounter = 0;
				if (++updateCounter % 3 == 0)
				{
					// Map info
					std::string mapName = GetCurrentLevelName();
					if (mapName.empty()) mapName = "N/A";
					QueueMessage("Map", mapName, false);

					// Server address
					QueueMessage("Server", GetServerAddressString(), false);

					// Player class and health
					QueueMessage("PlayerClass", GetPlayerClassName(), false);
					QueueMessage("Health", GetHealthString(), false);

					ProcessMessageQueue();

					if (I::EngineClient->IsInGame())
						UpdateLocalBotIgnoreStatus();
					else
					{
						// Not in game: ensure panel receives disconnect-ish state
						QueueMessage("Status", "NotInGame", true);
						ProcessMessageQueue();
					}
				}


				char buffer[4096] = { 0 };
				DWORD bytesRead = 0;


				OVERLAPPED overlapped = { 0 };
				overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

				if (overlapped.hEvent != NULL)
				{

					if (bytesAvail > 0)
					{
						BOOL readSuccess = ReadFile(hPipe, buffer, sizeof(buffer) - 1, &bytesRead, &overlapped);

						if (!readSuccess && GetLastError() == ERROR_IO_PENDING)
						{
							DWORD waitResult = WaitForSingleObject(overlapped.hEvent, 1000);

							if (waitResult == WAIT_OBJECT_0)
							{
								if (!GetOverlappedResult(hPipe, &overlapped, &bytesRead, FALSE))
								{
									Log("GetOverlappedResult failed: " + std::to_string(GetLastError()));
									CloseHandle(overlapped.hEvent);
									CloseHandle(hPipe);
									hPipe = INVALID_HANDLE_VALUE;
									continue;
								}
							}
							else if (waitResult == WAIT_TIMEOUT)
							{
								CancelIo(hPipe);
								CloseHandle(overlapped.hEvent);
								continue;
							}
							else
							{
								Log("Wait failed: " + std::to_string(GetLastError()));
								CloseHandle(overlapped.hEvent);
								CloseHandle(hPipe);
								hPipe = INVALID_HANDLE_VALUE;
								continue;
							}
						}
						else if (!readSuccess)
						{
							Log("ReadFile failed immediately: " + std::to_string(GetLastError()));
							CloseHandle(overlapped.hEvent);
							CloseHandle(hPipe);
							hPipe = INVALID_HANDLE_VALUE;
							continue;
						}


						if (bytesRead > 0)
						{
							buffer[bytesRead] = '\0'; // Ensure null termination
							std::string message(buffer, bytesRead);
							Log("Received message: " + message);


							std::stringstream ss(message);
							std::string line;

							while (std::getline(ss, line))
							{
								if (line.empty()) continue;


								std::istringstream iss(line);
								std::string botNumber, messageType, content;
								std::getline(iss, botNumber, ':');
								std::getline(iss, messageType, ':');
								std::getline(iss, content);

								if (messageType == "Command")
								{
									Log("Executing command: " + content);
									ExecuteCommand(content);
								}
								else if (messageType == "LocalBot")
									ProcessLocalBotMessage(line);
								else
									Log("Received unknown message type: " + messageType);
							}
						}
					}

					CloseHandle(overlapped.hEvent);
				}
			}


			if (hPipe == INVALID_HANDLE_VALUE)
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			else
				std::this_thread::sleep_for(std::chrono::milliseconds(333));
		}


		{
			std::lock_guard<std::mutex> lock(messageQueueMutex);
			messageQueue.clear();
		}

		if (hPipe != INVALID_HANDLE_VALUE)
		{

			try
			{
				if (botId != -1)
				{
					std::string message = std::to_string(botId) + ":Status:Disconnecting\n";
					DWORD bytesWritten = 0;
					WriteFile(hPipe, message.c_str(), static_cast<DWORD>(message.length()), &bytesWritten, NULL);
				}
			}
			catch (...)
			{
				// Ignore any errors during shutdown
			}

			CloseHandle(hPipe);
			hPipe = INVALID_HANDLE_VALUE;
		}
		Log("ConnectAndMaintainPipe ended");
	}
}

#endif // TEXTMODE