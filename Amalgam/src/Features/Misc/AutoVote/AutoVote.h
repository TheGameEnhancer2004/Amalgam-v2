#pragma once
#include "../../../SDK/SDK.h"

class CAutoVote
{
public:
	void UserMessage(bf_read& msgData);

private:
	std::vector<std::string> m_vF1Messages;
	std::vector<std::string> m_vF2Messages;
};

ADD_FEATURE(CAutoVote, AutoVote);