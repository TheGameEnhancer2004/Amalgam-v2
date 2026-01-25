#include "Entities.h"

#include "../../SDK.h"
#include "../../../Utils/Hash/FNV1A.h"
#include "../../../Features/Players/PlayerUtils.h"
#include "../../../Features/Backtrack/Backtrack.h"
#include "../../../Features/CheaterDetection/CheaterDetection.h"
#include "../../../Features/Resolver/Resolver.h"

bool CEntities::UpdatePlayerDetails(int nIndex, CTFPlayer* pPlayer, int iLag)
{
	bool bDormant = ManageDormancy(nIndex, pPlayer);
	float flSimTime = pPlayer->m_flSimulationTime();
	float flOldSimTime = pPlayer->m_flOldSimulationTime();

	if (float flDeltaTime = m_mDeltaTimes[nIndex] = TICKS_TO_TIME(std::clamp(TIME_TO_TICKS(flSimTime - flOldSimTime) - iLag, 0, 24)))
	{
		m_mLagTimes[nIndex] = flDeltaTime;
		m_mSetTicks[nIndex] = I::GlobalVars->tickcount;

		if (!bDormant)
		{
			m_mOrigins[nIndex].emplace_front(pPlayer->m_vecOrigin() + Vec3(0, 0, pPlayer->GetSize().z), flSimTime);
			if (m_mOrigins[nIndex].size() > (size_t)Vars::Aimbot::Projectile::VelocityAverageCount.Value)
				m_mOrigins[nIndex].pop_back();

			if (pPlayer->IsAlive())
				F::CheaterDetection.ReportChoke(pPlayer, m_mChokes[nIndex]);
		}
		else 
			m_mOrigins[nIndex].clear(); 

		m_mOldAngles[nIndex] = m_mEyeAngles[nIndex];
		m_mEyeAngles[nIndex] = pPlayer->GetEyeAngles();
	}
	m_mChokes[nIndex] = std::max(0, I::GlobalVars->tickcount - m_mSetTicks[nIndex]);

	return !bDormant;
}

void CEntities::UpdatePartyAndLobbyInfo(int nLocalIndex)
{
	static Timer tUpdateTimer{};
	if (!tUpdateTimer.Run(1.0f))
		return;

	m_mIPriorities.clear();			m_mUPriorities.clear();
	m_mIFollowPriorities.clear();	m_mUFollowPriorities.clear();
	m_mIFriends.clear();			m_mUFriends.clear();
	m_mIParty.clear();				m_mUParty.clear();
	m_mIF2P.clear();				m_mUF2P.clear();
	m_mILevels.clear();				m_mULevels.clear();

	std::unordered_map<uint32_t, uint64_t> mParties;
	std::unordered_map<uint32_t, bool> mF2P;
	std::unordered_map<uint32_t, int> mLevels;

	if (auto pLobby = I::TFGCClientSystem->GetLobby())
	{
		auto pGameRules = I::TFGameRules();
		auto pMatchDesc = pGameRules ? pGameRules->GetMatchGroupDescription() : nullptr;

		int iMembers = pLobby->GetNumMembers();
		for (int i = 0; i < iMembers; i++)
		{
			CSteamID tSteamID;
			if (pLobby->GetMember(&tSteamID, i))
			{
				uint32_t uAccountID = tSteamID.GetAccountID();
				ConstTFLobbyPlayer pDetails;
				if (pLobby->GetMemberDetails(&pDetails, i))
				{
					auto pProto = pDetails.Proto();
					mF2P[uAccountID] = pProto->chat_suspension;
					mParties[uAccountID] = pProto->original_party_id;
					if (pMatchDesc && pMatchDesc->m_pProgressionDesc)
						mLevels[uAccountID] = std::max((int)pProto->rank, pMatchDesc->GetLevelForSteamID(&tSteamID));
					else
						mLevels[uAccountID] = pProto->rank;
				}
			}
		}
	}
	if (auto pParty = I::TFGCClientSystem->GetParty())
	{
		int iMembers = pParty->GetNumMembers();
		for (int i = 0; i < iMembers; i++)
		{
			CSteamID tSteamID;
			if (pParty->GetMember(&tSteamID, i))
				mParties[tSteamID.GetAccountID()] = 1;
		}
	}

	std::map<uint64_t, std::vector<uint32_t>> mPartiesGrouped;
	for (auto& [uAccountID, uPartyID] : mParties)
	{
		if (uPartyID)
			mPartiesGrouped[uPartyID].push_back(uAccountID);
	}
	mParties.clear();
	uint64_t uPartyCount = 0;
	for (auto& [uPartyID, vAccountIds] : mPartiesGrouped)
	{
		if (vAccountIds.size() <= 1) continue;

		int iPartyIndex = (uPartyID == 1) ? 1 : (++uPartyCount + 1);
		for (auto uAccountID : vAccountIds)
			mParties[uAccountID] = iPartyIndex;
	}
	m_iPartyCount = uPartyCount;

	auto pResource = GetResource();
	if (!pResource)
		return;

	int nMaxClients = I::EngineClient->GetMaxClients();
	for (int n = 1; n <= nMaxClients; n++)
	{
		if (!pResource->m_bValid(n)) continue;

		uint32_t uAccountID = pResource->m_iAccountID(n);
		bool bLocal = (n == nLocalIndex);
		if (bLocal) m_uAccountID = uAccountID;

		m_mIPriorities[n] = m_mUPriorities[uAccountID] = !bLocal ? F::PlayerUtils.GetPriority(uAccountID, false) : 0;
		m_mIFollowPriorities[n] = m_mUFollowPriorities[uAccountID] = !bLocal ? F::PlayerUtils.GetFollowPriority(uAccountID, false) : 0;
		m_mIFriends[n] = m_mUFriends[uAccountID] = !pResource->IsFakePlayer(n) && I::SteamFriends->HasFriend({ uAccountID, 1, k_EUniversePublic, k_EAccountTypeIndividual }, k_EFriendFlagImmediate);
		m_mIParty[n] = m_mUParty[uAccountID] = mParties.count(uAccountID) ? mParties[uAccountID] : 0;
		m_mIF2P[n] = m_mUF2P[uAccountID] = mF2P.count(uAccountID) ? mF2P[uAccountID] : false;
		m_mILevels[n] = m_mULevels[uAccountID] = mLevels.count(uAccountID) ? mLevels[uAccountID] : -2;
	}
}

void CEntities::UpdatePlayerAnimations(int nLocalIndex)
{
	F::Resolver.FrameStageNotify();

	const auto& vPlayers = m_mGroups[EntityEnum::PlayerAll];
	if (vPlayers.empty()) return;

	bool bDisableinterpolation = Vars::Visuals::Removals::Interpolation.Value;
	bool bPlayingDemo = I::EngineClient->IsPlayingDemo();

	for (auto pEntity : vPlayers)
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		if (!pPlayer->IsAlive()) continue;
		if (pPlayer->entindex() == nLocalIndex && !bPlayingDemo) // local player managed in CreateMove
			continue;

		bool bResolved = F::Resolver.GetAngles(pPlayer);
		if (!bDisableinterpolation && !bResolved)
			continue;

		int iDeltaTicks = TIME_TO_TICKS(GetDeltaTime(pPlayer->entindex()));
		if (iDeltaTicks <= 0) continue;

		float flOldFrameTime = I::GlobalVars->frametime;
		I::GlobalVars->frametime = I::Prediction->m_bEnginePaused ? 0.f : TICK_INTERVAL;

		G::UpdatingAnims = true;
		for (int i = 0; i < iDeltaTicks; i++)
		{
			if (bResolved)
			{
				float flYaw, flPitch;
				F::Resolver.GetAngles(pPlayer, &flYaw, &flPitch, nullptr, i + 1 == iDeltaTicks);

				float flOriginalYaw = pPlayer->m_angEyeAnglesY();
				float flOriginalPitch = pPlayer->m_angEyeAnglesX();

				pPlayer->m_angEyeAnglesY() = flYaw, pPlayer->m_angEyeAnglesX() = flPitch;
				pPlayer->UpdateClientSideAnimation();
				pPlayer->m_angEyeAnglesY() = flOriginalYaw, pPlayer->m_angEyeAnglesX() = flOriginalPitch;
			}
			else
				pPlayer->UpdateClientSideAnimation();
		}
		G::UpdatingAnims = false;
		I::GlobalVars->frametime = flOldFrameTime;
	}
}

void CEntities::Store()
{
	int nLocalIndex = I::EngineClient->GetLocalPlayer();
	auto pLocalEntity = I::ClientEntityList->GetClientEntity(nLocalIndex);
	if (!pLocalEntity)
		return;

	m_bIsSpectated = false;
	m_pLocal = pLocalEntity->As<CTFPlayer>();
	m_pLocalWeapon = m_pLocal->m_hActiveWeapon()->As<CTFWeaponBase>();
	int iLocalTeam = m_pLocal->m_iTeamNum();

	int iLag;
	{
		static int iStaticTickcout = I::GlobalVars->tickcount;
		iLag = I::GlobalVars->tickcount - iStaticTickcout - 1;
		iStaticTickcout = I::GlobalVars->tickcount;
	}

	int nMaxClients = I::EngineClient->GetMaxClients();
	int nHighestEntity = I::ClientEntityList->GetHighestEntityIndex();

	for (int n = 1; n <= nHighestEntity; n++)
	{
		auto pEntity = I::ClientEntityList->GetClientEntity(n)->As<CBaseEntity>();
		if (!pEntity)
			continue;

		auto nClassID = pEntity->GetClassID();
		if (n <= nMaxClients)
		{
			if (nClassID == ETFClassID::CTFPlayer)
			{	
				auto pPlayer = pEntity->As<CTFPlayer>();

				m_mGroups[EntityEnum::PlayerAll].push_back(pPlayer);
				m_mGroups[pPlayer->m_iTeamNum() != iLocalTeam ? EntityEnum::PlayerEnemy : EntityEnum::PlayerTeam].push_back(pPlayer);

				if (n != nLocalIndex)
				{
					if (!UpdatePlayerDetails(n, pPlayer, iLag))
						continue;

					// Check if this player is spectating the local player 
					if (!m_bIsSpectated && !pPlayer->IsAlive() &&
						pPlayer->m_iObserverMode() == OBS_MODE_FIRSTPERSON &&
						pPlayer->m_hObserverTarget().GetEntryIndex() == nLocalIndex)
						m_bIsSpectated = true;
				}
				m_mModels[n] = FNV1A::Hash32(I::ModelInfoClient->GetModelName(pEntity->GetModel()));
			}
		}
		else if (!ManageDormancy(n, pEntity))
		{
			switch (nClassID)
			{
			case ETFClassID::CTFPlayerResource:
				m_pPlayerResource = pEntity->As<CTFPlayerResource>();
				break;
			case ETFClassID::CTFObjectiveResource:
				m_pObjectiveResource = pEntity->As<CBaseTeamObjectiveResource>();
				break;
			case ETFClassID::CObjectSentrygun:
			case ETFClassID::CObjectDispenser:
			case ETFClassID::CObjectTeleporter:
				m_mModels[n] = FNV1A::Hash32(I::ModelInfoClient->GetModelName(pEntity->GetModel()));
				m_mGroups[EntityEnum::BuildingAll].push_back(pEntity);
				m_mGroups[pEntity->m_iTeamNum() != iLocalTeam ? EntityEnum::BuildingEnemy : EntityEnum::BuildingTeam].push_back(pEntity);
				break;
			case ETFClassID::CBaseProjectile:
			case ETFClassID::CBaseGrenade:
			case ETFClassID::CTFWeaponBaseGrenadeProj:
			case ETFClassID::CTFWeaponBaseMerasmusGrenade:
			case ETFClassID::CTFGrenadePipebombProjectile:
			case ETFClassID::CTFStunBall:
			case ETFClassID::CTFBall_Ornament:
			case ETFClassID::CTFProjectile_Jar:
			case ETFClassID::CTFProjectile_Cleaver:
			case ETFClassID::CTFProjectile_JarGas:
			case ETFClassID::CTFProjectile_JarMilk:
			case ETFClassID::CTFProjectile_SpellBats:
			case ETFClassID::CTFProjectile_SpellKartBats:
			case ETFClassID::CTFProjectile_SpellMeteorShower:
			case ETFClassID::CTFProjectile_SpellMirv:
			case ETFClassID::CTFProjectile_SpellPumpkin:
			case ETFClassID::CTFProjectile_SpellSpawnBoss:
			case ETFClassID::CTFProjectile_SpellSpawnHorde:
			case ETFClassID::CTFProjectile_SpellSpawnZombie:
			case ETFClassID::CTFProjectile_SpellTransposeTeleport:
			case ETFClassID::CTFProjectile_Throwable:
			case ETFClassID::CTFProjectile_ThrowableBreadMonster:
			case ETFClassID::CTFProjectile_ThrowableBrick:
			case ETFClassID::CTFProjectile_ThrowableRepel:
			case ETFClassID::CTFBaseRocket:
			case ETFClassID::CTFFlameRocket:
			case ETFClassID::CTFProjectile_Arrow:
			case ETFClassID::CTFProjectile_GrapplingHook:
			case ETFClassID::CTFProjectile_HealingBolt:
			case ETFClassID::CTFProjectile_Rocket:
			case ETFClassID::CTFProjectile_BallOfFire:
			case ETFClassID::CTFProjectile_MechanicalArmOrb:
			case ETFClassID::CTFProjectile_SentryRocket:
			case ETFClassID::CTFProjectile_SpellFireball:
			case ETFClassID::CTFProjectile_SpellLightningOrb:
			case ETFClassID::CTFProjectile_SpellKartOrb:
			case ETFClassID::CTFProjectile_EnergyBall:
			case ETFClassID::CTFProjectile_Flare:
			case ETFClassID::CTFBaseProjectile:
			case ETFClassID::CTFProjectile_EnergyRing:
			{
				if ((nClassID == ETFClassID::CTFProjectile_Cleaver || nClassID == ETFClassID::CTFStunBall) &&
					pEntity->As<CTFGrenadePipebombProjectile>()->m_bTouched())
					break;

				if ((nClassID == ETFClassID::CTFProjectile_Arrow || nClassID == ETFClassID::CTFProjectile_GrapplingHook) &&
					!pEntity->m_MoveType())
					break;

				m_mGroups[EntityEnum::WorldProjectile].push_back(pEntity);

				if (nClassID == ETFClassID::CTFGrenadePipebombProjectile)
				{
					auto pPipebomb = pEntity->As<CTFGrenadePipebombProjectile>();
					if (pPipebomb->m_hThrower().GetEntryIndex() == nLocalIndex && pPipebomb->m_iType() == TF_GL_MODE_REMOTE_DETONATE)
						m_mGroups[EntityEnum::LocalStickies].push_back(pEntity);
				}
				else if (nClassID == ETFClassID::CTFProjectile_Flare && pEntity->m_hOwnerEntity().GetEntryIndex() == nLocalIndex)
				{
					auto pLauncher = pEntity->As<CTFProjectile_Flare>()->m_hLauncher()->As<CTFWeaponBase>();
					if (pLauncher && pLauncher->As<CTFFlareGun>()->GetFlareGunType() == FLAREGUN_DETONATE)
						m_mGroups[EntityEnum::LocalFlares].push_back(pEntity);
				}
				break;
			}
			case ETFClassID::CCaptureFlag:
			case ETFClassID::CCaptureZone:
			case ETFClassID::CObjectCartDispenser:
			case ETFClassID::CTeamControlPoint:
			case ETFClassID::CFuncTrackTrain:
				m_mGroups[EntityEnum::WorldObjective].push_back(pEntity);
				break;
			case ETFClassID::CTFBaseBoss:
			case ETFClassID::CTFTankBoss:
			case ETFClassID::CMerasmus:
			case ETFClassID::CEyeballBoss:
			case ETFClassID::CHeadlessHatman:
			case ETFClassID::CZombie:
				m_mGroups[EntityEnum::WorldNPC].push_back(pEntity);
				break;
			case ETFClassID::CTFGenericBomb:
			case ETFClassID::CTFPumpkinBomb:
				m_mGroups[EntityEnum::WorldBomb].push_back(pEntity);
				break;
			case ETFClassID::CBaseAnimating:
				m_mModels[n] = FNV1A::Hash32(I::ModelInfoClient->GetModelName(pEntity->GetModel()));
				break;
			case ETFClassID::CTFAmmoPack:
				m_mGroups[EntityEnum::PickupAmmo].push_back(pEntity);
				break;
			case ETFClassID::CSniperDot:
				m_mGroups[EntityEnum::SniperDots].push_back(pEntity);
				break;
			}
		}
		else if (nClassID == ETFClassID::CObjectSentrygun ||
			nClassID == ETFClassID::CObjectDispenser)
		{
			m_mGroups[EntityEnum::BuildingAll].push_back(pEntity);
			m_mGroups[pEntity->m_iTeamNum() != iLocalTeam ? EntityEnum::BuildingEnemy : EntityEnum::BuildingTeam].push_back(pEntity);
		}
	}

	UpdatePartyAndLobbyInfo(nLocalIndex);
	UpdatePlayerAnimations(nLocalIndex);
}

void CEntities::Clear(bool bShutdown)
{
	m_pLocal = nullptr;
	m_pLocalWeapon = nullptr;
	m_pPlayerResource = nullptr;
	m_pObjectiveResource = nullptr;

	if (bShutdown)
	{
		m_mGroups.clear();
		m_mDeltaTimes.clear();
		m_mLagTimes.clear();
		m_mChokes.clear();
		m_mSetTicks.clear();
		m_mOldAngles.clear();
		m_mEyeAngles.clear();
		m_mLagCompensation.clear();
		m_mDormancy.clear();
		m_mAvgVelocities.clear();
		m_mModels.clear();
		m_mOrigins.clear();

		m_mIPriorities.clear();			m_mUPriorities.clear();
		m_mIFollowPriorities.clear();	m_mUFollowPriorities.clear();
		m_mIFriends.clear();			m_mUFriends.clear();
		m_mIParty.clear();				m_mUParty.clear();
		m_mIF2P.clear();				m_mUF2P.clear();
		m_mILevels.clear();				m_mULevels.clear();
	}
	else
	{
		// Keep groups, we are going to update them anyway
		for (auto& [_, vEnts] : m_mGroups)
			vEnts.clear();
	}
}

void CEntities::ManualNetwork(const StartSoundParams_t& params)
{
	if (params.soundsource <= 0 || !params.origin || params.soundsource == I::EngineClient->GetLocalPlayer())
		return;

	auto pEntity = I::ClientEntityList->GetClientEntity(params.soundsource)->As<CBaseEntity>();
	if (!pEntity || !pEntity->IsDormant())
		return;

	float flDuration = 0.f;
	switch (pEntity->GetClassID())
	{
	case ETFClassID::CTFPlayer: flDuration = 1.f; break;
	case ETFClassID::CObjectSentrygun:
	case ETFClassID::CObjectDispenser:
	case ETFClassID::CObjectTeleporter: flDuration = 5.f; break;
	}
	if (flDuration)
		m_mDormancy[params.soundsource] = { params.origin, I::GlobalVars->curtime + flDuration };
}

bool CEntities::ManageDormancy(int nIndex, CBaseEntity* pEntity)
{
	bool bDormant = pEntity->IsDormant();

	float flDuration = 0.f;
	const auto nClassID = pEntity->GetClassID();
	switch (nClassID)
	{
	case ETFClassID::CTFPlayer: flDuration = 1.f; break;
	case ETFClassID::CObjectSentrygun:
	case ETFClassID::CObjectDispenser:
	case ETFClassID::CObjectTeleporter: flDuration = 5.f; break;
	}
	if (flDuration)
	{
		if (bDormant)
		{
			if (nClassID == ETFClassID::CTFPlayer)
			{
				if (auto pResource = GetResource())
				{
					pEntity->As<CTFPlayer>()->m_lifeState() = pResource->m_bAlive(nIndex) ? LIFE_ALIVE : LIFE_DEAD;
					pEntity->As<CTFPlayer>()->m_iHealth() = pResource->m_iHealth(nIndex);
				}
			}
			if (m_mDormancy.contains(nIndex))
			{
				auto& tDormancy = m_mDormancy[nIndex];
				if (tDormancy.LastUpdate - I::GlobalVars->curtime > 0.f || flDuration == 5.f)
					pEntity->SetAbsOrigin(pEntity->m_vecOrigin() = tDormancy.Location);
				else
					m_mDormancy.erase(nIndex);
			}
		}
		else if (nClassID != ETFClassID::CTFPlayer || pEntity->As<CTFPlayer>()->IsAlive())
			m_mDormancy[nIndex] = { pEntity->m_vecOrigin(), I::GlobalVars->curtime + flDuration };
	}

	return bDormant;
}

bool CEntities::IsHealth(uint32_t uHash)
{
	switch (uHash)
	{
	case FNV1A::Hash32Const("models/items/banana/plate_banana.mdl"):
	case FNV1A::Hash32Const("models/items/medkit_small.mdl"):
	case FNV1A::Hash32Const("models/items/medkit_medium.mdl"):
	case FNV1A::Hash32Const("models/items/medkit_large.mdl"):
	case FNV1A::Hash32Const("models/items/medkit_small_bday.mdl"):
	case FNV1A::Hash32Const("models/items/medkit_medium_bday.mdl"):
	case FNV1A::Hash32Const("models/items/medkit_large_bday.mdl"):
	case FNV1A::Hash32Const("models/items/plate.mdl"):
	case FNV1A::Hash32Const("models/items/plate_sandwich_xmas.mdl"):
	case FNV1A::Hash32Const("models/items/plate_robo_sandwich.mdl"):
	case FNV1A::Hash32Const("models/props_medieval/medieval_meat.mdl"):
	case FNV1A::Hash32Const("models/workshopweapons/c_models/c_chocolate/plate_chocolate.mdl"):
	case FNV1A::Hash32Const("models/workshopweapons/c_models/c_fishcake/plate_fishcake.mdl"):
	case FNV1A::Hash32Const("models/props_halloween/halloween_medkit_small.mdl"):
	case FNV1A::Hash32Const("models/props_halloween/halloween_medkit_medium.mdl"):
	case FNV1A::Hash32Const("models/props_halloween/halloween_medkit_large.mdl"):
	case FNV1A::Hash32Const("models/items/ld1/mushroom_large.mdl"):
	case FNV1A::Hash32Const("models/items/plate_steak.mdl"):
	case FNV1A::Hash32Const("models/props_brine/foodcan.mdl"):
		return true;
	}
	return false;
}

bool CEntities::IsAmmo(uint32_t uHash)
{
	switch (uHash)
	{
	case FNV1A::Hash32Const("models/items/ammopack_small.mdl"):
	case FNV1A::Hash32Const("models/items/ammopack_medium.mdl"):
	case FNV1A::Hash32Const("models/items/ammopack_large.mdl"):
	case FNV1A::Hash32Const("models/items/ammopack_large_bday.mdl"):
	case FNV1A::Hash32Const("models/items/ammopack_medium_bday.mdl"):
	case FNV1A::Hash32Const("models/items/ammopack_small_bday.mdl"):
		return true;
	}
	return false;
}

bool CEntities::IsPowerup(uint32_t uHash)
{
	switch (uHash)
	{
	case FNV1A::Hash32Const("models/pickups/pickup_powerup_agility.mdl"):
	case FNV1A::Hash32Const("models/pickups/pickup_powerup_crit.mdl"):
	case FNV1A::Hash32Const("models/pickups/pickup_powerup_defense.mdl"):
	case FNV1A::Hash32Const("models/pickups/pickup_powerup_haste.mdl"):
	case FNV1A::Hash32Const("models/pickups/pickup_powerup_king.mdl"):
	case FNV1A::Hash32Const("models/pickups/pickup_powerup_knockout.mdl"):
	case FNV1A::Hash32Const("models/pickups/pickup_powerup_plague.mdl"):
	case FNV1A::Hash32Const("models/pickups/pickup_powerup_precision.mdl"):
	case FNV1A::Hash32Const("models/pickups/pickup_powerup_reflect.mdl"):
	case FNV1A::Hash32Const("models/pickups/pickup_powerup_regen.mdl"):
	//case FNV1A::Hash32Const("models/pickups/pickup_powerup_resistance.mdl"):
	case FNV1A::Hash32Const("models/pickups/pickup_powerup_strength.mdl"):
	//case FNV1A::Hash32Const("models/pickups/pickup_powerup_strength_arm.mdl"):
	case FNV1A::Hash32Const("models/pickups/pickup_powerup_supernova.mdl"):
	//case FNV1A::Hash32Const("models/pickups/pickup_powerup_thorns.mdl"):
	//case FNV1A::Hash32Const("models/pickups/pickup_powerup_uber.mdl"):
	case FNV1A::Hash32Const("models/pickups/pickup_powerup_vampire.mdl"):
		return true;
	}
	return false;
}

bool CEntities::IsSpellbook(uint32_t uHash)
{
	switch (uHash)
	{
	case FNV1A::Hash32Const("models/props_halloween/hwn_spellbook_flying.mdl"):
	case FNV1A::Hash32Const("models/props_halloween/hwn_spellbook_upright.mdl"):
	case FNV1A::Hash32Const("models/props_halloween/hwn_spellbook_upright_major.mdl"):
	case FNV1A::Hash32Const("models/items/crystal_ball_pickup.mdl"):
	case FNV1A::Hash32Const("models/items/crystal_ball_pickup_major.mdl"):
	case FNV1A::Hash32Const("models/props_monster_mash/flask_vial_green.mdl"):
	case FNV1A::Hash32Const("models/props_monster_mash/flask_vial_purple.mdl"): // prop_dynamic in the map, probably won't work
		return true;
	}
	return false;
}

CTFPlayer* CEntities::GetLocal() { return m_pLocal; }
CTFWeaponBase* CEntities::GetWeapon() { return m_pLocalWeapon; }
CTFPlayerResource* CEntities::GetResource() { return m_pPlayerResource; }
CBaseTeamObjectiveResource* CEntities::GetObjectiveResource( ) { return m_pObjectiveResource; }

const std::vector<CBaseEntity*>& CEntities::GetGroup(const EntityEnum::EntityEnum iGroup) { return m_mGroups[iGroup]; }

float CEntities::GetDeltaTime(int iIndex) { return m_mDeltaTimes.contains(iIndex) ? m_mDeltaTimes[iIndex] : TICK_INTERVAL; }
float CEntities::GetLagTime(int iIndex) { return m_mLagTimes.contains(iIndex) ? m_mLagTimes[iIndex] : TICK_INTERVAL; }
int CEntities::GetChoke(int iIndex) { return m_mChokes.contains(iIndex) ? m_mChokes[iIndex] : 0; }
bool CEntities::GetDormancy(int iIndex) { return m_mDormancy.contains(iIndex); }
Vec3 CEntities::GetEyeAngles(int iIndex) { return m_mEyeAngles.contains(iIndex) ? m_mEyeAngles[iIndex] : Vec3(); }
Vec3 CEntities::GetDeltaAngles(int iIndex) { return m_mOldAngles.contains(iIndex) ? m_mEyeAngles[iIndex].DeltaAngle(m_mOldAngles[iIndex]) / GetLagTime(iIndex) * (F::Backtrack.GetReal() + TICKS_TO_TIME(F::Backtrack.GetAnticipatedChoke())) : Vec3(); }
bool CEntities::GetLagCompensation(int iIndex) { return m_mLagCompensation[iIndex]; }
void CEntities::SetLagCompensation(int iIndex, bool bLagComp) { m_mLagCompensation[iIndex] = bLagComp; }
Vec3* CEntities::GetAvgVelocity(int iIndex) { return iIndex != I::EngineClient->GetLocalPlayer() ? &m_mAvgVelocities[iIndex] : nullptr; }
void CEntities::SetAvgVelocity(int iIndex, Vec3 vAvgVelocity) { m_mAvgVelocities[iIndex] = vAvgVelocity; }
uint32_t CEntities::GetModel(int iIndex) { return m_mModels[iIndex]; }
std::deque<VelFixRecord>* CEntities::GetOrigins(int iIndex) { return m_mOrigins.contains(iIndex) ? &m_mOrigins[iIndex] : nullptr; }

int CEntities::GetPriority(int iIndex) { return m_mIPriorities[iIndex]; }
int CEntities::GetPriority(uint32_t uAccountID) { return m_mUPriorities[uAccountID]; }
int CEntities::GetFollowPriority(int iIndex) { return m_mIFollowPriorities[iIndex]; }
int CEntities::GetFollowPriority(uint32_t uAccountID) { return m_mUFollowPriorities[uAccountID]; }
bool CEntities::IsFriend(int iIndex) { return m_mIFriends[iIndex]; }
bool CEntities::IsFriend(uint32_t uAccountID) { return m_mUFriends[uAccountID]; }
bool CEntities::InParty(int iIndex) { return iIndex != I::EngineClient->GetLocalPlayer() && m_mIParty[iIndex] == 1; }
bool CEntities::InParty(uint32_t uAccountID) { return uAccountID != m_uAccountID && m_mUParty[uAccountID] == 1; }
bool CEntities::IsF2P(int iIndex) { return m_mIF2P[iIndex]; }
bool CEntities::IsF2P(uint32_t uAccountID) { return m_mUF2P[uAccountID]; }
int CEntities::GetLevel(int iIndex) { return m_mILevels.contains(iIndex) ? m_mILevels[iIndex] : -2; }
int CEntities::GetLevel(uint32_t uAccountID) { return m_mULevels.contains(uAccountID) ? m_mULevels[uAccountID] : -2; }
int CEntities::GetParty(int iIndex) { return m_mIParty.contains(iIndex) ? m_mIParty[iIndex] : 0; }
int CEntities::GetParty(uint32_t uAccountID) { return m_mUParty.contains(uAccountID) ? m_mUParty[uAccountID] : 0; }
int CEntities::GetPartyCount() { return m_iPartyCount; }
uint32_t CEntities::GetLocalAccountID() { return m_uAccountID; }
bool CEntities::IsSpectated() { return m_bIsSpectated; }
