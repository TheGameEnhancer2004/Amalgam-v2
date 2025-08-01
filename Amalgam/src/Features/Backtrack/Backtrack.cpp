#include "Backtrack.h"

#include "../PacketManip/FakeLag/FakeLag.h"
#include "../Ticks/Ticks.h"

void CBacktrack::Reset()
{
	m_mRecords.clear();
	m_dSequences.clear();
	m_iLastInSequence = 0;
}


float CBacktrack::GetReal(int iFlow, bool bNoFake)
{
	auto pNetChan = I::EngineClient->GetNetChannelInfo();
	if (!pNetChan)
		return 0.f;

	if (iFlow != MAX_FLOWS)
		return pNetChan->GetLatency(iFlow) - (bNoFake && iFlow == FLOW_INCOMING ? GetFakeLatency() : 0.f);
	return pNetChan->GetLatency(FLOW_INCOMING) + pNetChan->GetLatency(FLOW_OUTGOING) - (bNoFake ? GetFakeLatency() : 0.f);
}

float CBacktrack::GetWishFake()
{
	return std::clamp(Vars::Backtrack::Latency.Value / 1000.f, 0.f, m_flMaxUnlag);
}

float CBacktrack::GetWishLerp()
{
	if (Vars::Misc::Game::AntiCheatCompatibility.Value)
		return std::clamp(Vars::Backtrack::Interp.Value / 1000.f, G::Lerp, 0.1f);

	return std::clamp(Vars::Backtrack::Interp.Value / 1000.f, G::Lerp, m_flMaxUnlag);
}

float CBacktrack::GetFakeLatency()
{
	return m_flFakeLatency;
}

float CBacktrack::GetFakeInterp()
{
	if (Vars::Misc::Game::AntiCheatCompatibility.Value)
		return std::min(m_flFakeInterp, 0.1f);

	return m_flFakeInterp;
}

float CBacktrack::GetWindow()
{
	return Vars::Backtrack::Window.Value / 1000.f;
}

int CBacktrack::GetAnticipatedChoke(int iMethod)
{
	int iAnticipatedChoke = 0;
	if (F::Ticks.CanChoke() && G::PrimaryWeaponType != EWeaponType::HITSCAN && Vars::Aimbot::General::AimType.Value == Vars::Aimbot::General::AimTypeEnum::Silent)
		iAnticipatedChoke = 1;
	if (F::FakeLag.m_iGoal && !Vars::Fakelag::UnchokeOnAttack.Value && F::Ticks.m_iShiftedTicks == F::Ticks.m_iShiftedGoal && !F::Ticks.m_bDoubletap && !F::Ticks.m_bSpeedhack)
		iAnticipatedChoke = F::FakeLag.m_iGoal - I::ClientState->chokedcommands; // iffy, unsure if there is a good way to get it to work well without unchoking
	return iAnticipatedChoke;
}

void CBacktrack::SendLerp()
{
	static Timer tTimer = {};
	if (!tTimer.Run(0.1f))
		return;

	float flTarget = GetWishLerp();
	if (m_flSentInterp != flTarget)
	{
		m_flSentInterp = flTarget;

		auto pNetChan = reinterpret_cast<CNetChannel*>(I::EngineClient->GetNetChannelInfo());
		if (pNetChan && I::EngineClient->IsConnected())
		{
			NET_SetConVar tConvar1 = { "cl_interp", std::to_string(m_flSentInterp).c_str() };
			pNetChan->SendNetMsg(tConvar1);

			NET_SetConVar tConvar2 = { "cl_interp_ratio", "1" };
			pNetChan->SendNetMsg(tConvar2);

			NET_SetConVar tConvar3 = { "cl_interpolate", "1" };
			pNetChan->SendNetMsg(tConvar3);
		}
	}
}

void CBacktrack::SetLerp(IGameEvent* pEvent)
{
	if (I::EngineClient->GetPlayerForUserID(pEvent->GetInt("userid")) == I::EngineClient->GetLocalPlayer())
		m_flFakeInterp = m_flSentInterp;
}

void CBacktrack::UpdateDatagram()
{
	auto pNetChan = reinterpret_cast<CNetChannel*>(I::EngineClient->GetNetChannelInfo());
	if (!pNetChan)
		return;

	if (auto pLocal = H::Entities.GetLocal())
		m_nOldTickBase = pLocal->m_nTickBase();

	if (pNetChan->m_nInSequenceNr > m_iLastInSequence)
	{
		m_iLastInSequence = pNetChan->m_nInSequenceNr;
		m_dSequences.emplace_front(pNetChan->m_nInReliableState, pNetChan->m_nInSequenceNr, I::GlobalVars->realtime);
	}

	if (m_dSequences.size() > 67)
		m_dSequences.pop_back();
}



bool CBacktrack::GetRecords(CBaseEntity* pEntity, std::vector<TickRecord*>& vReturn)
{
	if (!m_mRecords.contains(pEntity))
		return false;

	auto& vRecords = m_mRecords[pEntity];
	for (auto& tRecord : vRecords)
		vReturn.push_back(&tRecord);
	return true;
}

std::vector<TickRecord*> CBacktrack::GetValidRecords(std::vector<TickRecord*>& vRecords, CTFPlayer* pLocal, bool bDistance, float flTimeMod)
{
	if (vRecords.empty())
		return {};

	auto pNetChan = I::EngineClient->GetNetChannelInfo();
	if (!pNetChan)
		return {};

	std::vector<TickRecord*> vReturn = {};
	float flCorrect = std::clamp(GetReal(MAX_FLOWS, false) + ROUND_TO_TICKS(GetFakeInterp()), 0.f, m_flMaxUnlag);
	int iServerTick = m_iTickCount + GetAnticipatedChoke() + Vars::Backtrack::Offset.Value + TIME_TO_TICKS(GetReal(FLOW_OUTGOING));

	if (!Vars::Misc::Game::AntiCheatCompatibility.Value && GetWindow())
	{
		for (auto pRecord : vRecords)
		{
			float flDelta = fabsf(flCorrect - (TICKS_TO_TIME(iServerTick) - (pRecord->m_flSimTime + flTimeMod)));
			if (flDelta > GetWindow())
				continue;

			vReturn.push_back(pRecord);
		}
	}

	if (vReturn.empty())
	{	// make sure there is at least 1 record
		float flMinDelta = 0.2f;
		for (auto pRecord : vRecords)
		{
			float flDelta = fabsf(flCorrect - TICKS_TO_TIME(iServerTick - TIME_TO_TICKS(pRecord->m_flSimTime + flTimeMod)));
			if (flDelta > flMinDelta)
				continue;

			flMinDelta = flDelta;
			vReturn = { pRecord };
		}
	}
	else if (pLocal && vReturn.size() > 1)
	{
		if (bDistance)
			std::sort(vReturn.begin(), vReturn.end(), [&](const TickRecord* a, const TickRecord* b) -> bool
				{
					if (Vars::Backtrack::PreferOnShot.Value && a->m_bOnShot != b->m_bOnShot)
						return a->m_bOnShot > b->m_bOnShot;

					return pLocal->m_vecOrigin().DistTo(a->m_vOrigin) < pLocal->m_vecOrigin().DistTo(b->m_vOrigin);
				});
		else
		{
			std::sort(vReturn.begin(), vReturn.end(), [&](const TickRecord* a, const TickRecord* b) -> bool
				{
					if (Vars::Backtrack::PreferOnShot.Value && a->m_bOnShot != b->m_bOnShot)
						return a->m_bOnShot > b->m_bOnShot;

					const float flADelta = flCorrect - (TICKS_TO_TIME(iServerTick - (a->m_flSimTime + flTimeMod)));
					const float flBDelta = flCorrect - (TICKS_TO_TIME(iServerTick - (b->m_flSimTime + flTimeMod)));
					return fabsf(flADelta) < fabsf(flBDelta);
				});
		}
	}

	return vReturn;
}



void CBacktrack::MakeRecords()
{
	for (auto& pEntity : H::Entities.GetGroup(EGroupType::PLAYERS_ALL))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		if (pPlayer->entindex() == I::EngineClient->GetLocalPlayer() || pPlayer->IsDormant() || !pPlayer->IsAlive() || pPlayer->IsAGhost()
			|| !H::Entities.GetDeltaTime(pPlayer->entindex()))
			continue;
			
		auto pBones = H::Entities.GetBones(pPlayer->entindex());
		if (!pBones) continue;
		auto pModel = pPlayer->GetModel();
		if (!pModel) continue;
		auto pHDR = I::ModelInfoClient->GetStudiomodel(pModel);
		if (!pHDR) continue;
		auto pSet = pHDR->pHitboxSet(pEntity->As<CBaseAnimating>()->m_nHitboxSet());
		if (!pSet) continue;

		std::vector<HitboxInfo> vHitboxInfos{};
		auto tBoneMatrix = *reinterpret_cast<BoneMatrix*>(pBones);

		for (int nHitbox = 0; nHitbox < pPlayer->GetNumOfHitboxes(); nHitbox++)
		{
			auto pBox = pSet->pHitbox(nHitbox);
			if (!pBox) continue;

			const Vec3 iMin = pBox->bbmin, iMax = pBox->bbmax;
			const int iBone = pBox->bone;
			Vec3 vCenter{};
			Math::VectorTransform((iMin + iMax) / 2, tBoneMatrix.m_aBones[iBone], vCenter);
			vHitboxInfos.push_back({ iBone, nHitbox, vCenter, iMin, iMax });
		}

		auto& vRecords = m_mRecords[pPlayer];

		const TickRecord* pLastRecord = !vRecords.empty() ? &vRecords.front() : nullptr;
		vRecords.emplace_front(
			pPlayer->m_flSimulationTime(),
			pPlayer->m_vecOrigin(),
			pPlayer->m_vecMins(),
			pPlayer->m_vecMaxs(),
			tBoneMatrix,
			vHitboxInfos,
			m_mDidShoot[pPlayer->entindex()],
			pPlayer->m_vecOrigin()
		);
		const TickRecord& tCurRecord = vRecords.front();

		bool bLagComp = false;
		if (pLastRecord)
		{
			const Vec3 vDelta = tCurRecord.m_vBreak - pLastRecord->m_vBreak;

			static auto sv_lagcompensation_teleport_dist = U::ConVars.FindVar("sv_lagcompensation_teleport_dist");
			const float flDist = powf(sv_lagcompensation_teleport_dist->GetFloat(), 2.f);
			if (vDelta.Length2DSqr() > flDist)
			{
				bLagComp = true;
				if (!H::Entities.GetLagCompensation(pPlayer->entindex()))
				{
					vRecords.resize(1);
					vRecords.front().m_flSimTime = std::numeric_limits<float>::max(); // hack
				}
				std::for_each(vRecords.begin(), vRecords.end(), [](auto& tRecord) { tRecord.m_bInvalid = true; });
			}

			for (auto& tRecord : vRecords)
			{
				if (!tRecord.m_bInvalid)
					continue;

				tRecord.m_vOrigin = tCurRecord.m_vOrigin;
				tRecord.m_vMins = tCurRecord.m_vMins;
				tRecord.m_vMaxs = tCurRecord.m_vMaxs;
				tRecord.m_BoneMatrix = tCurRecord.m_BoneMatrix;
				tRecord.m_vHitboxInfos = tCurRecord.m_vHitboxInfos;
				tRecord.m_bOnShot = tCurRecord.m_bOnShot;
			}
		}

		H::Entities.SetLagCompensation(pPlayer->entindex(), bLagComp);
		m_mDidShoot[pPlayer->entindex()] = false;
	}
}

void CBacktrack::CleanRecords()
{
	for (auto& pEntity : H::Entities.GetGroup(EGroupType::PLAYERS_ALL))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		if (pPlayer->entindex() == I::EngineClient->GetLocalPlayer())
			continue;

		auto& vRecords = m_mRecords[pPlayer];

		if (!pPlayer->IsAlive() || pPlayer->IsAGhost())
		{
			vRecords.clear();
			continue;
		}

		//const int iOldSize = pRecords.size();

		const int flDeadtime = I::GlobalVars->curtime + GetReal() - m_flMaxUnlag; // int ???
		if (vRecords.size() > 1 && vRecords.back().m_flSimTime == std::numeric_limits<float>::max())
			vRecords.pop_back();
		while (!vRecords.empty())
		{
			if (vRecords.back().m_flSimTime < flDeadtime || vRecords.size() > 1 && vRecords.back().m_flSimTime == std::numeric_limits<float>::max())
				vRecords.pop_back();
			else
				break;
		}

		//const int iNewSize = pRecords.size();
		//if (iOldSize != iNewSize)
		//	SDK::Output("Clear", std::format("{} -> {}", iOldSize, iNewSize).c_str(), { 255, 0, 200 }, Vars::Debug::Logging.Value);
	}
}



void CBacktrack::Store()
{
	UpdateDatagram();
	if (!I::EngineClient->IsInGame())
		return;

	static auto sv_maxunlag = U::ConVars.FindVar("sv_maxunlag");
	m_flMaxUnlag = sv_maxunlag->GetFloat();
	
	MakeRecords();
	CleanRecords();
}

void CBacktrack::ResolverUpdate(CBaseEntity* pEntity)
{
	/*
	if (!m_mRecords.contains(pEntity))
		return;

	m_mRecords[pEntity].clear();
	*/
}

void CBacktrack::ReportShot(int iIndex)
{
	if (!Vars::Backtrack::PreferOnShot.Value)
		return;

	auto pEntity = I::ClientEntityList->GetClientEntity(iIndex);
	if (!pEntity || SDK::GetWeaponType(pEntity->As<CTFPlayer>()->m_hActiveWeapon()->As<CTFWeaponBase>()) != EWeaponType::HITSCAN)
		return;

	m_mDidShoot[pEntity->entindex()] = true;
}

void CBacktrack::AdjustPing(CNetChannel* pNetChan)
{
	m_nOldInSequenceNr = pNetChan->m_nInSequenceNr, m_nOldInReliableState = pNetChan->m_nInReliableState;

	auto Set = [&]()
		{
			if (!Vars::Backtrack::Latency.Value)
				return 0.f;

			auto pLocal = H::Entities.GetLocal();
			if (!pLocal || !pLocal->m_iClass())
				return 0.f;

			static auto host_timescale = U::ConVars.FindVar("host_timescale");
			float flTimescale = host_timescale->GetFloat();

			static float flStaticReal = 0.f;
			float flFake = GetWishFake(), flReal = TICKS_TO_TIME(pLocal->m_nTickBase() - m_nOldTickBase);
			flStaticReal += (flReal + 5 * TICK_INTERVAL - flStaticReal) * 0.1f;

			int nInReliableState = pNetChan->m_nInReliableState, nInSequenceNr = pNetChan->m_nInSequenceNr; float flLatency = 0.f;
			for (auto& cSequence : m_dSequences)
			{
				nInReliableState = cSequence.m_nInReliableState;
				nInSequenceNr = cSequence.m_nSequenceNr;
				flLatency = (I::GlobalVars->realtime - cSequence.m_flTime) * flTimescale - TICK_INTERVAL;

				if (flLatency > flFake || m_nLastInSequenceNr >= cSequence.m_nSequenceNr || flLatency > m_flMaxUnlag - flStaticReal)
					break;
			}
			if (flLatency > 1.f) // hacky failsafe
				return 0.f;

			pNetChan->m_nInReliableState = nInReliableState;
			pNetChan->m_nInSequenceNr = nInSequenceNr;
			return flLatency;
		};

	auto flLatency = Set();
	m_nLastInSequenceNr = pNetChan->m_nInSequenceNr;

	if (Vars::Backtrack::Latency.Value || m_flFakeLatency)
	{
		m_flFakeLatency = std::clamp(m_flFakeLatency + (flLatency - m_flFakeLatency) * 0.1f, m_flFakeLatency - TICK_INTERVAL, m_flFakeLatency + TICK_INTERVAL);
		if (!flLatency && m_flFakeLatency < TICK_INTERVAL)
			m_flFakeLatency = 0.f;
	}
}

void CBacktrack::RestorePing(CNetChannel* pNetChan)
{
	pNetChan->m_nInSequenceNr = m_nOldInSequenceNr, pNetChan->m_nInReliableState = m_nOldInReliableState;
}

std::optional<TickRecord> CBacktrack::GetHitRecord(CBaseEntity* pEntity, CTFWeaponBase* pWeapon, CUserCmd* pCmd, CrosshairRecordInfo_t& InfoOut, const Vec3 vAngles, const Vec3 vPos)
{
	std::optional<TickRecord> pReturnRecord{};
	float flMinFov = 45.f;
	float flMinDist = 50.f;
	bool bInsideRecord = false;
	std::vector<TickRecord*> vRecords;
	if (GetRecords(pEntity, vRecords))
	{
		vRecords = F::Backtrack.GetValidRecords(vRecords);
		for(auto pRecord : vRecords)
		{
			if (!pRecord->m_BoneMatrix.m_aBones)
				continue;

			for (int n = 0; n < pRecord->m_vHitboxInfos.size(); n++)
			{
				auto sHitboxInfo = pRecord->m_vHitboxInfos[n];

				// pSet->pHitbox failed, this hitbox cannot be used
				if (sHitboxInfo.m_iBone == -1)
					continue;

				const auto vAngleTo = Math::CalcAngle(vPos, sHitboxInfo.m_vCenter);
				const float flFOVTo = Math::CalcFov(vAngles, vAngleTo);
				const float flDistTo = vPos.DistTo(sHitboxInfo.m_vCenter);

				// We are inside this record (probably)
				if (flDistTo < flMinDist)
				{
					pReturnRecord = *pRecord;
					flMinDist = flDistTo;
					bInsideRecord = true;
				}
				else if (!bInsideRecord && flFOVTo < flMinFov)
				{
					pReturnRecord = *pRecord;
					flMinFov = flFOVTo;
				}
			}
		}
	}

	InfoOut = { flMinDist, flMinFov, bInsideRecord };
	return pReturnRecord;
}

void CBacktrack::BacktrackToCrosshair(CUserCmd* pCmd)
{
	auto pLocal = H::Entities.GetLocal();
	if (!pCmd || !pLocal || !pLocal->IsAlive() || pLocal->IsAGhost() || pLocal->InCond(TF_COND_HALLOWEEN_KART))
		return;

	auto pWeapon = H::Entities.GetWeapon();
	if (!pWeapon)
		return;

	if (G::Attacking == 1 && G::CanPrimaryAttack)
	{
		const Vec3 vShootPos = pLocal->GetShootPos();
		const Vec3 vAngles = pCmd->viewangles;

		std::vector<std::pair<TickRecord, CrosshairRecordInfo_t>> vValidRecords;
		for (auto pEntity : H::Entities.GetGroup(EGroupType::PLAYERS_ENEMIES))
		{
			if (!pEntity)
				continue;

			if (/*pEntity->IsDormant() || */!pEntity->As<CTFPlayer>()->IsAlive() || pEntity->As<CTFPlayer>()->IsAGhost() || pEntity->As<CTFPlayer>()->IsInvulnerable())
				continue;

			CrosshairRecordInfo_t sRecordInfo{};
			if (auto pCheckRec = GetHitRecord(pEntity, pWeapon, pCmd, sRecordInfo, vAngles, vShootPos))
				vValidRecords.push_back({ *pCheckRec, sRecordInfo });
		}

		std::optional<TickRecord> pReturnTick{};
		if (!vValidRecords.empty())
		{
			auto pFinalTick = std::ranges::min_element(vValidRecords,
				[&](const std::pair<TickRecord, CrosshairRecordInfo_t>& a, const std::pair<TickRecord, CrosshairRecordInfo_t>& b)
				{
					const bool bInsideBoth = a.second.bInsideThisRecord && b.second.bInsideThisRecord;
					const bool bNotInsideRecords = !a.second.bInsideThisRecord && !b.second.bInsideThisRecord;

					const bool bResult =
					{
							bInsideBoth ? a.second.flMinDist < b.second.flMinDist :
							bNotInsideRecords ? a.second.flFov < b.second.flFov :
							a.second.bInsideThisRecord
					};
					return bResult;
				});
			pReturnTick = pFinalTick->first;
		}

		if (pReturnTick)
			pCmd->tick_count = TIME_TO_TICKS(pReturnTick->m_flSimTime + m_flFakeInterp);
	}
}

void CBacktrack::Draw(CTFPlayer* pLocal)
{
	if (!(Vars::Menu::Indicators.Value & Vars::Menu::IndicatorsEnum::Ping) || !pLocal->IsAlive())
		return;

	auto pResource = H::Entities.GetPR();
	auto pNetChan = I::EngineClient->GetNetChannelInfo();
	if (!pResource || !pNetChan)
		return;

	static float flFakeLatency = 0.f;
	{
		static Timer tTimer = {};
		if (tTimer.Run(0.5f))
			flFakeLatency = GetFakeLatency();
	}
	float flFakeLerp = GetFakeInterp() > G::Lerp ? GetFakeInterp() : 0.f;

	float flFake = std::min(flFakeLatency + flFakeLerp, m_flMaxUnlag) * 1000;
	float flLatency = std::max(pNetChan->GetLatency(FLOW_INCOMING) + pNetChan->GetLatency(FLOW_OUTGOING) - flFakeLatency, 0.f) * 1000;
	int iLatencyScoreboard = pResource->m_iPing(pLocal->entindex());

	int x = Vars::Menu::PingDisplay.Value.x;
	int y = Vars::Menu::PingDisplay.Value.y + 8;
	const auto& fFont = H::Fonts.GetFont(FONT_INDICATORS);
	const int nTall = fFont.m_nTall + H::Draw.Scale(1);

	EAlign align = ALIGN_TOP;
	if (x <= 100 + H::Draw.Scale(50, Scale_Round))
	{
		x -= H::Draw.Scale(42, Scale_Round);
		align = ALIGN_TOPLEFT;
	}
	else if (x >= H::Draw.m_nScreenW - 100 - H::Draw.Scale(50, Scale_Round))
	{
		x += H::Draw.Scale(42, Scale_Round);
		align = ALIGN_TOPRIGHT;
	}

	if (flFake || Vars::Backtrack::Interp.Value > G::Lerp * 1000)
		H::Draw.StringOutlined(fFont, x, y, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value, align, std::format("Ping {:.0f} (+ {:.0f}) ms", flLatency, flFake).c_str());
	else
		H::Draw.StringOutlined(fFont, x, y, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value, align, std::format("Ping {:.0f} ms", flLatency).c_str());
	H::Draw.StringOutlined(fFont, x, y += nTall, Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value, align, std::format("Scoreboard {} ms", iLatencyScoreboard).c_str());
}