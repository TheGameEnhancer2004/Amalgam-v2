#pragma once

#include "../../SDK/SDK.h"

#include <filesystem>
#include <future>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

class CSteamProfileCache
{
public:
	struct AvatarImage
	{
		std::shared_ptr<const std::vector<uint8_t>> pixels;
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t stride = 0;

		[[nodiscard]] bool HasData() const
		{
			return pixels && !pixels->empty() && width && height;
		}
	};

	void Touch(uint32_t uAccountID);
	void TouchAvatar(uint32_t uAccountID);
	void Pump();
	std::string GetPersonaName(uint32_t uAccountID);
	bool TryGetAvatarImage(uint32_t uAccountID, AvatarImage& outImage);
	void Invalidate(uint32_t uAccountID);
	static std::filesystem::path GetAvatarPath(uint32_t uAccountID);
	static bool SaveAvatarToDisk(uint32_t uAccountID, const std::vector<uint8_t>& vPixels, uint32_t width, uint32_t height, std::filesystem::path* pOutPath = nullptr, bool bLog = true);

private:
	struct SummaryResult
	{
		bool success = false;
		bool permanentFailure = false;
		std::string personaName;
		std::string avatarUrl;
		double retryDelay = 300.0;
	};

	struct AvatarResult
	{
		bool success = false;
		std::vector<uint8_t> pixels;
		uint32_t width = 0;
		uint32_t height = 0;
		double retryDelay = 120.0;
	};

	struct Entry
	{
		std::string personaName;
		std::string avatarUrl;
		std::shared_ptr<std::vector<uint8_t>> avatarPixels;
		uint32_t avatarWidth = 0;
		uint32_t avatarHeight = 0;
		bool summaryResolved = false;
		bool summaryFailed = false;
		bool avatarResolved = false;
		bool avatarFailed = false;
		bool avatarUrlWarned = false;
		double nextSummaryAttempt = 0.0;
		double nextAvatarAttempt = 0.0;
		std::future<SummaryResult> summaryFuture;
		std::future<AvatarResult> avatarFuture;
	};

	mutable std::shared_mutex m_mutex;
	std::unordered_map<uint32_t, Entry> m_entries;
	double m_flNextSummaryDispatch = 0.0;
	uint32_t m_uSummaryInFlight = 0;

	void EnsureSummary(uint32_t uAccountID, Entry& entry);
	void EnsureAvatar(uint32_t uAccountID, Entry& entry);
	void ProcessFutures(uint32_t uAccountID, Entry& entry);

	static SummaryResult FetchSummary(uint32_t uAccountID, std::string sApiKey);
	static AvatarResult FetchAvatar(uint32_t uAccountID, const std::string& sUrl);
};

ADD_FEATURE(CSteamProfileCache, SteamProfileCache);
