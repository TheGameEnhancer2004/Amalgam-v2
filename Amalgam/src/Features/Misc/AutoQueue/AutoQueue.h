#pragma once
#include "../../../SDK/SDK.h"
#include "../../../SDK/Definitions/Steam/ISteamMatchmaking.h"
#include "../../../SDK/Definitions/Steam/MatchmakingTypes.h"
#include <vector>
#include <string>

class gameserveritem_t;

class CAutoQueue : public ISteamMatchmakingServerListResponse
{
private:
	// Community servers
	HServerListRequest m_hServerListRequest = nullptr;
	std::vector<gameserveritem_t*> m_vCommunityServers;
	float m_flLastServerSearch = 0.0f;
	float m_flServerJoinTime = 0.0f;
	std::string m_sCurrentServerIP;
	bool m_bSearchingServers = false;
	bool m_bConnectedToCommunityServer = false;
	std::string m_sLastLevelName;
	bool m_bNavmeshAbandonTriggered = false;
	float m_flAutoDumpStartTime = 0.0f;
	bool m_bAutoDumpedThisMatch = false;

	void RunCommunityQueue();
	void SearchCommunityServers();
	void ConnectToServer(const gameserveritem_t* pServer);
	bool IsServerValid(const gameserveritem_t* pServer);
	bool HasNavmeshForMap(const std::string& sMapName);
	bool IsServerNameMatch(const std::string& sServerName);
	void CleanupServerList();
	void HandleDisconnect();
	void CheckServerTimeout();

public:
	void Run();

	// ISteamMatchmakingServerListResponse
	virtual void ServerResponded(HServerListRequest hRequest, int iServer) override;
	virtual void ServerFailedToRespond(HServerListRequest hRequest, int iServer) override;
	virtual void RefreshComplete(HServerListRequest hRequest, EMatchMakingServerResponse response) override;
};

ADD_FEATURE(CAutoQueue, AutoQueue);