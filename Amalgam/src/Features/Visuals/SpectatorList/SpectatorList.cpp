#include "SpectatorList.h"

#include "../../Players/PlayerUtils.h"
#include "../../Spectate/Spectate.h"

static float s_flCurrentHeight = 0.0f;

bool CSpectatorList::GetSpectators(CTFPlayer* pTarget)
{
	m_vSpectators.clear();

	auto pResource = H::Entities.GetResource();
	if (!pResource)
		return false;

	int iTarget = pTarget->entindex();
	for (int n = 1; n <= I::EngineClient->GetMaxClients(); n++)
	{
		auto pPlayer = I::ClientEntityList->GetClientEntity(n)->As<CTFPlayer>();
		bool bLocal = n == I::EngineClient->GetLocalPlayer();

		if (pResource->m_bValid(n) && !pResource->IsFakePlayer(n)
			&& pResource->m_iTeam(I::EngineClient->GetLocalPlayer()) != TEAM_SPECTATOR && pResource->m_iTeam(n) == TEAM_SPECTATOR)
		{
			m_vSpectators.emplace_back(F::PlayerUtils.GetPlayerName(n, pResource->GetName(n)), "possible", -1.f, false, n);
			continue;
		}

		if (pTarget->entindex() == n || pResource->IsFakePlayer(n)
			|| !pPlayer || !pPlayer->IsPlayer() || pPlayer->IsAlive()
			|| pTarget->IsDormant() != pPlayer->IsDormant()
			|| pResource->m_iTeam(iTarget) != pResource->m_iTeam(n))
		{
			if (m_mRespawnCache.contains(n))
				m_mRespawnCache.erase(n);
			continue;
		}

		int iObserverTarget = !pPlayer->IsDormant() ? pPlayer->m_hObserverTarget().GetEntryIndex() : iTarget;
		int iObserverMode = pPlayer->m_iObserverMode();
		if (bLocal && F::Spectate.m_iTarget != -1)
		{
			iObserverTarget = F::Spectate.m_hOriginalTarget.GetEntryIndex();
			iObserverMode = F::Spectate.m_iOriginalMode;
		}
		if (iObserverTarget != iTarget || bLocal && !I::EngineClient->IsPlayingDemo() && F::Spectate.m_iTarget == -1)
		{
			if (m_mRespawnCache.contains(n))
				m_mRespawnCache.erase(n);
			continue;
		}

		const char* sMode = "possible";
		if (!pPlayer->IsDormant())
		{
			switch (iObserverMode)
			{
			case OBS_MODE_FIRSTPERSON: sMode = "1st"; break;
			case OBS_MODE_THIRDPERSON: sMode = "3rd"; break;
			default: continue;
			}
		}

		float flRespawnTime = 0.f, flRespawnIn = -1.f;
		bool bRespawnTimeIncreased = false;
		if (pPlayer->IsInValidTeam())
		{
			flRespawnTime = pResource->m_flNextRespawnTime(n);
			flRespawnIn = std::max(floorf(flRespawnTime - TICKS_TO_TIME(I::ClientState->m_ClockDriftMgr.m_nServerTick)), 0.f);
			if (!m_mRespawnCache.contains(n))
				m_mRespawnCache[n] = flRespawnTime;
			else if (m_mRespawnCache[n] + 0.5f < flRespawnTime)
				bRespawnTimeIncreased = true;
		}

		m_vSpectators.emplace_back(F::PlayerUtils.GetPlayerName(n, pResource->GetName(n)), sMode, flRespawnIn, bRespawnTimeIncreased, n);
	}

	return !m_vSpectators.empty();
}

void CSpectatorList::Draw(CTFPlayer* pLocal)
{
	if (!(Vars::Menu::Indicators.Value & Vars::Menu::IndicatorsEnum::Spectators))
	{
		m_mRespawnCache.clear();
		s_flCurrentHeight = 0.0f;  // Reset height when disabled
		return;
	}

	auto pTarget = pLocal;
	switch (pLocal->m_iObserverMode())
	{
	case OBS_MODE_FIRSTPERSON:
	case OBS_MODE_THIRDPERSON:
		pTarget = pLocal->m_hObserverTarget()->As<CTFPlayer>();
	}
	if (!pTarget || !pTarget->IsPlayer()
		|| !GetSpectators(pTarget))
		return;

	int x = Vars::Menu::SpectatorsDisplay.Value.x;
	int y = Vars::Menu::SpectatorsDisplay.Value.y;
	const auto& fFont = H::Fonts.GetFont(FONT_INDICATORS);
	const int nTall = fFont.m_nTall + H::Draw.Scale(3);

	int maxTextWidth = 0;
	for (auto& Spectator : m_vSpectators)
	{
		int w = 0, h = 0;
		std::string text = std::format("{} ({} - respawn {}s)", Spectator.m_sName, Spectator.m_sMode, static_cast<int>(Spectator.m_flRespawnIn));
		I::MatSystemSurface->GetTextSize(fFont.m_dwFont, SDK::ConvertUtf8ToWide(text).c_str(), w, h);
		maxTextWidth = std::max(maxTextWidth, w);
	}

	int totalHeight = H::Draw.Scale(48);
	totalHeight += static_cast<int>(m_vSpectators.size()) * nTall;
	totalHeight += H::Draw.Scale(4); 

	s_flCurrentHeight = std::lerp(s_flCurrentHeight, static_cast<float>(totalHeight), I::GlobalVars->frametime * 10.0f);
	totalHeight = static_cast<int>(std::round(s_flCurrentHeight));

	const int boxWidth = std::max(H::Draw.Scale(220), maxTextWidth + H::Draw.Scale(40)); 
	const int cornerRadius = H::Draw.Scale(2); 
	
	Color_t tBackgroundColor = Vars::Menu::Theme::Background.Value;
	tBackgroundColor = tBackgroundColor.Lerp({ 127, 127, 127, tBackgroundColor.a }, 1.f / 9);
	tBackgroundColor.a = 255; // Make fully opaque
	
	Color_t tAccentColor = Vars::Menu::Theme::Accent.Value;
	Color_t tActiveColor = Vars::Menu::Theme::Active.Value;

	H::Draw.FillRoundRect(x, y, boxWidth, totalHeight, cornerRadius, tBackgroundColor);

	const float headerHeight = H::Draw.Scale(24);
	Color_t tHeaderBgColor = tBackgroundColor;
	tHeaderBgColor = { 
		static_cast<byte>(tBackgroundColor.r * 0.9f), 
		static_cast<byte>(tBackgroundColor.g * 0.9f), 
		static_cast<byte>(tBackgroundColor.b * 0.9f), 
		tBackgroundColor.a 
	};
	
	H::Draw.FillRoundRect(x, y, boxWidth, headerHeight, cornerRadius, tHeaderBgColor);
	H::Draw.String(fFont, x + H::Draw.Scale(16), y + H::Draw.Scale(5), tActiveColor, ALIGN_TOPLEFT, "Spec");
	int specWidth = 0, specHeight = 0;
	I::MatSystemSurface->GetTextSize(fFont.m_dwFont, L"Spec", specWidth, specHeight);
	H::Draw.String(fFont, x + H::Draw.Scale(16) + specWidth, y + H::Draw.Scale(5), tAccentColor, ALIGN_TOPLEFT, "tators");

	y += H::Draw.Scale(32);
	for (auto& Spectator : m_vSpectators)
	{
		Color_t tColor = tActiveColor;
		if (Spectator.m_bIsFriend)
			tColor = F::PlayerUtils.m_vTags[F::PlayerUtils.TagToIndex(FRIEND_TAG)].m_tColor;
		else if (Spectator.m_bInParty)
			tColor = F::PlayerUtils.m_vTags[F::PlayerUtils.TagToIndex(PARTY_TAG)].m_tColor;
		else if (Spectator.m_bRespawnTimeIncreased)
			tColor = F::PlayerUtils.m_vTags[F::PlayerUtils.TagToIndex(CHEATER_TAG)].m_tColor;
		else if (FNV1A::Hash32(Spectator.m_sMode) == FNV1A::Hash32Const("1st"))
			tColor = tColor.Lerp({ 255, 150, 0, 255 }, 0.5f);

		if (Spectator.m_bRespawnTimeIncreased || FNV1A::Hash32(Spectator.m_sMode) == FNV1A::Hash32Const("1st"))
		{
			Color_t tHighlightColor = tBackgroundColor;
			tHighlightColor = tHighlightColor.Lerp({ 255, 255, 255, tBackgroundColor.a }, 0.05f);
			H::Draw.FillRoundRect(x + H::Draw.Scale(12), y - H::Draw.Scale(2), boxWidth - H::Draw.Scale(24), nTall, H::Draw.Scale(2), tHighlightColor);
		}

		H::Draw.String(fFont, x + H::Draw.Scale(16), y, tColor, ALIGN_TOPLEFT, std::format("{} ({} - respawn {}s)", Spectator.m_sName.c_str(), Spectator.m_sMode, static_cast<int>(Spectator.m_flRespawnIn)).c_str());
		y += nTall;
	}
}