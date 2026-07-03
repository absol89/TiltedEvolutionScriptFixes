#include <TiltedOnlinePCH.h>

#include <Forms/TESNPC.h>

TP_THIS_FUNCTION(TSetLeveledNpc, TESNPC*, TESNPC, TESNPC*);
static TSetLeveledNpc* RealSetLeveledNpc = nullptr;

// The engine resolves a leveled spawn by creating a temporary TESNPC from
// (placed base, picked NPC); the pick is not reliably recoverable from the
// temp NPC afterwards, so remember it here. Temp form ids are recycled by
// the engine, which conveniently overwrites stale entries.
static TiltedPhoques::Map<uint32_t, uint32_t> s_leveledPicks;

// Substituted for the engine's random roll while a remote correction
// re-resolves an actor (set around DisableImpl/EnableImpl by the applier).
static thread_local TESNPC* s_pForcedLeveledPick = nullptr;

TESNPC* TP_MAKE_THISCALL(HookSetLeveledNpc, TESNPC, TESNPC* apSelectedNpc)
{
    TESNPC* pSelected = s_pForcedLeveledPick ? s_pForcedLeveledPick : apSelectedNpc;

    TESNPC* pResult = TiltedPhoques::ThisCall(RealSetLeveledNpc, apThis, pSelected);

    if (pResult && pSelected)
        s_leveledPicks[pResult->formID] = pSelected->formID;

    return pResult;
}

void TESNPC::SetForcedLeveledPick(TESNPC* apPick) noexcept
{
    s_pForcedLeveledPick = apPick;
}

uint32_t TESNPC::GetLeveledPickFormId(uint32_t aTempNpcFormId) noexcept
{
    const auto cIt = s_leveledPicks.find(aTempNpcFormId);
    return cIt != s_leveledPicks.end() ? cIt->second : 0;
}

static TiltedPhoques::Initializer s_npcInitHooks(
    []()
    {
        POINTER_SKYRIMSE(TSetLeveledNpc, s_SetLeveledNpc, 14375);

        RealSetLeveledNpc = s_SetLeveledNpc.Get();

        TP_HOOK(&RealSetLeveledNpc, HookSetLeveledNpc);
    });
