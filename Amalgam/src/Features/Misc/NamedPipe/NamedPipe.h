#pragma once

#ifdef TEXTMODE

#include "../../../SDK/SDK.h"
#include <string>

namespace F::NamedPipe
{
    void Initialize();
    void Shutdown();
    void SendStatusUpdate(const std::string& status);
    void ExecuteCommand(const std::string& command);
    int GetCurrentPlayerClass();
    std::string GetCurrentLevelName();
    int ReadBotIdFromFile();

    // Local bot tracking
    void BroadcastLocalBotId();
    void ProcessLocalBotMessage(const std::string& message);
    bool IsLocalBot(uint32_t friendsID);
    void UpdateLocalBotIgnoreStatus();
    void ClearLocalBots();
}

#endif