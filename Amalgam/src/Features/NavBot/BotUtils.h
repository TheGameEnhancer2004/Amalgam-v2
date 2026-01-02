#pragma once
#include "../../SDK/SDK.h"
#include "../../Utils/Timer/Timer.h"

// Struct to hold data about the nearest threat
struct ClosestEnemy_t
{
    int m_iEntIdx = -1;
    CTFPlayer* m_pPlayer = nullptr;
    float m_flDist = FLT_MAX;

    // Helper to quickly check if a valid enemy was found
    bool IsValid() const { return m_pPlayer != nullptr && m_iEntIdx != -1; }
};

// Enum for the ShouldTarget logic
Enum(ShouldTarget, Invalid = -1, DontTarget, Target);

class CBotUtils
{
public:
    // --- Public State ---
    int m_iCurrentSlot = -1;
    int m_iBestSlot = -1;
    ClosestEnemy_t m_tClosestEnemy = {};
    Vec3 m_vLastAngles = {};

public:
    // --- Main Interface ---
    void Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
    void Reset();

    // --- Targeting Logic ---
    ShouldTargetEnum::ShouldTargetEnum ShouldTarget(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, int iEntIdx);
    ShouldTargetEnum::ShouldTargetEnum ShouldTargetBuilding(CTFPlayer* pLocal, int iEntIdx);
    bool ShouldAssist(CTFPlayer* pLocal, int iEntIdx);

    // --- Movement & Aiming ---
    void LookLegit(CTFPlayer* pLocal, CUserCmd* pCmd, const Vec3& vDest, bool bSilent);
    
    // Path finding look helpers
    void LookAtPath(CUserCmd* pCmd, Vec2 vDest, Vec3 vLocalEyePos, bool bSilent);
    void LookAtPath(CUserCmd* pCmd, Vec3 vWishAngles, Vec3 vLocalEyePos, bool bSilent, bool bSmooth = true);
    
    // Smoothing helper
    void DoSlowAim(Vec3& vWishAngles, float flSpeed, Vec3 vPreviousAngles);

    // --- Utilities ---
    void SetSlot(CTFPlayer* pLocal, int iSlot);
    bool GetDormantOrigin(int iIndex, Vector& vOut);
    void InvalidateLLAP();

private:
    // --- Internal Logic ---
    ClosestEnemy_t UpdateCloseEnemies(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);
    void UpdateBestSlot(CTFPlayer* pLocal);
    void AutoScope(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
    bool HasMedigunTargets(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);

    // --- Caching ---
    std::unordered_map<int, bool> m_mAutoScopeCache;
    std::vector<ClosestEnemy_t> m_vCloseEnemies;

    // --- Legit Look State ---
    // Simplified struct to match the improved, cleaner logic in .cpp
    struct LegitLookState_t
    {
        bool m_bInitialized = false;
        Vec3 m_vAnchor = {}; // The smoothed "center" point of vision
        
        // Note: Previous complex timer/phase variables removed 
        // as they are replaced by cleaner realtime math in the cpp.
    };
    LegitLookState_t m_tLLAP = {};
};

ADD_FEATURE(CBotUtils, BotUtils);
