//
//
//
// TODO: REFACTOR THIS DOGSHIT
// TODO: REFACTOR THIS DOGSHIT
// TODO: REFACTOR THIS DOGSHIT
//
//
//
#include "NamedPipe.h"
#include "../../../SDK/SDK.h"
#include "../../Players/PlayerUtils.h"
#include "../../Configs/Configs.h"

#include <windows.h>
#include <thread>
#include <atomic>
#include <sstream>
#include <iostream>
#include <fstream>
#include <string>
#include <regex>
#include <unordered_map>

namespace F::NamedPipe
{
    HANDLE hPipe = INVALID_HANDLE_VALUE;
    std::atomic<bool> shouldRun(true);
    std::thread pipeThread;
    std::ofstream logFile("C:\\pipe_log.txt", std::ios::app);
    int botId = -1;
    

    std::unordered_map<uint32_t, bool> localBots;
    

    void ConnectAndMaintainPipe();

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

    std::string GetTF2Folder()
    {
        std::string configPath = F::Configs.m_sConfigPath;
        size_t pos = configPath.find_last_of("\\");
        if (pos != std::string::npos)
            return configPath.substr(0, pos);
            
        char modulePath[MAX_PATH];
        GetModuleFileNameA(nullptr, modulePath, MAX_PATH);
        
        std::string path = modulePath;
        pos = path.find_last_of("\\");
        if (pos != std::string::npos)
            path = path.substr(0, pos);
            
        return path;
    }

    int ReadBotIdFromFile()
    {
        std::string tf2Folder = GetTF2Folder();
        std::regex botFileRegex("bot(\\d+)\\.txt");
        
        WIN32_FIND_DATA findFileData;
        HANDLE hFind = FindFirstFile((tf2Folder + "\\*.txt").c_str(), &findFileData);

        if (hFind != INVALID_HANDLE_VALUE) 
        {
            do 
            {
                std::string filename(findFileData.cFileName);
                std::smatch match;
                if (std::regex_match(filename, match, botFileRegex))
                {
                    FindClose(hFind);
                    return std::stoi(match[1]);
                }
            } while (FindNextFile(hFind, &findFileData) != 0);
            FindClose(hFind);
        }
        return -1;
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
        {
            pipeThread.join();
        }
    }

    void SendStatusUpdate(const std::string& status)
    {
        if (hPipe == INVALID_HANDLE_VALUE || botId == -1)
        {
            return;
        }

        std::string message = std::to_string(botId) + ":" + status;
        DWORD bytesWritten;
        WriteFile(hPipe, message.c_str(), static_cast<DWORD>(message.length()), &bytesWritten, NULL);
    }

    void ExecuteCommand(const std::string& command)
    {
        Log("ExecuteCommand called with: " + command);
        
        if (command == "debug navbot")
        {
            Log("Debug navbot command received, sending 'kill' to TF2 console");
            if (I::EngineClient)
            {
                I::EngineClient->ClientCmd_Unrestricted("kill");
                Log("'kill' command sent to TF2 console");
                SendStatusUpdate("CommandExecuted:kill");
            }
            else
            {
                Log("Error: EngineClient is not available. 'kill' command not executed");
                SendStatusUpdate("CommandFailed:kill");
            }
            return;
        }
        
        if (command.substr(0, 11) == "loadconfig ")
        {
            std::string configName = command.substr(11);
            Log("Loading config: " + configName);
            
            if (F::Configs.LoadConfig(configName, true))
            {
                Log("Config loaded successfully: " + configName);
                SendStatusUpdate("ConfigLoaded:" + configName);
            }
            else
            {
                Log("Failed to load config: " + configName);
                SendStatusUpdate("ConfigLoadFailed:" + configName);
            }
        }
        else if (I::EngineClient)
        {
            Log("EngineClient is available, sending command to TF2 console");
            I::EngineClient->ClientCmd_Unrestricted(command.c_str());
            Log("Command sent to TF2 console: " + command);
            SendStatusUpdate("CommandExecuted:" + command);
        }
        else
        {
            Log("Error: EngineClient is not available. Command not executed: " + command);
            SendStatusUpdate("CommandFailed:" + command);
        }
    }

    void SendHealthUpdate(int health)
    {
        if (hPipe == INVALID_HANDLE_VALUE || botId == -1)
        {
            return;
        }

        std::string message = std::to_string(botId) + ":Health:" + std::to_string(health);
        DWORD bytesWritten;
        WriteFile(hPipe, message.c_str(), static_cast<DWORD>(message.length()), &bytesWritten, NULL);
    }

    int GetCurrentPlayerClass()
    {
        Log("GetCurrentPlayerClass called");
        if (I::EngineClient && I::EngineClient->IsInGame())
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
            {
                Log("Local player not found");
            }
        }
        else
        {
            Log("Not in game or EngineClient not available");
        }
        return -1;
    }

    void SendPlayerClassUpdate(int playerClass)
    {
        if (hPipe == INVALID_HANDLE_VALUE || botId == -1)
        {
            return;
        }

        std::string classString;
        switch (playerClass)
        {
            case 1: classString = "Scout"; break;
            case 2: classString = "Sniper"; break;
            case 3: classString = "Soldier"; break;
            case 4: classString = "Demoman"; break;
            case 5: classString = "Medic"; break;
            case 6: classString = "Heavy"; break;
            case 7: classString = "Pyro"; break;
            case 8: classString = "Spy"; break;
            case 9: classString = "Engineer"; break;
            default: classString = "Unknown";
        }

        std::string message = std::to_string(botId) + ":PlayerClass:" + classString + "\n";
        DWORD bytesWritten;
        WriteFile(hPipe, message.c_str(), static_cast<DWORD>(message.length()), &bytesWritten, NULL);
        Log("Sent player class update: " + classString);
    }

    std::string GetCurrentLevelName()
    {
        return SDK::GetLevelName();
    }

    void SendMapUpdate()
    {
        if (hPipe == INVALID_HANDLE_VALUE || botId == -1)
        {
            return;
        }

        std::string mapName = GetCurrentLevelName();
        std::string message = std::to_string(botId) + ":Map:" + mapName + "\n";
        DWORD bytesWritten;
        WriteFile(hPipe, message.c_str(), static_cast<DWORD>(message.length()), &bytesWritten, NULL);
        Log("Sent map update: " + mapName);
    }

    void SendServerInfo()
    {
        if (hPipe == INVALID_HANDLE_VALUE || botId == -1)
        {
            return;
        }

        std::string serverInfo = "0.0.0.0";
        std::string message = std::to_string(botId) + ":ServerInfo:" + serverInfo + "\n";
        DWORD bytesWritten;
        WriteFile(hPipe, message.c_str(), static_cast<DWORD>(message.length()), &bytesWritten, NULL);
        Log("Sent server info update: " + serverInfo);
    }

    void UpdateBotInfo()
    {
        SendPlayerClassUpdate(GetCurrentPlayerClass());
        SendMapUpdate();
        SendServerInfo();
    }

    void BroadcastLocalBotId()
    {
        if (hPipe == INVALID_HANDLE_VALUE || botId == -1)
        {
            return;
        }

        auto pLocal = H::Entities.GetLocal();
        if (!pLocal)
        {
            return;
        }

        PlayerInfo_t pi{};
        int localIndex = I::EngineClient->GetLocalPlayer();
        if (I::EngineClient->GetPlayerInfo(localIndex, &pi))
        {
            std::string message = std::to_string(botId) + ":LocalBot:" + std::to_string(pi.friendsID) + "\n";
            DWORD bytesWritten;
            WriteFile(hPipe, message.c_str(), static_cast<DWORD>(message.length()), &bytesWritten, NULL);
            Log("Broadcasted local bot ID: " + std::to_string(pi.friendsID));
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
            uint32_t friendsID = std::stoull(friendsIDstr);
            
            if (std::stoi(botNumber) == botId)
            {
                return;
            }
            
            localBots[friendsID] = true;
            Log("Added local bot with friendsID: " + friendsIDstr);
            
            PlayerInfo_t pi{};
            for (int i = 1; i <= I::EngineClient->GetMaxClients(); i++)
            {
                if (I::EngineClient->GetPlayerInfo(i, &pi) && pi.friendsID == friendsID)
                {
                    F::PlayerUtils.AddTag(friendsID, F::PlayerUtils.TagToIndex(IGNORED_TAG), true, pi.name);
                    Log("Marked local bot as ignored: " + std::string(pi.name));
                    break;
                }
            }
        }
    }

    bool IsLocalBot(uint32_t friendsID)
    {
        if (friendsID == 0)
            return false;
            
        return localBots.find(friendsID) != localBots.end();
    }

    void UpdateLocalBotIgnoreStatus()
    {
        BroadcastLocalBotId();
        
        for (const auto& [friendsID, isLocal] : localBots)
        {
            if (!F::PlayerUtils.HasTag(friendsID, F::PlayerUtils.TagToIndex(IGNORED_TAG)))
            {
                PlayerInfo_t pi{};
                for (int i = 1; i <= I::EngineClient->GetMaxClients(); i++)
                {
                    if (I::EngineClient->GetPlayerInfo(i, &pi) && pi.friendsID == friendsID)
                    {
                        F::PlayerUtils.AddTag(friendsID, F::PlayerUtils.TagToIndex(IGNORED_TAG), true, pi.name);
                        Log("Marked local bot as ignored: " + std::string(pi.name));
                        break;
                    }
                }
            }
        }
    }

    void ClearLocalBots()
    {
        localBots.clear();
        Log("Cleared local bots list");
    }

    void ConnectAndMaintainPipe()
    {
        Log("ConnectAndMaintainPipe started");
        while (shouldRun)
        {
            if (hPipe == INVALID_HANDLE_VALUE)
            {
                Log("Attempting to connect to pipe");
                hPipe = CreateFile(
                    PIPE_NAME,
                    GENERIC_READ | GENERIC_WRITE,
                     0,
                    NULL,
                    OPEN_EXISTING,
                    0,
                    NULL);

                if (hPipe != INVALID_HANDLE_VALUE)
                {
                    Log("Connected to pipe");
                    SendStatusUpdate("Connected");

                    if (botId != -1)
                    {
                        Log("Using Bot ID: " + std::to_string(botId));
                    }
                    else
                    {
                        Log("Warning: Bot ID not set");
                    }
                    
                    ClearLocalBots();
                }
                else
                {
                    DWORD error = GetLastError();
                    Log("Failed to connect to pipe: " + std::to_string(error) + " - " + GetErrorMessage(error));
                }
            }

            if (hPipe != INVALID_HANDLE_VALUE)
            {
                SendStatusUpdate("Heartbeat");
                SendPlayerClassUpdate(GetCurrentPlayerClass());
                SendMapUpdate();
                SendServerInfo();
                
                if (I::EngineClient && I::EngineClient->IsInGame())
                {
                    UpdateLocalBotIgnoreStatus();
                }
                
                char buffer[1024];
                DWORD bytesRead;
                DWORD bytesAvail = 0;

                if (PeekNamedPipe(hPipe, NULL, 0, NULL, &bytesAvail, NULL) && bytesAvail > 0)
                {
                    if (ReadFile(hPipe, buffer, sizeof(buffer), &bytesRead, NULL))
                    {
                        std::string message(buffer, bytesRead);
                        Log("Received message: " + message);
                        std::istringstream iss(message);
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
                        {
                            ProcessLocalBotMessage(message);
                        }
                        else
                        {
                            Log("Received unknown message type: " + messageType);
                        }
                    }
                    else
                    {
                        Log("ReadFile failed: " + std::to_string(GetLastError()));
                        CloseHandle(hPipe);
                        hPipe = INVALID_HANDLE_VALUE;
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }

        if (hPipe != INVALID_HANDLE_VALUE)
        {
            CloseHandle(hPipe);
            hPipe = INVALID_HANDLE_VALUE;
        }
        Log("ConnectAndMaintainPipe ended");
    }
} 