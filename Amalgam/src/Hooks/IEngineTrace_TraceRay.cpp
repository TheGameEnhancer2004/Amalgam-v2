#include "../SDK/SDK.h"

#include <optional>

MAKE_SIGNATURE(CM_BoxTrace, "engine.dll", "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 41 56 48 83 EC ? 45 0F B6 F1", 0x0);
MAKE_SIGNATURE(CM_ClipBoxToBrush_True, "engine.dll", "48 89 5C 24 ? 57 48 81 EC ? ? ? ? 44 0F B7 42", 0x0);
MAKE_SIGNATURE(CM_ClipBoxToBrush_False, "engine.dll", "48 8B C4 48 89 58 ? 57 48 81 EC ? ? ? ? 44 0F B7 42", 0x0);
MAKE_SIGNATURE(CEngineTrace_ClipTraceToTrace, "engine.dll", "48 89 5C 24 ? 57 48 83 EC ? 80 7A ? ? 49 8B D8", 0x0);

#define MAX_CHECK_COUNT_DEPTH 2
typedef uint32 TraceCounter_t;
typedef CUtlVector<TraceCounter_t> CTraceCounterVec;

struct TraceInfo_t
{
	Vector m_start;
	Vector m_end;
	Vector m_mins;
	Vector m_maxs;
	Vector m_extents;
	Vector m_delta;
	Vector m_invDelta;
	trace_t m_trace;
	trace_t m_stabTrace;
	int m_contents;
	bool m_ispoint;
	bool m_isswept;
	void* m_pBSPData;
	Vector m_DispStabDir;
	int m_bDispHit;
	bool m_bCheckPrimary;
	int m_nCheckDepth;
	TraceCounter_t m_Count[MAX_CHECK_COUNT_DEPTH];
	CTraceCounterVec m_BrushCounters[MAX_CHECK_COUNT_DEPTH];
	CTraceCounterVec m_DispCounters[MAX_CHECK_COUNT_DEPTH];
};

static bool s_bNoSkip = false;
static std::optional<bool> s_bStartSolid = std::nullopt;

MAKE_HOOK(IEngineTrace_TraceRay, U::Memory.GetVirtual(I::EngineTrace, 4), void,
	void* rcx, const Ray_t& ray, unsigned int fMask, ITraceFilter* pTraceFilter, trace_t* pTrace)
{
	DEBUG_RETURN(IEngineTrace_TraceRay, rcx, ray, fMask, pTraceFilter, pTrace);

	s_bNoSkip = fMask & CONTENTS_NOSKIP;

	CALL_ORIGINAL(rcx, ray, fMask, pTraceFilter, pTrace);
#ifdef DEBUG_TRACES
	if (Vars::Debug::VisualizeTraces.Value)
	{
		Vec3 vStart = ray.m_Start + ray.m_StartOffset;
		Vec3 vEnd = Vars::Debug::VisualizeTraceHits.Value ? pTrace->endpos : ray.m_Delta + vStart;
		G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(vStart, vEnd), I::GlobalVars->curtime + 0.015f, Color_t(0, 0, 255), bool(GetAsyncKeyState(VK_MENU) & 0x8000));
		if (!ray.m_IsRay)
		{
			G::BoxStorage.emplace_back(ray.m_Start, ray.m_Extents * -1, ray.m_Extents, Vec3(), I::GlobalVars->curtime + 0.015f, Color_t(0, 0, 255), Color_t(0, 0, 0, 0), bool(GetAsyncKeyState(VK_MENU) & 0x8000));
			G::BoxStorage.emplace_back(vEnd - ray.m_StartOffset, ray.m_Extents * -1, ray.m_Extents, Vec3(), I::GlobalVars->curtime + 0.015f, Color_t(0, 0, 255), Color_t(0, 0, 0, 0), bool(GetAsyncKeyState(VK_MENU) & 0x8000));
		}
	}
#endif

	if (s_bStartSolid)
		pTrace->startsolid = s_bStartSolid.value(), s_bStartSolid = std::nullopt;

	s_bNoSkip = false;
}

MAKE_HOOK(CM_BoxTrace, S::CM_BoxTrace(), void,
	const Ray_t& ray, int headnode, int brushmask, bool computeEndpt, trace_t& tr)
{
	CALL_ORIGINAL(ray, headnode, brushmask, computeEndpt, tr);

	if (s_bStartSolid)
		tr.startsolid = s_bStartSolid.value(), s_bStartSolid = std::nullopt;
	if (s_bNoSkip)
		s_bStartSolid = tr.startsolid, tr.startsolid = false;
}

MAKE_HOOK(CM_ClipBoxToBrush_True, S::CM_ClipBoxToBrush_True(), void,
	TraceInfo_t* pTraceInfo, void* brush)
{
	DEBUG_RETURN(CM_ClipBoxToBrush_True, pTraceInfo, brush);

	if (s_bStartSolid)
		pTraceInfo->m_trace.startsolid = s_bStartSolid.value(), s_bStartSolid = std::nullopt;

	CALL_ORIGINAL(pTraceInfo, brush);

	if (s_bNoSkip)
	{
		s_bStartSolid = pTraceInfo->m_trace.startsolid, pTraceInfo->m_trace.startsolid = false;
		if (!s_bStartSolid.value() && !pTraceInfo->m_trace.fractionleftsolid && pTraceInfo->m_trace.fraction != 1.f)
			pTraceInfo->m_trace.fractionleftsolid = pTraceInfo->m_trace.fraction - FLT_EPSILON;
	}
}

MAKE_HOOK(CM_ClipBoxToBrush_False, S::CM_ClipBoxToBrush_False(), void,
	TraceInfo_t* pTraceInfo, void* brush)
{
	DEBUG_RETURN(CM_ClipBoxToBrush_False, pTraceInfo, brush);

	if (s_bStartSolid)
		pTraceInfo->m_trace.startsolid = s_bStartSolid.value(), s_bStartSolid = std::nullopt;

	CALL_ORIGINAL(pTraceInfo, brush);

	if (s_bNoSkip)
	{
		s_bStartSolid = pTraceInfo->m_trace.startsolid, pTraceInfo->m_trace.startsolid = false;
		if (!s_bStartSolid.value() && !pTraceInfo->m_trace.fractionleftsolid && pTraceInfo->m_trace.fraction != 1.f)
			pTraceInfo->m_trace.fractionleftsolid = pTraceInfo->m_trace.fraction - FLT_EPSILON;
	}
}

MAKE_HOOK(CEngineTrace_ClipTraceToTrace, S::CEngineTrace_ClipTraceToTrace(), bool,
	void* rcx, trace_t& clipTrace, trace_t* pFinalTrace)
{
	DEBUG_RETURN(CEngineTrace_ClipTraceToTrace, rcx, clipTrace, pFinalTrace);

	if (s_bStartSolid)
		pFinalTrace->startsolid = s_bStartSolid.value(), s_bStartSolid = std::nullopt;
	if (s_bNoSkip && (!clipTrace.fraction || clipTrace.fraction < pFinalTrace->fractionleftsolid))
		return false;

	return CALL_ORIGINAL(rcx, clipTrace, pFinalTrace);
}