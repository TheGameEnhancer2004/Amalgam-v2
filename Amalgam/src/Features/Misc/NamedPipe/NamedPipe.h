#pragma once

#include "../../../SDK/SDK.h"
#include "../../Configs/Configs.h"
#include <string>

namespace F::NamedPipe
{
    void Initialize();
    void Shutdown();
    void SendStatusUpdate(const std::string& status);
    void ExecuteCommand(const std::string& command);
    void SendHealthUpdate(int health);
    void SendPlayerClassUpdate(int playerClass);
    int GetCurrentPlayerClass();
    std::string GetCurrentLevelName();
    void SendMapUpdate();
    void SendServerInfo();
    int ReadBotIdFromFile();

    // Local bot tracking
    void BroadcastLocalBotId();
    void ProcessLocalBotMessage(const std::string& message);
    bool IsLocalBot(uint32_t friendsID);
    void UpdateLocalBotIgnoreStatus();
    void ClearLocalBots();
} 