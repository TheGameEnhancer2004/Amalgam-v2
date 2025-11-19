#pragma once

#include "../../SDK/SDK.h"

#include <filesystem>
#include <future>
#include <shared_mutex>

class CSteamProfileCache
{
public:
	struct AvatarImage_t
	{
		std::shared_ptr<const std::vector<uint8_t>> m_pPixels;
		uint32_t m_uWidth = 0;
		uint32_t m_uHeight = 0;
		uint32_t m_uStride = 0;

		[[nodiscard]] bool HasData() const
		{
			return m_pPixels && !m_pPixels->empty() && m_uWidth && m_uHeight;
		}
	};

	void Touch(uint32_t uAccountID);
	void TouchAvatar(uint32_t uAccountID);
	void Pump();
	std::string GetPersonaName(uint32_t uAccountID);
	bool TryGetAvatarImage(uint32_t uAccountID, AvatarImage_t& tOutImage);
	void Invalidate(uint32_t uAccountID);
	static std::filesystem::path GetAvatarPath(uint32_t uAccountID);
	static bool SaveAvatarToDisk(uint32_t uAccountID, const std::vector<uint8_t>& vPixels, uint32_t uWidth, uint32_t uHeight, std::filesystem::path* pOutPath = nullptr, bool bLog = true);

private:
	struct SummaryResult_t
	{
		bool m_bSuccess = false;
		bool m_bPermanentFailure = false;
		std::string m_sPersonaName;
		std::string m_sAvatarUrl;
		double m_dRetryDelay = 300.0;
	};

	struct AvatarResult_t
	{
		bool m_bSuccess = false;
		std::vector<uint8_t> m_vPixels;
		uint32_t m_uWidth = 0;
		uint32_t m_uHeight = 0;
		double m_dRetryDelay = 120.0;
	};

	struct Entry_t
	{
		std::string m_sPersonaName;
		std::string m_sAvatarUrl;
		std::shared_ptr<std::vector<uint8_t>> m_pAvatarPixels;
		uint32_t m_uAvatarWidth = 0;
		uint32_t m_uAvatarHeight = 0;
		bool m_bSummaryResolved = false;
		bool m_bSummaryFailed = false;
		bool m_bAvatarResolved = false;
		bool m_bAvatarFailed = false;
		bool m_bAvatarUrlWarned = false;
		double m_dNextSummaryAttempt = 0.0;
		double m_dNextAvatarAttempt = 0.0;
		std::future<SummaryResult_t> m_fuSummary;
		std::future<AvatarResult_t> m_fuAvatar;
	};

	mutable std::shared_mutex m_mutex;
	std::unordered_map<uint32_t, Entry_t> m_mEntries;
	double m_flNextSummaryDispatch = 0.0;
	uint32_t m_uSummaryInFlight = 0;

	void EnsureSummary(uint32_t uAccountID, Entry_t& tEntry);
	void EnsureAvatar(uint32_t uAccountID, Entry_t& tEntry);
	void ProcessFutures(uint32_t uAccountID, Entry_t& tEntry);

	static SummaryResult_t FetchSummary(uint32_t uAccountID, std::string sApiKey);
	static AvatarResult_t FetchAvatar(uint32_t uAccountID, const std::string& sUrl);
};

ADD_FEATURE(CSteamProfileCache, SteamProfileCache);
