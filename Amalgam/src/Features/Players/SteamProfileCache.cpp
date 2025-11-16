#include "SteamProfileCache.h"
#include "../Configs/Configs.h"

#include <boost/property_tree/json_parser.hpp>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <mutex>
#include <sstream>
#include <winhttp.h>
#include <wininet.h>
#include <wincodec.h>
#include <wrl/client.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "windowscodecs.lib")

#ifndef HTTP_STATUS_UNAUTHORIZED
#define HTTP_STATUS_UNAUTHORIZED 401
#endif
#ifndef HTTP_STATUS_FORBIDDEN
#define HTTP_STATUS_FORBIDDEN 403
#endif
#ifndef HTTP_STATUS_TOO_MANY_REQUESTS
#define HTTP_STATUS_TOO_MANY_REQUESTS 429
#endif

using Microsoft::WRL::ComPtr;

namespace
{
	constexpr Color_t kLogColor = { 175, 150, 255, 255 };
	constexpr Color_t kErrorColor = { 255, 150, 150, 255 };
	double g_flNextMissingKeyLog = 0.0;
	double g_flNextSummaryFailLog = 0.0;
	double g_flNextAvatarFailLog = 0.0;

	std::filesystem::path GetAvatarFolder()
	{
		if (F::Configs.m_sCorePath.empty())
			return {};
		std::filesystem::path tFolder = std::filesystem::path(F::Configs.m_sCorePath) / "avatars";
		std::error_code ec;
		std::filesystem::create_directories(tFolder, ec);
		if (ec)
			return {};
		return tFolder;
	}

	bool WriteAvatarPng(const std::filesystem::path& sPath, const std::vector<uint8_t>& vPixels, uint32_t uWidth, uint32_t uHeight)
	{
		if (vPixels.empty() || !uWidth || !uHeight)
			return false;

		const HRESULT hrInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		const bool bDidInit = SUCCEEDED(hrInit);
		if (!bDidInit && hrInit != RPC_E_CHANGED_MODE)
			return false;

		HRESULT hr = S_OK;
		ComPtr<IWICImagingFactory> pFactory;
		hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory));
		ComPtr<IWICStream> pStream;
		if (SUCCEEDED(hr))
			hr = pFactory->CreateStream(&pStream);
		std::wstring sWidePath;
		if (SUCCEEDED(hr))
		{
			sWidePath = sPath.wstring();
			hr = pStream->InitializeFromFilename(sWidePath.c_str(), GENERIC_WRITE);
		}
		ComPtr<IWICBitmapEncoder> pEncoder;
		if (SUCCEEDED(hr))
			hr = pFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &pEncoder);
		if (SUCCEEDED(hr))
			hr = pEncoder->Initialize(pStream.Get(), WICBitmapEncoderNoCache);
		ComPtr<IWICBitmapFrameEncode> pFrame;
		ComPtr<IPropertyBag2> pProps;
		if (SUCCEEDED(hr))
			hr = pEncoder->CreateNewFrame(&pFrame, &pProps);
		if (SUCCEEDED(hr))
			hr = pFrame->Initialize(pProps.Get());
		if (SUCCEEDED(hr))
			hr = pFrame->SetSize(uWidth, uHeight);
		WICPixelFormatGUID tFormat = GUID_WICPixelFormat32bppBGRA;
		if (SUCCEEDED(hr))
			hr = pFrame->SetPixelFormat(&tFormat);
		if (SUCCEEDED(hr) && tFormat != GUID_WICPixelFormat32bppBGRA)
			hr = E_FAIL;
		if (SUCCEEDED(hr))
			hr = pFrame->WritePixels(uHeight, uWidth * 4, static_cast<UINT>(vPixels.size()), const_cast<BYTE*>(vPixels.data()));
		if (SUCCEEDED(hr))
			hr = pFrame->Commit();
		if (SUCCEEDED(hr))
			hr = pEncoder->Commit();

		if (bDidInit)
			CoUninitialize();
		return SUCCEEDED(hr);
	}


	struct ParsedUrl_t
	{
		std::wstring host;
		std::wstring path;
		INTERNET_PORT port = 0;
		bool secure = false;
	};

	void LogThrottled(const char* sMessage, double& flNextAllowed, double flCooldown, Color_t tColor)
	{
		const double flNow = SDK::PlatFloatTime();
		if (flNow < flNextAllowed)
			return;
		flNextAllowed = flNow + flCooldown;
		SDK::Output("steamwebapi", sMessage, tColor, OUTPUT_CONSOLE | OUTPUT_DEBUG | OUTPUT_MENU);
	}

	bool CrackUrl(const std::wstring& sUrl, ParsedUrl_t& tOut)
	{
		URL_COMPONENTS tComponents = {};
		tComponents.dwStructSize = sizeof(tComponents);
		tComponents.dwSchemeLength = static_cast<DWORD>(-1);
		tComponents.dwHostNameLength = static_cast<DWORD>(-1);
		tComponents.dwUrlPathLength = static_cast<DWORD>(-1);
		tComponents.dwExtraInfoLength = static_cast<DWORD>(-1);
		if (!WinHttpCrackUrl(sUrl.c_str(), 0, 0, &tComponents))
			return false;

		tOut.secure = tComponents.nScheme == INTERNET_SCHEME_HTTPS;
		tOut.port = tComponents.nPort ? tComponents.nPort : (tOut.secure ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT);
		tOut.host.assign(tComponents.lpszHostName, tComponents.dwHostNameLength);
		std::wstring sPath(tComponents.lpszUrlPath, tComponents.dwUrlPathLength);
		std::wstring sExtra(tComponents.lpszExtraInfo, tComponents.dwExtraInfoLength);
		if (sPath.empty())
			sPath = L"/";
		tOut.path = sPath + sExtra;
		return true;
	}

	std::vector<uint8_t> DownloadUrl(const std::wstring& sUrl, DWORD* pStatusCode = nullptr)
	{
		std::vector<uint8_t> vBuffer;
		if (pStatusCode)
			*pStatusCode = 0;
		ParsedUrl_t tParsed;
		if (!CrackUrl(sUrl, tParsed))
			return vBuffer;

		HINTERNET hSession = WinHttpOpen(L"Amalgam/SteamProfileCache", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
		if (!hSession)
			return vBuffer;

		WinHttpSetTimeouts(hSession, 5000, 5000, 5000, 5000);

		HINTERNET hConnect = WinHttpConnect(hSession, tParsed.host.c_str(), tParsed.port, 0);
		if (!hConnect)
		{
			WinHttpCloseHandle(hSession);
			return vBuffer;
		}

		DWORD dwFlags = tParsed.secure ? WINHTTP_FLAG_SECURE : 0;
		HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", tParsed.path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, dwFlags);
		if (!hRequest)
		{
			WinHttpCloseHandle(hConnect);
			WinHttpCloseHandle(hSession);
			return vBuffer;
		}

		BOOL bResult = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
		if (bResult)
			bResult = WinHttpReceiveResponse(hRequest, nullptr);

		if (bResult)
		{
			DWORD dwStatus = 0;
			DWORD dwStatusSize = sizeof(dwStatus);
			if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &dwStatus, &dwStatusSize, WINHTTP_NO_HEADER_INDEX))
			{
				if (pStatusCode)
					*pStatusCode = dwStatus;
				if (dwStatus != HTTP_STATUS_OK)
					bResult = FALSE;
			}
		}

		if (bResult)
		{
			DWORD dwBytesAvailable = 0;
			while (WinHttpQueryDataAvailable(hRequest, &dwBytesAvailable))
			{
				if (!dwBytesAvailable)
					break;

				size_t uOffset = vBuffer.size();
				vBuffer.resize(uOffset + dwBytesAvailable);
				DWORD dwBytesRead = 0;
				if (!WinHttpReadData(hRequest, vBuffer.data() + uOffset, dwBytesAvailable, &dwBytesRead))
				{
					vBuffer.clear();
					break;
				}
				vBuffer.resize(uOffset + dwBytesRead);
			}
		}

		WinHttpCloseHandle(hRequest);
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return vBuffer;
	}

	bool DecodeImage(const std::vector<uint8_t>& vSource, std::vector<uint8_t>& vOut, uint32_t& uWidth, uint32_t& uHeight)
	{
		if (vSource.empty())
			return false;

		const HRESULT hrInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		const bool bDidInit = SUCCEEDED(hrInit);
		if (!bDidInit && hrInit != RPC_E_CHANGED_MODE)
			return false;

		HRESULT hr = S_OK;
		ComPtr<IWICImagingFactory> pFactory;
		hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory));
		if (SUCCEEDED(hr))
		{
			ComPtr<IWICStream> pStream;
			hr = pFactory->CreateStream(&pStream);
			if (SUCCEEDED(hr))
				hr = pStream->InitializeFromMemory(const_cast<BYTE*>(vSource.data()), static_cast<DWORD>(vSource.size()));

			ComPtr<IWICBitmapDecoder> pDecoder;
			if (SUCCEEDED(hr))
				hr = pFactory->CreateDecoderFromStream(pStream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &pDecoder);

			ComPtr<IWICBitmapFrameDecode> pFrame;
			if (SUCCEEDED(hr))
				hr = pDecoder->GetFrame(0, &pFrame);

			ComPtr<IWICFormatConverter> pConverter;
			if (SUCCEEDED(hr))
			{
				hr = pFactory->CreateFormatConverter(&pConverter);
				if (SUCCEEDED(hr))
					hr = pConverter->Initialize(pFrame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeCustom);
			}

			if (SUCCEEDED(hr))
				hr = pConverter->GetSize(&uWidth, &uHeight);

			if (SUCCEEDED(hr))
			{
				vOut.resize(static_cast<size_t>(uWidth) * static_cast<size_t>(uHeight) * 4);
				hr = pConverter->CopyPixels(nullptr, uWidth * 4, static_cast<UINT>(vOut.size()), vOut.data());
			}
		}

		if (bDidInit)
			CoUninitialize();
		return SUCCEEDED(hr);
	}
}

	std::filesystem::path CSteamProfileCache::GetAvatarPath(uint32_t uAccountID)
	{
		if (!uAccountID)
			return {};

		auto sFolder = GetAvatarFolder();
		if (sFolder.empty())
			return {};

		const uint64_t uSteamID64 = CSteamID(uAccountID, k_EUniversePublic, k_EAccountTypeIndividual).ConvertToUint64();
		return sFolder / (std::to_string(uSteamID64) + ".png");
	}

	bool CSteamProfileCache::SaveAvatarToDisk(uint32_t uAccountID, const std::vector<uint8_t>& vPixels, uint32_t uWidth, uint32_t uHeight, std::filesystem::path* pOutPath, bool bLog)
	{
		if (vPixels.empty() || !uWidth || !uHeight)
			return false;

		auto sFolder = GetAvatarFolder();
		if (sFolder.empty())
			return false;

		std::error_code ec;
		std::filesystem::create_directories(sFolder, ec);
		if (ec)
			return false;

		const auto sPath = sFolder / (std::to_string(CSteamID(uAccountID, k_EUniversePublic, k_EAccountTypeIndividual).ConvertToUint64()) + ".png");
		if (pOutPath)
			*pOutPath = sPath;

		std::filesystem::remove(sPath, ec);
		const bool bSaved = WriteAvatarPng(sPath, vPixels, uWidth, uHeight);
		if (bLog)
		{
			if (bSaved)
			{
				const std::string sMessage = std::format("Saved avatar to {}", sPath.string());
				SDK::Output("steamwebapi", sMessage.c_str(), kLogColor, OUTPUT_CONSOLE | OUTPUT_DEBUG);
			}
			else
			{
				const std::string sMessage = std::format("Failed to write avatar file {}", sPath.string());
				SDK::Output("steamwebapi", sMessage.c_str(), kErrorColor, OUTPUT_CONSOLE | OUTPUT_DEBUG);
			}
		}
		return bSaved;
	}

void CSteamProfileCache::Touch(uint32_t uAccountID)
{
	if (!uAccountID)
		return;

	std::unique_lock lock(m_mutex);
	auto& entry = m_entries[uAccountID];
	ProcessFutures(uAccountID, entry);
	EnsureSummary(uAccountID, entry);
}

void CSteamProfileCache::TouchAvatar(uint32_t uAccountID)
{
	if (!uAccountID)
		return;

	std::unique_lock lock(m_mutex);
	auto& entry = m_entries[uAccountID];
	ProcessFutures(uAccountID, entry);
	EnsureSummary(uAccountID, entry);
	EnsureAvatar(uAccountID, entry);
}

void CSteamProfileCache::Pump()
{
	std::unique_lock lock(m_mutex);
	for (auto& [uAccountID, entry] : m_entries)
	{
		ProcessFutures(uAccountID, entry);
		EnsureSummary(uAccountID, entry);
		EnsureAvatar(uAccountID, entry);
	}
}

std::string CSteamProfileCache::GetPersonaName(uint32_t uAccountID)
{
	if (!uAccountID)
		return {};

	std::unique_lock lock(m_mutex);
	auto& entry = m_entries[uAccountID];
	ProcessFutures(uAccountID, entry);
	if (!entry.personaName.empty())
		return entry.personaName;

	EnsureSummary(uAccountID, entry);
	return {};
}

bool CSteamProfileCache::TryGetAvatarImage(uint32_t uAccountID, AvatarImage& outImage)
{
	outImage = {};
	if (!uAccountID)
		return false;

	std::unique_lock lock(m_mutex);
	auto& entry = m_entries[uAccountID];
	ProcessFutures(uAccountID, entry);
	if (entry.avatarPixels && !entry.avatarPixels->empty() && entry.avatarWidth && entry.avatarHeight)
	{
		outImage.pixels = entry.avatarPixels;
		outImage.width = entry.avatarWidth;
		outImage.height = entry.avatarHeight;
		outImage.stride = entry.avatarWidth * 4;
		return true;
	}

	EnsureSummary(uAccountID, entry);
	EnsureAvatar(uAccountID, entry);
	return false;
}

void CSteamProfileCache::Invalidate(uint32_t uAccountID)
{
	if (!uAccountID)
		return;

	std::unique_lock lock(m_mutex);
	if (m_uSummaryInFlight == uAccountID)
	{
		m_uSummaryInFlight = 0;
		m_flNextSummaryDispatch = std::max(m_flNextSummaryDispatch, SDK::PlatFloatTime() + 1.0);
	}
	m_entries.erase(uAccountID);
}

void CSteamProfileCache::EnsureSummary(uint32_t uAccountID, Entry& entry)
{
	if (entry.summaryResolved || entry.summaryFuture.valid())
		return;

	const double flNow = SDK::PlatFloatTime();
	if (entry.summaryFailed && flNow < entry.nextSummaryAttempt)
		return;
	if (m_uSummaryInFlight)
		return;
	if (flNow < m_flNextSummaryDispatch)
		return;

	const std::string sApiKey = Vars::Config::SteamWebAPIKey.Value;
	if (sApiKey.empty())
	{
		LogThrottled("steamwebapi key missing. Set it under Anticheat > Cheaters.", g_flNextMissingKeyLog, 15.0, kErrorColor);
		return;
	}

	entry.summaryFuture = std::async(std::launch::async, [uAccountID, sApiKey]() -> SummaryResult
	{
		return FetchSummary(uAccountID, sApiKey);
	});
	m_uSummaryInFlight = uAccountID;
}

void CSteamProfileCache::EnsureAvatar(uint32_t uAccountID, Entry& entry)
{
	if (!entry.summaryResolved)
		return;
	if (entry.avatarUrl.empty())
	{
		if (!entry.avatarUrlWarned)
		{
			entry.avatarUrlWarned = true;
			const uint64_t uSteamID64 = CSteamID(uAccountID, k_EUniversePublic, k_EAccountTypeIndividual).ConvertToUint64();
			const std::string sMessage = std::format("steamwebapi summary returned no avatar URL for {}. Skipping avatar download.", uSteamID64);
			SDK::Output("steamwebapi", sMessage.c_str(), kErrorColor, OUTPUT_CONSOLE | OUTPUT_DEBUG | OUTPUT_MENU);
		}
		return;
	}
	if (entry.avatarResolved || entry.avatarFuture.valid())
		return;

	const double flNow = SDK::PlatFloatTime();
	if (entry.avatarFailed && flNow < entry.nextAvatarAttempt)
		return;

	const std::string sUrl = entry.avatarUrl;
	const uint64_t uSteamID64 = CSteamID(uAccountID, k_EUniversePublic, k_EAccountTypeIndividual).ConvertToUint64();
	SDK::Output("steamwebapi", std::format("Downloading avatar for {}", uSteamID64).c_str(), kLogColor, OUTPUT_CONSOLE | OUTPUT_DEBUG | OUTPUT_MENU);
	entry.avatarFuture = std::async(std::launch::async, [uAccountID, sUrl]() -> AvatarResult
	{
		return FetchAvatar(uAccountID, sUrl);
	});
}

void CSteamProfileCache::ProcessFutures(uint32_t uAccountID, Entry& entry)
{
	if (entry.summaryFuture.valid() && entry.summaryFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
	{
		SummaryResult tResult = entry.summaryFuture.get();
		entry.summaryFuture = {};
		if (m_uSummaryInFlight == uAccountID)
		{
			m_uSummaryInFlight = 0;
			m_flNextSummaryDispatch = std::max(m_flNextSummaryDispatch, SDK::PlatFloatTime() + 1.0);
		}
		if (tResult.success)
		{
			entry.personaName = tResult.personaName;
			entry.avatarUrl = tResult.avatarUrl;
			entry.summaryResolved = true;
			entry.summaryFailed = false;
			entry.nextSummaryAttempt = 0.0;
			entry.avatarUrlWarned = false;
			const uint64_t uSteamID64 = CSteamID(uAccountID, k_EUniversePublic, k_EAccountTypeIndividual).ConvertToUint64();
			const std::string sDisplayName = entry.personaName.empty() ? "(no name)" : entry.personaName;
			const std::string sMessage = std::format("steamwebapi refreshed profile for {} ({})", uSteamID64, sDisplayName);
			SDK::Output("steamwebapi", sMessage.c_str(), kLogColor, OUTPUT_CONSOLE | OUTPUT_DEBUG | OUTPUT_MENU);
		}
		else
		{
			if (tResult.permanentFailure)
			{
				entry.summaryResolved = true;
				entry.summaryFailed = false;
				entry.nextSummaryAttempt = 0.0;
				entry.avatarUrl.clear();
			}
			else
			{
				entry.summaryFailed = true;
				const double flDelay = std::max(tResult.retryDelay, 15.0);
				entry.nextSummaryAttempt = SDK::PlatFloatTime() + flDelay;
			}
		}
	}

	if (entry.avatarFuture.valid() && entry.avatarFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
	{
		AvatarResult tResult = entry.avatarFuture.get();
		entry.avatarFuture = {};
		if (tResult.success && !tResult.pixels.empty())
		{
			entry.avatarPixels = std::make_shared<std::vector<uint8_t>>(std::move(tResult.pixels));
			entry.avatarWidth = tResult.width;
			entry.avatarHeight = tResult.height;
			entry.avatarResolved = true;
			entry.avatarFailed = false;
			entry.nextAvatarAttempt = 0.0;
			const uint64_t uSteamID64 = CSteamID(uAccountID, k_EUniversePublic, k_EAccountTypeIndividual).ConvertToUint64();
			const std::string sMessage = std::format("steamwebapi downloaded avatar for {} ({}x{})", uSteamID64, entry.avatarWidth, entry.avatarHeight);
			SDK::Output("steamwebapi", sMessage.c_str(), kLogColor, OUTPUT_CONSOLE | OUTPUT_DEBUG | OUTPUT_MENU);
			CSteamProfileCache::SaveAvatarToDisk(uAccountID, *entry.avatarPixels, entry.avatarWidth, entry.avatarHeight);
		}
		else
		{
			entry.avatarFailed = true;
			const double flDelay = std::max(tResult.retryDelay, 5.0);
			entry.nextAvatarAttempt = SDK::PlatFloatTime() + flDelay;
		}
	}
}

CSteamProfileCache::SummaryResult CSteamProfileCache::FetchSummary(uint32_t uAccountID, std::string sApiKey)
{
	SummaryResult tResult = {};
	if (!uAccountID || sApiKey.empty())
		return tResult;

	const uint64_t uSteamID64 = CSteamID(uAccountID, k_EUniversePublic, k_EAccountTypeIndividual).ConvertToUint64();
	const std::string sUrl = std::format("https://api.steampowered.com/ISteamUser/GetPlayerSummaries/v2/?key={}&steamids={}", sApiKey, uSteamID64);
	DWORD dwStatus = 0;
	std::vector<uint8_t> vData = DownloadUrl(SDK::ConvertUtf8ToWide(sUrl), &dwStatus);
	if (dwStatus && dwStatus != HTTP_STATUS_OK)
	{
		if (dwStatus == 420 || dwStatus == HTTP_STATUS_TOO_MANY_REQUESTS)
		{
			tResult.retryDelay = 120.0;
			const std::string sMessage = std::format("steamwebapi rate limited player summary lookups (HTTP {}). Cooling down before retrying.", dwStatus);
			LogThrottled(sMessage.c_str(), g_flNextSummaryFailLog, 10.0, kErrorColor);
		}
		else if (dwStatus == HTTP_STATUS_FORBIDDEN || dwStatus == HTTP_STATUS_UNAUTHORIZED)
		{
			tResult.retryDelay = 300.0;
			const std::string sMessage = std::format("steamwebapi rejected the configured key (HTTP {}). Double-check the key under Anticheat > Cheaters.", dwStatus);
			LogThrottled(sMessage.c_str(), g_flNextSummaryFailLog, 10.0, kErrorColor);
		}
		else
		{
			const std::string sMessage = std::format("steamwebapi returned HTTP {} while resolving player summaries.", dwStatus);
			LogThrottled(sMessage.c_str(), g_flNextSummaryFailLog, 10.0, kErrorColor);
		}
		return tResult;
	}
	if (vData.empty())
	{
		LogThrottled("steamwebapi returned an empty response when resolving player summaries.", g_flNextSummaryFailLog, 10.0, kErrorColor);
		return tResult;
	}

	bool bSawPlayersNode = false;
	try
	{
		std::stringstream ss;
		ss.write(reinterpret_cast<const char*>(vData.data()), static_cast<std::streamsize>(vData.size()));
		boost::property_tree::ptree tJson;
		boost::property_tree::read_json(ss, tJson);
		if (auto tResponse = tJson.get_child_optional("response.players"))
		{
			bSawPlayersNode = true;
			for (auto& [_, tNode] : *tResponse)
			{
				tResult.personaName = tNode.get<std::string>("personaname", "");
				tResult.avatarUrl = tNode.get<std::string>("avatarfull", tNode.get<std::string>("avatarmedium", ""));
				tResult.success = true;
				break;
			}
		}
	}
	catch (...)
	{
		LogThrottled("Failed to parse steamwebapi response for player summaries.", g_flNextSummaryFailLog, 10.0, kErrorColor);
	}

	if (!tResult.success)
	{
		if (bSawPlayersNode)
			tResult.permanentFailure = true;
		const std::string sMessage = std::format("steamwebapi returned no data for SteamID {}. The profile may be disabled (E43) or steamwebapi is having a stroke.", uSteamID64);
		LogThrottled(sMessage.c_str(), g_flNextSummaryFailLog, 10.0, kErrorColor);
	}

	return tResult;
}

CSteamProfileCache::AvatarResult CSteamProfileCache::FetchAvatar(uint32_t uAccountID, const std::string& sUrl)
{
	AvatarResult tResult = {};
	if (sUrl.empty())
		return tResult;

	DWORD dwStatus = 0;
	std::vector<uint8_t> vData = DownloadUrl(SDK::ConvertUtf8ToWide(sUrl), &dwStatus);
	if (dwStatus && dwStatus != HTTP_STATUS_OK)
	{
		const uint64_t uSteamID64 = CSteamID(uAccountID, k_EUniversePublic, k_EAccountTypeIndividual).ConvertToUint64();
		if (dwStatus == HTTP_STATUS_NOT_FOUND)
		{
			tResult.retryDelay = 900.0;
			const std::string sMessage = std::format("Steam CDN has no avatar for {} yet (HTTP 404). Will retry later.", uSteamID64);
			LogThrottled(sMessage.c_str(), g_flNextAvatarFailLog, 10.0, kErrorColor);
		}
		else if (dwStatus == 420 || dwStatus == HTTP_STATUS_TOO_MANY_REQUESTS)
		{
			tResult.retryDelay = 120.0;
			const std::string sMessage = std::format("Steam CDN rate limited avatar download for {} (HTTP {}). Cooling down before retrying.", uSteamID64, dwStatus);
			LogThrottled(sMessage.c_str(), g_flNextAvatarFailLog, 10.0, kErrorColor);
		}
		else
		{
			const std::string sMessage = std::format("steamwebapi returned HTTP {} while downloading an avatar image for {}.", dwStatus, uSteamID64);
			LogThrottled(sMessage.c_str(), g_flNextAvatarFailLog, 10.0, kErrorColor);
		}
		return tResult;
	}
	if (vData.empty())
	{
		const uint64_t uSteamID64 = CSteamID(uAccountID, k_EUniversePublic, k_EAccountTypeIndividual).ConvertToUint64();
		const std::string sMessage = std::format("steamwebapi returned an empty avatar payload for {}.", uSteamID64);
		LogThrottled(sMessage.c_str(), g_flNextAvatarFailLog, 10.0, kErrorColor);
		return tResult;
	}

	std::vector<uint8_t> vDecoded;
	uint32_t uWidth = 0, uHeight = 0;
	if (!DecodeImage(vData, vDecoded, uWidth, uHeight))
	{
		const uint64_t uSteamID64 = CSteamID(uAccountID, k_EUniversePublic, k_EAccountTypeIndividual).ConvertToUint64();
		const std::string sMessage = std::format("Failed to decode avatar image from steamwebapi for {}.", uSteamID64);
		LogThrottled(sMessage.c_str(), g_flNextAvatarFailLog, 10.0, kErrorColor);
		return tResult;
	}

	tResult.success = true;
	tResult.pixels = std::move(vDecoded);
	tResult.width = uWidth;
	tResult.height = uHeight;
	return tResult;
}
