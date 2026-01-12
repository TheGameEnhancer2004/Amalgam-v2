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
	msgData.Reset();

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