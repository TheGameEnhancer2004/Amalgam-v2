#include "CheaterDetection.h"

#include "../Players/PlayerUtils.h"
#include "../Output/Output.h"

#include <algorithm>

bool CCheaterDetection::ShouldScan()
{
	if (!Vars::CheaterDetection::Methods.Value /*|| I::EngineClient->IsPlayingDemo()*/)
		return false;

	static int iStaticTickcount = I::GlobalVars->tickcount;
	const int iLastTickcount = iStaticTickcount;
	const int iCurrTickcount = iStaticTickcount = I::GlobalVars->tickcount;
	if (iCurrTickcount != iLastTickcount + 1)
		return false;

	auto pNetChan = I::EngineClient->GetNetChannelInfo();
	if (pNetChan && (pNetChan->GetTimeSinceLastReceived() > TICK_INTERVAL * 2 || pNetChan->IsTimingOut()))
		return false;

	return true;
}

bool CCheaterDetection::InvalidPitch(CTFPlayer* pEntity)
{
	return Vars::CheaterDetection::Methods.Value & Vars::CheaterDetection::MethodsEnum::InvalidPitch && fabsf(pEntity->m_angEyeAnglesX()) == 90.f;
}

bool CCheaterDetection::IsChoking(CTFPlayer* pEntity)
{
	bool bReturn = mData[pEntity].m_PacketChoking.m_bInfract;
	mData[pEntity].m_PacketChoking.m_bInfract = false;

	return Vars::CheaterDetection::Methods.Value & Vars::CheaterDetection::MethodsEnum::PacketChoking && bReturn;
}

bool CCheaterDetection::IsFlicking(CTFPlayer* pEntity) // awful
{
	auto& vAngles = mData[pEntity].m_AimFlicking.m_vAngles;
	if (!(Vars::CheaterDetection::Methods.Value & Vars::CheaterDetection::MethodsEnum::AimFlicking))
	{
		vAngles.clear();
		return false;
	}

	vAngles.emplace_front(pEntity->GetEyeAngles(), false);
	if (vAngles.size() > 3)
		vAngles.pop_back();

	if (vAngles.size() != 3 || !vAngles[0].m_bAttacking && !vAngles[1].m_bAttacking && !vAngles[2].m_bAttacking
		|| Math::CalcFov(vAngles[0].m_vAngle, vAngles[1].m_vAngle) < Vars::CheaterDetection::MinimumFlick.Value
		|| Math::CalcFov(vAngles[0].m_vAngle, vAngles[2].m_vAngle) > Vars::CheaterDetection::MaximumNoise.Value * (TICK_INTERVAL / 0.015f))
		return false;

	vAngles.clear();
	return true;
}

bool CCheaterDetection::IsDuckSpeed(CTFPlayer* pEntity)
{
	if (!(Vars::CheaterDetection::Methods.Value & Vars::CheaterDetection::MethodsEnum::DuckSpeed)
		|| !pEntity->IsDucking() || !pEntity->IsOnGround()
		|| pEntity->m_vecVelocity().Length2D() < pEntity->m_flMaxspeed() * 0.5f)
	{
		mData[pEntity].m_DuckSpeed.m_iStartTick = 0;
		return false;
	}

	if (!mData[pEntity].m_DuckSpeed.m_iStartTick)
		mData[pEntity].m_DuckSpeed.m_iStartTick = I::GlobalVars->tickcount;

	if (I::GlobalVars->tickcount - mData[pEntity].m_DuckSpeed.m_iStartTick > TIME_TO_TICKS(1))
	{
		mData[pEntity].m_DuckSpeed.m_iStartTick = 0;
		return true;
	}

	return false;
}

bool CCheaterDetection::IsLagCompAbusing(CTFPlayer* pEntity, int iDeltaTicks)
{
	auto& tLagComp = mData[pEntity].m_PacketChoking.m_LagComp;
	if (!(Vars::CheaterDetection::Methods.Value & Vars::CheaterDetection::MethodsEnum::LagCompAbuse))
	{
		tLagComp = {};
		return false;
	}

	const int iMinDelta = std::max(2, Vars::CheaterDetection::LagCompMinimumDelta.Value);
	const int iWindowTicks = std::max(1, TIME_TO_TICKS(std::max(0.1f, Vars::CheaterDetection::LagCompWindow.Value)));
	const int iRequiredBursts = std::max(1, Vars::CheaterDetection::LagCompBurstCount.Value);

	if (iDeltaTicks <= iMinDelta)
	{
		return false;
	}

	tLagComp.m_vBurstTicks.emplace_back(I::GlobalVars->tickcount);
	tLagComp.m_vDeltaCmds.emplace_back(iDeltaTicks);

	while (!tLagComp.m_vBurstTicks.empty() && I::GlobalVars->tickcount - tLagComp.m_vBurstTicks.front() > iWindowTicks)
	{
		tLagComp.m_vBurstTicks.pop_front();
		tLagComp.m_vDeltaCmds.pop_front();
	}

	if ((int)tLagComp.m_vBurstTicks.size() >= iRequiredBursts)
	{
		tLagComp.m_vBurstTicks.clear();
		tLagComp.m_vDeltaCmds.clear();
		tLagComp.m_bInfract = true;
	}

	bool bReturn = tLagComp.m_bInfract;
	tLagComp.m_bInfract = false;
	return bReturn;
}

bool CCheaterDetection::IsCritManipulating(CTFPlayer* pEntity)
{
	auto& tCritTracker = mData[pEntity].m_CritTracker;
	if (!(Vars::CheaterDetection::Methods.Value & Vars::CheaterDetection::MethodsEnum::CritManipulation))
	{
		tCritTracker = {};
		return false;
	}

	bool bReturn = tCritTracker.m_bInfract;
	tCritTracker.m_bInfract = false;
	return bReturn;
}

void CCheaterDetection::TrackCritEvent(CTFPlayer* pEntity, CTFWeaponBase* pWeapon, bool bCrit)
{
	if (!(Vars::CheaterDetection::Methods.Value & Vars::CheaterDetection::MethodsEnum::CritManipulation) || !pWeapon)
		return;

	auto& tCritTracker = mData[pEntity].m_CritTracker;

	if (pEntity->IsCritBoosted())
	{
		tCritTracker.m_mWeaponHistory.erase(pWeapon->GetWeaponID());
		return;
	}

	auto& tHistory = tCritTracker.m_mWeaponHistory[pWeapon->GetWeaponID()];
	tHistory.m_vHistory.emplace_back(bCrit);
	if (bCrit)
		tHistory.m_iCrits++;

	const int iWindow = std::max(1, Vars::CheaterDetection::CritWindow.Value);
	while ((int)tHistory.m_vHistory.size() > iWindow)
	{
		if (tHistory.m_vHistory.front())
			tHistory.m_iCrits--;
		tHistory.m_vHistory.pop_front();
	}

	if ((int)tHistory.m_vHistory.size() < iWindow)
		return;

	const float flCritRate = (float(tHistory.m_iCrits) / float(tHistory.m_vHistory.size())) * 100.f;
	if (flCritRate >= Vars::CheaterDetection::CritThreshold.Value)
	{
		tHistory.m_vHistory.clear();
		tHistory.m_iCrits = 0;
		tCritTracker.m_bInfract = true;
	}
}

void CCheaterDetection::Infract(CTFPlayer* pEntity, const char* sReason)
{
	bool bMark = false;
	if (Vars::CheaterDetection::DetectionsRequired.Value)
	{
		mData[pEntity].m_iDetections++;
		bMark = mData[pEntity].m_iDetections >= Vars::CheaterDetection::DetectionsRequired.Value;
	}

	F::Output.CheatDetection(mData[pEntity].m_sName, bMark ? "marked" : "infracted", sReason);
	if (bMark)
	{
		const int iDetections = std::max(mData[pEntity].m_iDetections, Vars::CheaterDetection::DetectionsRequired.Value);
		mData[pEntity].m_iDetections = 0;
		F::PlayerUtils.AddTag(
			mData[pEntity].m_uAccountID,
			F::PlayerUtils.TagToIndex(CHEATER_TAG),
			true,
			mData[pEntity].m_sName,
			sReason,
			iDetections,
			true);
	}
}

void CCheaterDetection::Run()
{
	if (!ShouldScan() || !I::EngineClient->IsConnected() || I::EngineClient->IsPlayingDemo())
		return;

	auto pResource = H::Entities.GetResource();
	if (!pResource)
		return;

	for (auto& pEntity : H::Entities.GetGroup(EntityEnum::PlayerAll))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		int iIndex = pPlayer->entindex();
		float flDeltaTime = H::Entities.GetDeltaTime(iIndex);
		if (!flDeltaTime)
			continue;

		const int iDeltaTicks = TIME_TO_TICKS(flDeltaTime);

		if (iIndex == I::EngineClient->GetLocalPlayer() || !pPlayer->IsAlive() || pPlayer->IsAGhost()
			|| pResource->IsFakePlayer(iIndex) || F::PlayerUtils.HasTag(iIndex, F::PlayerUtils.TagToIndex(CHEATER_TAG)))
		{
			mData[pPlayer].m_PacketChoking = {};
			mData[pPlayer].m_AimFlicking = {};
			mData[pPlayer].m_DuckSpeed = {};
			mData[pPlayer].m_CritTracker = {};
			continue;
		}

		mData[pPlayer].m_uAccountID = pResource->m_iAccountID(iIndex);
		mData[pPlayer].m_sName = F::PlayerUtils.GetPlayerName(iIndex, pResource->GetName(iIndex));

		if (InvalidPitch(pPlayer))
			Infract(pPlayer, "invalid pitch");
		if (IsChoking(pPlayer))
			Infract(pPlayer, "choking packets");
		if (IsFlicking(pPlayer))
			Infract(pPlayer, "flicking");
		if (IsDuckSpeed(pPlayer))
			Infract(pPlayer, "duck speed");
		if (IsLagCompAbusing(pPlayer, iDeltaTicks))
			Infract(pPlayer, "lag-comp abuse");
		if (IsCritManipulating(pPlayer))
			Infract(pPlayer, "crit manipulation");
	}
}

void CCheaterDetection::Reset()
{
	mData.clear();
}

void CCheaterDetection::ReportChoke(CTFPlayer* pEntity, int iChoke)
{
	if (Vars::CheaterDetection::Methods.Value & Vars::CheaterDetection::MethodsEnum::PacketChoking)
	{
		mData[pEntity].m_PacketChoking.m_vChokes.push_back(iChoke);
		if (mData[pEntity].m_PacketChoking.m_vChokes.size() == 3)
		{
			mData[pEntity].m_PacketChoking.m_bInfract = true; // check for last 3 choke amounts
			for (auto& iChoke : mData[pEntity].m_PacketChoking.m_vChokes)
			{
				if (iChoke < Vars::CheaterDetection::MinimumChoking.Value)
					mData[pEntity].m_PacketChoking.m_bInfract = false;
			}
			mData[pEntity].m_PacketChoking.m_vChokes.clear();
		}
	}
	else
		mData[pEntity].m_PacketChoking.m_vChokes.clear();
}

void CCheaterDetection::ReportDamage(IGameEvent* pEvent)
{
	const bool bAimFlicking = Vars::CheaterDetection::Methods.Value & Vars::CheaterDetection::MethodsEnum::AimFlicking;
	const bool bCritTracking = Vars::CheaterDetection::Methods.Value & Vars::CheaterDetection::MethodsEnum::CritManipulation;
	if (!bAimFlicking && !bCritTracking)
		return;

	int iIndex = I::EngineClient->GetPlayerForUserID(pEvent->GetInt("attacker"));
	if (iIndex == I::EngineClient->GetLocalPlayer())
		return;

	auto pEntity = I::ClientEntityList->GetClientEntity(iIndex)->As<CTFPlayer>();
	if (!pEntity || !pEntity->IsPlayer() || pEntity->IsDormant())
		return;

	auto pWeapon = pEntity->m_hActiveWeapon()->As<CTFWeaponBase>();
	switch (SDK::GetWeaponType(pWeapon))
	{
	case EWeaponType::UNKNOWN:
	case EWeaponType::PROJECTILE:
		return;
	}

	if (bAimFlicking)
	{
		auto& vAngles = mData[pEntity].m_AimFlicking.m_vAngles;
		if (!vAngles.empty())
			vAngles.back().m_bAttacking = true;
	}

	if (bCritTracking)
		TrackCritEvent(pEntity, pWeapon, pEvent->GetBool("crit"));
}