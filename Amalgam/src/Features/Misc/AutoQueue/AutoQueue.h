#pragma once
#include "../../../SDK/SDK.h"
#include "../../../SDK/Definitions/Steam/ISteamMatchmaking.h"
#include "../../../SDK/Definitions/Steam/MatchmakingTypes.h"

class gameserveritem_t;

class CAutoQueue : public ISteamMatchmakingServerListResponse
{
private:
	// commserv
	HServerListRequest m_hServerListRequest = nullptr;
	std::vector<gameserveritem_t*> m_vCommunityServers;
	float m_flLastServerSearch = 0.0f;
	float m_flServerJoinTime = 0.0f;
	std::string m_sCurrentServerIP;
	bool m_bSearchingServers = false;
	bool m_bConnectedToCommunityServer = false;
	
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
	
	virtual void ServerResponded(HServerListRequest hRequest, int iServer) override;
	virtual void ServerFailedToRespond(HServerListRequest hRequest, int iServer) override;
	virtual void RefreshComplete(HServerListRequest hRequest, EMatchMakingServerResponse response) override;
};

ADD_FEATURE(CAutoQueue, AutoQueue);