#include "AutoVote.h"

#include "../../Players/PlayerUtils.h"
#include "../../Misc/NamedPipe/NamedPipe.h"
#include "../Misc.h"
#include <fstream>
#include <filesystem>

void CAutoVote::UserMessage(bf_read& msgData)
{
	/*const int iTeam =*/ msgData.ReadByte();
	const int iVoteID = msgData.ReadLong();
	const int iCaller = msgData.ReadByte();
	char sReason[256]; msgData.ReadString(sReason, sizeof(sReason));
	char sTarget[256]; msgData.ReadString(sTarget, sizeof(sTarget));
	const int iTarget = msgData.ReadByte() >> 1;
	msgData.Seek(0);

	if (Vars::Misc::Automation::ChatSpam::VoteKickReply.Value)
	{
		if (m_vF1Messages.empty() && m_vF2Messages.empty())
		{
			std::string sPath = I::EngineClient->GetGameDirectory();
			sPath += "\\Amalgam\\votekick.txt";

			if (!std::filesystem::exists(sPath))
			{
				std::ofstream newFile(sPath);
				if (newFile.good())
				{
					newFile << "// Vote Kick Reply Configuration\n";
					newFile << "// Format: F1: message (supports {target})\n";
					newFile << "// Format: F2: message (supports {target})\n";
					newFile << "F1: F1! Kick {target}!\n";
					newFile << "F1: bye bye {target}\n";
					newFile << "F2: F2! Don't kick {target}!\n";
					newFile << "F2: {target} is innocent!\n";
				}
			}

			std::ifstream file(sPath);
			if (file.good())
			{
				std::string line;
				while (std::getline(file, line))
				{
					if (line.empty() || line.find("//") == 0) continue;

					if (line.find("F1:") == 0)
					{
						std::string msg = line.substr(3);
						if (msg.find_first_not_of(" \t") != std::string::npos)
							msg.erase(0, msg.find_first_not_of(" \t"));
						if (!msg.empty()) m_vF1Messages.push_back(msg);
					}
					else if (line.find("F2:") == 0)
					{
						std::string msg = line.substr(3);
						if (msg.find_first_not_of(" \t") != std::string::npos)
							msg.erase(0, msg.find_first_not_of(" \t"));
						if (!msg.empty()) m_vF2Messages.push_back(msg);
					}
				}
			}
		}

		bool bTargetIsFriend = H::Entities.IsFriend(iTarget) || H::Entities.InParty(iTarget) || F::PlayerUtils.IsIgnored(iTarget);
		bool bCallerIsFriend = H::Entities.IsFriend(iCaller) || H::Entities.InParty(iCaller) || F::PlayerUtils.IsIgnored(iCaller);

		std::string sReply = "";

		if (bTargetIsFriend && iCaller != I::EngineClient->GetLocalPlayer())
		{
			if (!m_vF2Messages.empty())
			{
				int index = SDK::RandomInt(0, static_cast<int>(m_vF2Messages.size()) - 1);
				sReply = m_vF2Messages[index];
			}
		}
		else if (bCallerIsFriend && iTarget != I::EngineClient->GetLocalPlayer())
		{
			if (!m_vF1Messages.empty())
			{
				int index = SDK::RandomInt(0, static_cast<int>(m_vF1Messages.size()) - 1);
				sReply = m_vF1Messages[index];
			}
		}

		if (!sReply.empty())
		{
			sReply = F::Misc.ReplaceTags(sReply, sTarget);
			I::EngineClient->ClientCmd_Unrestricted(std::format("say {}", sReply).c_str());
		}
	}

#ifdef TEXTMODE
	if (auto pResource = H::Entities.GetResource(); pResource)
	{
		if (F::NamedPipe.IsLocalBot(pResource->m_iAccountID(iTarget)))
		{
			I::ClientState->SendStringCmd(std::format("vote {} option2", iVoteID).c_str());
			return;
		}

		if (F::NamedPipe.IsLocalBot(pResource->m_iAccountID(iCaller)))
		{
			I::ClientState->SendStringCmd(std::format("vote {} option1", iVoteID).c_str());
			return;
		}
	}
#endif

if (F::PlayerUtils.IsIgnored(iCaller))
{
    I::ClientState->SendStringCmd(std::format("vote {} option1", iVoteID).c_str());
    return;
}

if (Vars::Misc::Automation::AutoF2Ignored.Value
    && (F::PlayerUtils.IsIgnored(iTarget)
    || H::Entities.IsFriend(iTarget)
    || H::Entities.InParty(iTarget)))
{
    I::ClientState->SendStringCmd(std::format("vote {} option2", iVoteID).c_str());
    return;
}


	if (Vars::Misc::Automation::AutoF1Priority.Value && F::PlayerUtils.IsPrioritized(iTarget)
		&& !H::Entities.IsFriend(iTarget)
		&& !H::Entities.InParty(iTarget))
	{
		I::ClientState->SendStringCmd(std::format("vote {} option1", iVoteID).c_str());
		return;
	}
}