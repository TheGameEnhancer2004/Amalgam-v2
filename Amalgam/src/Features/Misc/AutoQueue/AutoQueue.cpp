#include "AutoQueue.h"
#include "../../Players/PlayerUtils.h"

void CAutoQueue::Run()
{
	static float flLastQueueTime = 0.0f;
	static bool bQueuedOnce = false;
	static bool bWasInGame = false;
	static bool bWasDisconnected = false;
	
	if (!Vars::Misc::Queueing::AutoCasualQueue.Value)
	{
		bQueuedOnce = false;
		flLastQueueTime = 0.0f;
		return;
	}

	if (I::TFPartyClient->BInQueueForMatchGroup(k_eTFMatchGroup_Casual_Default))
		return;

	bool bInGame = I::EngineClient->IsInGame();
	bool bIsLoadingMap = I::EngineClient->IsDrawingLoadingImage();

	if (bIsLoadingMap && Vars::Misc::Queueing::RQLTM.Value)
		return;

	float flCurrentTime = I::EngineClient->Time();
	float flQueueDelay = Vars::Misc::Queueing::QueueDelay.Value * 60.0f; 
	
	if (bWasInGame && !bInGame && !bIsLoadingMap)
	{
		bWasDisconnected = true;
		
		if (Vars::Misc::Queueing::RQif.Value && Vars::Misc::Queueing::RQkick.Value)
		{
			flLastQueueTime = 0.0f;
		}
	}
	
	bWasInGame = bInGame;

	if (auto pResource = H::Entities.GetResource())
	{
		if (bInGame && Vars::Misc::Queueing::RQif.Value)
		{
			int nPlayerCount = 0;

			for (int i = 1; i <= I::EngineClient->GetMaxClients(); i++)
			{
				if (pResource->m_iUserID(i) != -1)
				{
					bool bShouldCount = true;

					if (Vars::Misc::Queueing::RQIgnoreFriends.Value)
					{
						uint32_t uFriendsID = pResource->m_iAccountID(i);

						if (H::Entities.IsFriend(uFriendsID) ||
							H::Entities.InParty(uFriendsID) ||
							F::PlayerUtils.HasTag(uFriendsID, F::PlayerUtils.TagToIndex(FRIEND_TAG)) ||
							F::PlayerUtils.HasTag(uFriendsID, F::PlayerUtils.TagToIndex(FRIEND_IGNORE_TAG)) ||
							F::PlayerUtils.HasTag(uFriendsID, F::PlayerUtils.TagToIndex(IGNORED_TAG)) ||
							F::PlayerUtils.HasTag(uFriendsID, F::PlayerUtils.TagToIndex(BOT_IGNORE_TAG)) ||
							F::PlayerUtils.HasTag(uFriendsID, F::PlayerUtils.TagToIndex(PARTY_TAG)))
						{
							bShouldCount = false;
						}
					}

					if (bShouldCount)
						nPlayerCount++;
				}
			}

			if (nPlayerCount < Vars::Misc::Queueing::RQplt.Value)
			{
				I::TFGCClientSystem->AbandonCurrentMatch();
				bWasInGame = false;
				bWasDisconnected = true;
				flLastQueueTime = 0.0f;
			}
		}
	}

	bool bShouldQueue = !bQueuedOnce || (flCurrentTime - flLastQueueTime >= flQueueDelay);
	
	if (bShouldQueue && !bInGame && !bIsLoadingMap)
	{
		static bool bHasLoaded = false;
		if (!bHasLoaded)
		{
			I::TFPartyClient->LoadSavedCasualCriteria();
			bHasLoaded = true;
		}
		
		I::TFPartyClient->RequestQueueForMatch(k_eTFMatchGroup_Casual_Default);
		flLastQueueTime = flCurrentTime;
		bQueuedOnce = true;
		bWasDisconnected = false;
	}
}