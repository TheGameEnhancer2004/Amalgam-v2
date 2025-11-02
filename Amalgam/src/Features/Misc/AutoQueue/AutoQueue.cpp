#include "AutoQueue.h"
#include "../../Players/PlayerUtils.h"
#include "../../NavBot/NavEngine/NavEngine.h"
#include "../Misc.h"
#ifdef TEXTMODE
#include "../NamedPipe/NamedPipe.h"
#endif

void CAutoQueue::Run()
{
	static float flLastQueueTime = 0.0f;
	static bool bQueuedOnce = false;
	static bool bWasInGame = false;
	static bool bWasDisconnected = false;
	static bool bQueuedFromRQif = false;

	const bool bInGameNow = I::EngineClient->IsInGame();
	const bool bIsLoadingMapNow = I::EngineClient->IsDrawingLoadingImage();
	const char* pszLevelName = I::EngineClient->GetLevelName();
	const std::string sLevelName = pszLevelName ? pszLevelName : "";

	if (sLevelName != m_sLastLevelName)
	{
		m_sLastLevelName = sLevelName;
		m_bNavmeshAbandonTriggered = false;
		m_bAutoDumpedThisMatch = false;
		m_flAutoDumpStartTime = 0.0f;
	}

	if (!Vars::Misc::Queueing::AutoAbandonIfNoNavmesh.Value)
		m_bNavmeshAbandonTriggered = false;
	else if (bInGameNow && !bIsLoadingMapNow && !m_bNavmeshAbandonTriggered)
	{
		const bool bNavMeshUnavailable = !F::NavEngine.IsNavMeshLoaded();
		if (bNavMeshUnavailable)
		{
			m_bNavmeshAbandonTriggered = true;
			SDK::Output("AutoQueue", "No navmesh available for current map, abandoning match", { 255, 100, 100 }, OUTPUT_CONSOLE | OUTPUT_TOAST, -1);
			I::TFGCClientSystem->AbandonCurrentMatch();
			bWasInGame = false;
			bWasDisconnected = true;
			flLastQueueTime = 0.0f;
			bQueuedFromRQif = false;
			return;
		}
	}

	if (Vars::Misc::Queueing::AutoDumpNames.Value && Vars::Misc::Queueing::AutoCasualQueue.Value && !Vars::Misc::Queueing::AutoCommunityQueue.Value)
	{
		if (bInGameNow && !bIsLoadingMapNow && !m_bAutoDumpedThisMatch)
		{
			const float flDelay = std::max(0, Vars::Misc::Queueing::AutoDumpDelay.Value);
			const float flCurrentTime = I::EngineClient->Time();
			if (m_flAutoDumpStartTime <= 0.0f)
				m_flAutoDumpStartTime = flCurrentTime;

			if ((flCurrentTime - m_flAutoDumpStartTime) >= flDelay)
			{
				const auto tResult = F::Misc.DumpNames(false);
				if (!tResult.m_bResourceAvailable || tResult.m_uCandidateCount == 0)
					m_flAutoDumpStartTime = flCurrentTime;
				else
				{
					m_bAutoDumpedThisMatch = true;
					m_flAutoDumpStartTime = 0.0f;

					if (I::TFGCClientSystem)
					{
						const size_t uDuplicateCount = tResult.m_uSkippedSessionDuplicate + tResult.m_uSkippedFileDuplicate;
						SDK::Output("AutoQueue", std::format("Auto dump complete: {} new names, {} duplicates skipped, {} comma filtered. Abandoning match for requeue.",
							tResult.m_uAppendedCount,
							uDuplicateCount,
							tResult.m_uSkippedComma).c_str(), { 255, 255, 100 }, OUTPUT_CONSOLE | OUTPUT_TOAST, -1);
						I::TFGCClientSystem->AbandonCurrentMatch();
						bWasInGame = false;
						bWasDisconnected = true;
						flLastQueueTime = 0.0f;
						bQueuedFromRQif = false;
						bQueuedOnce = false;
					}
				}
			}
		}
		else if (!bInGameNow)
		{
			m_bAutoDumpedThisMatch = false;
			m_flAutoDumpStartTime = 0.0f;
		}
	}
	else
	{
		m_flAutoDumpStartTime = 0.0f;
		if (!bInGameNow)
			m_bAutoDumpedThisMatch = false;
	}

	// Auto Mann Up queue
	if (Vars::Misc::Queueing::AutoMannUpQueue.Value)
	{
		if (!I::TFPartyClient->BInQueueForMatchGroup(k_eTFMatchGroup_MvM_MannUp))
		{
			bool bInGame = I::EngineClient->IsInGame();
			bool bIsLoadingMap = I::EngineClient->IsDrawingLoadingImage();
			if (bIsLoadingMap && Vars::Misc::Queueing::RQLTM.Value)
				return;

			float flCurrentTime = I::EngineClient->Time();
			float flQueueDelay = Vars::Misc::Queueing::QueueDelay.Value == 0 ? 20.0f : Vars::Misc::Queueing::QueueDelay.Value * 60.0f;

			static float flLastQueueTimeMannUp = 0.0f;
			static bool bQueuedOnceMannUp = false;

			bool bShouldQueue = !bQueuedOnceMannUp || (flCurrentTime - flLastQueueTimeMannUp >= flQueueDelay);
			if (bShouldQueue && !bInGame && !bIsLoadingMap)
			{
				I::TFPartyClient->RequestQueueForMatch(k_eTFMatchGroup_MvM_MannUp);
				flLastQueueTimeMannUp = flCurrentTime;
				bQueuedOnceMannUp = true;
			}
		}
	}

	if (Vars::Misc::Queueing::AutoCasualQueue.Value)
	{
		float flCurrentTime = I::EngineClient->Time();
		bool bInGame = I::EngineClient->IsInGame();
		bool bIsLoadingMap = I::EngineClient->IsDrawingLoadingImage();
		bool bIsConnected = I::EngineClient->IsConnected();
		bool bHasNetChannel = I::ClientState && I::ClientState->m_NetChannel;
		bool bIsQueued = I::TFPartyClient->BInQueueForMatchGroup(k_eTFMatchGroup_Casual_Default);

		if (bIsLoadingMap && bIsQueued)
		{
			I::TFPartyClient->CancelMatchQueueRequest(k_eTFMatchGroup_Casual_Default);
			SDK::Output("AutoQueue", "Loading screen active, canceling casual queue", { 255, 255, 100 }, OUTPUT_CONSOLE | OUTPUT_TOAST, -1);
			bQueuedFromRQif = false;
			flLastQueueTime = flCurrentTime;
			bIsQueued = false;
		}

		if (bIsLoadingMap && Vars::Misc::Queueing::RQLTM.Value)
			return;

		float flQueueDelay = Vars::Misc::Queueing::QueueDelay.Value == 0 ? 20.0f : Vars::Misc::Queueing::QueueDelay.Value * 60.0f;

		int nPlayerCount = 0;
		bool bRQConditionMet = false;

		if (bInGame && Vars::Misc::Queueing::RQif.Value)
		{
			if (auto pResource = H::Entities.GetResource())
			{
				for (int i = 1; i <= I::EngineClient->GetMaxClients(); i++)
				{
					if (!pResource->m_bValid(i) || !pResource->m_bConnected(i) || pResource->m_iUserID(i) == -1)
						continue;

					if (pResource->IsFakePlayer(i))
						continue;

					bool bShouldCount = true;
					const uint32_t uFriendsID = pResource->m_iAccountID(i);

					if (Vars::Misc::Queueing::RQIgnoreFriends.Value)
					{
#ifdef TEXTMODE
						if (uFriendsID && F::NamedPipe.IsLocalBot(uFriendsID))
							bShouldCount = false;
#endif

						if (bShouldCount && (H::Entities.IsFriend(uFriendsID) ||
							H::Entities.InParty(uFriendsID) ||
							F::PlayerUtils.HasTag(uFriendsID, F::PlayerUtils.TagToIndex(FRIEND_TAG)) ||
							F::PlayerUtils.HasTag(uFriendsID, F::PlayerUtils.TagToIndex(FRIEND_IGNORE_TAG)) ||
							F::PlayerUtils.HasTag(uFriendsID, F::PlayerUtils.TagToIndex(IGNORED_TAG)) ||
							F::PlayerUtils.HasTag(uFriendsID, F::PlayerUtils.TagToIndex(BOT_IGNORE_TAG)) ||
							F::PlayerUtils.HasTag(uFriendsID, F::PlayerUtils.TagToIndex(PARTY_TAG))))
							bShouldCount = false;
					}

					if (bShouldCount)
						nPlayerCount++;
				}
			}

			int nPlayersLT = Vars::Misc::Queueing::RQplt.Value;
			int nPlayersGT = Vars::Misc::Queueing::RQpgt.Value;
			if ((nPlayersLT > 0 && nPlayerCount < nPlayersLT) || (nPlayersGT > 0 && nPlayerCount > nPlayersGT))
				bRQConditionMet = true;
		}

		if (bIsQueued && bQueuedFromRQif)
		{
			bool bMaintainQueue = Vars::Misc::Queueing::RQif.Value && bInGame && bRQConditionMet;
			if (!bMaintainQueue)
			{
				I::TFPartyClient->CancelMatchQueueRequest(k_eTFMatchGroup_Casual_Default);
				SDK::Output("AutoQueue", "RQif conditions cleared, canceling casual queue", { 255, 255, 100 }, OUTPUT_CONSOLE | OUTPUT_TOAST, -1);
				bQueuedFromRQif = false;
				flLastQueueTime = flCurrentTime;
				bIsQueued = false;
			}
		}

		if (bIsQueued)
		{
			bWasInGame = bInGame;
			return;
		}

		if (bWasInGame && !bInGame && !bIsLoadingMap)
		{
			bWasDisconnected = true;

			if (Vars::Misc::Queueing::RQif.Value && Vars::Misc::Queueing::RQkick.Value)
				flLastQueueTime = 0.0f;
		}

		bWasInGame = bInGame;

		if (bInGame && Vars::Misc::Queueing::RQif.Value && bRQConditionMet)
		{
			if (Vars::Misc::Queueing::RQnoAbandon.Value)
			{
				I::TFPartyClient->RequestQueueForMatch(k_eTFMatchGroup_Casual_Default);
				flLastQueueTime = flCurrentTime;
				bQueuedFromRQif = true;
			}
			else
			{
				I::TFGCClientSystem->AbandonCurrentMatch();
				bWasInGame = false;
				bWasDisconnected = true;
				flLastQueueTime = 0.0f;
				bQueuedFromRQif = false;
			}
		}

		bool bShouldQueue = !bQueuedOnce || (flCurrentTime - flLastQueueTime >= flQueueDelay);
		bool bStillAttachedToServer = bInGame || bIsConnected || bHasNetChannel;

		if (bShouldQueue && !bIsLoadingMap && !bStillAttachedToServer)
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
			bQueuedFromRQif = false;
		}
	}
	else
	{
		bQueuedOnce = false;
		flLastQueueTime = 0.0f;
		bQueuedFromRQif = false;
	}

	if (Vars::Misc::Queueing::AutoCommunityQueue.Value)
		RunCommunityQueue();
	else
	{
		CleanupServerList();
		m_bConnectedToCommunityServer = false;
		m_sCurrentServerIP.clear();
	}
}

void CAutoQueue::RunCommunityQueue()
{
	if (!I::SteamMatchmakingServers)
		return;

	bool bInGame = I::EngineClient->IsInGame();
	bool bIsLoadingMap = I::EngineClient->IsDrawingLoadingImage();
	if (bIsLoadingMap)
		return;

	float flCurrentTime = I::EngineClient->Time();

	static bool bWasInGameCommunity = false;
	if (bWasInGameCommunity && !bInGame && m_bConnectedToCommunityServer)
	{
		HandleDisconnect();
	}
	bWasInGameCommunity = bInGame;

	if (bInGame && m_bConnectedToCommunityServer)
	{
		CheckServerTimeout();
		return; // Don't search for new servers while connected
	}

	if (!bInGame && !m_bSearchingServers)
	{
		float flSearchDelay = Vars::Misc::Queueing::ServerSearchDelay.Value;
		if (flCurrentTime - m_flLastServerSearch >= flSearchDelay)
			SearchCommunityServers();
	}
}

void CAutoQueue::SearchCommunityServers()
{
	if (!I::SteamMatchmakingServers || m_bSearchingServers)
		return;

	SDK::Output("AutoQueue", "Searching for community servers...", { 100, 255, 100 }, OUTPUT_CONSOLE | OUTPUT_TOAST, -1);
	CleanupServerList();

	std::vector<MatchMakingKeyValuePair_t> vFilters;
	MatchMakingKeyValuePair_t tAppFilter; strcpy_s(tAppFilter.m_szKey, "appid"); strcpy_s(tAppFilter.m_szValue, "440"); vFilters.push_back(tAppFilter);
	MatchMakingKeyValuePair_t tPlayersFilter; strcpy_s(tPlayersFilter.m_szKey, "hasplayers"); strcpy_s(tPlayersFilter.m_szValue, "1"); vFilters.push_back(tPlayersFilter);
	MatchMakingKeyValuePair_t tNotFullFilter; strcpy_s(tNotFullFilter.m_szKey, "notfull"); strcpy_s(tNotFullFilter.m_szValue, "1"); vFilters.push_back(tNotFullFilter);

	if (Vars::Misc::Queueing::AvoidPasswordServers.Value)
	{
		MatchMakingKeyValuePair_t tNoPasswordFilter; strcpy_s(tNoPasswordFilter.m_szKey, "nand"); strcpy_s(tNoPasswordFilter.m_szValue, "1"); vFilters.push_back(tNoPasswordFilter);
		MatchMakingKeyValuePair_t tPasswordFilter; strcpy_s(tPasswordFilter.m_szKey, "password"); strcpy_s(tPasswordFilter.m_szValue, "1"); vFilters.push_back(tPasswordFilter);
	}
	if (Vars::Misc::Queueing::OnlyNonDedicatedServers.Value)
	{
		MatchMakingKeyValuePair_t tNandFilter; strcpy_s(tNandFilter.m_szKey, "nand"); strcpy_s(tNandFilter.m_szValue, "1"); vFilters.push_back(tNandFilter);
		MatchMakingKeyValuePair_t tDedicatedFilter; strcpy_s(tDedicatedFilter.m_szKey, "dedicated"); strcpy_s(tDedicatedFilter.m_szValue, "1"); vFilters.push_back(tDedicatedFilter);
	}

	MatchMakingKeyValuePair_t* pFilters = vFilters.empty() ? nullptr : vFilters.data();
	m_hServerListRequest = I::SteamMatchmakingServers->RequestInternetServerList(
		440,
		&pFilters,
		vFilters.size(),
		this
	);

	if (m_hServerListRequest)
	{
		m_bSearchingServers = true;
		m_flLastServerSearch = I::EngineClient->Time();
	}
}

void CAutoQueue::ConnectToServer(const gameserveritem_t* pServer)
{
	if (!pServer)
		return;

	std::string sServerAddress = pServer->m_NetAdr.GetConnectionAddressString();

	char msg[256];
	snprintf(msg, sizeof(msg), "Connecting to server: %s (%s)", pServer->GetName(), sServerAddress.c_str());
	SDK::Output("AutoQueue", msg, { 100, 255, 100 }, OUTPUT_CONSOLE | OUTPUT_TOAST, -1);

	std::string sConnectCmd = std::string("connect ") + sServerAddress;
	I::EngineClient->ClientCmd_Unrestricted(sConnectCmd.c_str());

	m_sCurrentServerIP = sServerAddress;
	m_flServerJoinTime = I::EngineClient->Time();
	m_bConnectedToCommunityServer = true;
}

bool CAutoQueue::IsServerValid(const gameserveritem_t* pServer)
{
	if (!pServer || !pServer->m_bHadSuccessfulResponse)
		return false;

	if (Vars::Misc::Queueing::AvoidPasswordServers.Value && pServer->m_bPassword)
		return false;

	if (pServer->m_nPlayers >= pServer->m_nMaxPlayers)
		return false;

	int nPlayers = pServer->m_nPlayers - pServer->m_nBotPlayers;
	if (nPlayers < Vars::Misc::Queueing::MinPlayersOnServer.Value ||
		nPlayers > Vars::Misc::Queueing::MaxPlayersOnServer.Value)
		return false;

	if (Vars::Misc::Queueing::PreferSteamNickServers.Value)
	{
		if (!IsServerNameMatch(pServer->GetName()))
			return false;
	}

	if (Vars::Misc::Queueing::RequireNavmesh.Value)
	{
		if (!HasNavmeshForMap(pServer->m_szMap))
			return false;
	}

	if (Vars::Misc::Queueing::OnlySteamNetworkingIPs.Value)
	{
		std::string sServerIP = pServer->m_NetAdr.GetConnectionAddressString();
		if (sServerIP.rfind("169.254", 0) != 0)
			return false;
	}

	return true;
}

bool CAutoQueue::HasNavmeshForMap(const std::string& sMapName)
{
	auto sNavPath = F::NavEngine.GetNavFilePath();
	if (sNavPath.empty())
		return false;

	const size_t uFirstAfterLastSlash = sNavPath.find_last_of("/\\") + 1;
	if (sNavPath.find(sMapName, uFirstAfterLastSlash) != uFirstAfterLastSlash)
		return F::NavEngine.IsNavMeshLoaded();

	std::ifstream navFile(sNavPath, std::ios::binary);
	if (!navFile.is_open())
		return false;

	uint32_t uMagic;
	navFile.read(reinterpret_cast<char*>(&uMagic), sizeof(uint32_t));
	return uMagic == 0xFEEDFACE;
}

bool CAutoQueue::IsServerNameMatch(const std::string& sServerName)
{
	if (sServerName.length() < 10)
		return false;

	size_t sServerPos = sServerName.rfind("'s Server");
	if (sServerPos == std::string::npos)
		return false;

	size_t uExpectedEnd = sServerPos + 9;
	if (uExpectedEnd < sServerName.length())
	{
		char cNextChar = sServerName[uExpectedEnd];
		if (cNextChar != ' ' && cNextChar != '\t' && cNextChar != '(' && cNextChar != '\0')
			return false;
	}

	if (sServerPos < 2)
		return false;

	return true;
}

void CAutoQueue::CleanupServerList()
{
	if (m_hServerListRequest && I::SteamMatchmakingServers)
	{
		I::SteamMatchmakingServers->ReleaseRequest(m_hServerListRequest);
		m_hServerListRequest = nullptr;
	}

	m_vCommunityServers.clear();
	m_bSearchingServers = false;
}

void CAutoQueue::HandleDisconnect()
{
	SDK::Output("AutoQueue", "Disconnected from community server, searching for new one...", { 255, 255, 100 }, OUTPUT_CONSOLE | OUTPUT_TOAST, -1);

	m_bConnectedToCommunityServer = false;
	m_sCurrentServerIP.clear();
	m_flServerJoinTime = 0.0f;

	m_flLastServerSearch = I::EngineClient->Time() - Vars::Misc::Queueing::ServerSearchDelay.Value + 5.0f;
}

void CAutoQueue::CheckServerTimeout()
{
	float flCurrentTime = I::EngineClient->Time();
	float flMaxTime = Vars::Misc::Queueing::MaxTimeOnServer.Value;

	if (flCurrentTime - m_flServerJoinTime >= flMaxTime)
	{
		SDK::Output("AutoQueue", "Max time on server reached, disconnecting...", { 255, 255, 100 }, OUTPUT_CONSOLE | OUTPUT_TOAST, -1);
		I::EngineClient->ClientCmd_Unrestricted("disconnect");
	}
}

void CAutoQueue::ServerResponded(HServerListRequest hRequest, int iServer)
{
	if (hRequest != m_hServerListRequest || !I::SteamMatchmakingServers)
		return;

	gameserveritem_t* pServer = I::SteamMatchmakingServers->GetServerDetails(hRequest, iServer);
	if (pServer && IsServerValid(pServer))
		m_vCommunityServers.push_back(pServer);
}

void CAutoQueue::ServerFailedToRespond(HServerListRequest hRequest, int iServer)
{
	// Nothing to do here
}

void CAutoQueue::RefreshComplete(HServerListRequest hRequest, EMatchMakingServerResponse response)
{
	if (hRequest != m_hServerListRequest)
		return;

	m_bSearchingServers = false;

	char msg[128];
	snprintf(msg, sizeof(msg), "Found %zu valid community servers", m_vCommunityServers.size());
	SDK::Output("AutoQueue", msg, { 100, 255, 100 }, OUTPUT_CONSOLE | OUTPUT_TOAST, -1);

	if (!m_vCommunityServers.empty())
	{
		std::sort(m_vCommunityServers.begin(), m_vCommunityServers.end(),
				  [this](const gameserveritem_t* a, const gameserveritem_t* b) -> bool
				  {
					  bool aIsNickServer = IsServerNameMatch(a->GetName());
					  bool bIsNickServer = IsServerNameMatch(b->GetName());

					  if (aIsNickServer != bIsNickServer)
						  return aIsNickServer;

					  return (a->m_nPlayers - a->m_nBotPlayers) > (b->m_nPlayers - b->m_nBotPlayers);
				  });

		ConnectToServer(m_vCommunityServers[0]);
	}
	else
		SDK::Output("AutoQueue", "No valid community servers found", { 255, 100, 100 }, OUTPUT_CONSOLE | OUTPUT_TOAST, -1);

	CleanupServerList();
}