#include "AutoVote.h"

#include "../../Players/PlayerUtils.h"
#include "../NamedPipe/NamedPipe.h"

void CAutoVote::UserMessage(bf_read& msgData)
{
	/*const int iTeam =*/ msgData.ReadByte();
	const int iVoteID = msgData.ReadLong();
	/*const int iCaller =*/ msgData.ReadByte();
	char sReason[256]; msgData.ReadString(sReason, sizeof(sReason));
	char sTarget[256]; msgData.ReadString(sTarget, sizeof(sTarget));
	const int iTarget = msgData.ReadByte() >> 1;
	msgData.Seek(0);

	PlayerInfo_t pi{};
	if (I::EngineClient->GetPlayerInfo(iTarget, &pi))
	{
		if (F::NamedPipe::IsLocalBot(pi.friendsID))
		{
			I::ClientState->SendStringCmd(std::format("vote {} option2", iVoteID).c_str());
			return;
		}
	}
	PlayerInfo_t callerPi{};
	if (I::EngineClient->GetPlayerInfo(iTarget, &callerPi))
	{
		if (F::NamedPipe::IsLocalBot(callerPi.friendsID))
		{
			I::ClientState->SendStringCmd(std::format("vote {} option1", iVoteID).c_str());
			return;
		}
	}

	if (Vars::Misc::Automation::AutoF2Ignored.Value
		&& (F::PlayerUtils.IsIgnored(iTarget)
		|| Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Friends && H::Entities.IsFriend(iTarget)
		|| Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Party && H::Entities.InParty(iTarget)))
	{
		I::ClientState->SendStringCmd(std::format("vote {} option2", iVoteID).c_str());
	}
	else if (Vars::Misc::Automation::AutoF1Priority.Value && F::PlayerUtils.IsPrioritized(iTarget)
		&& !H::Entities.IsFriend(iTarget)
		&& !H::Entities.InParty(iTarget))
	{
		I::ClientState->SendStringCmd(std::format("vote {} option1", iVoteID).c_str());
	}
}