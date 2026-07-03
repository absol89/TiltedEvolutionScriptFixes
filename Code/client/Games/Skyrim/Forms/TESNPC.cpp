#include <TiltedOnlinePCH.h>

#include <Forms/TESNPC.h>

TP_THIS_FUNCTION(TSetLeveledNpc, TESNPC*, TESNPC, TESNPC*);
static TSetLeveledNpc* RealSetLeveledNpc = nullptr;

// The engine resolves a leveled spawn by creating a temporary TESNPC from
// (placed base, picked NPC); the pick is not reliably recoverable from the
// temp NPC afterwards, so remember it here. Entries are 8 bytes and recycled
// temp form ids overwrite their stale predecessors; the lock is needed since
// resolution can run on a loader thread while services read from the game
// thread.
static std::mutex s_leveledPicksLock;
static TiltedPhoques::Map<uint32_t, uint32_t> s_leveledPicks;

// Substituted for the engine's random roll while a remote correction
// re-resolves an actor (set around DisableImpl/EnableImpl by the applier).
// thread_local scopes it to resolutions made synchronously inside that
// enable call; consumed one-shot so a batched neighbor resolution in the
// same window cannot inherit it.
static thread_local TESNPC* s_pForcedLeveledPick = nullptr;

TESNPC* TP_MAKE_THISCALL(HookSetLeveledNpc, TESNPC, TESNPC* apSelectedNpc)
{
    TESNPC* pSelected = apSelectedNpc;

    if (s_pForcedLeveledPick)
    {
        pSelected = s_pForcedLeveledPick;
        s_pForcedLeveledPick = nullptr;
    }

    TESNPC* pResult = TiltedPhoques::ThisCall(RealSetLeveledNpc, apThis, pSelected);

    if (pResult && pSelected)
    {
        std::lock_guard lock(s_leveledPicksLock);
        s_leveledPicks[pResult->formID] = pSelected->formID;
    }

    return pResult;
}

void TESNPC::SetForcedLeveledPick(TESNPC* apPick) noexcept
{
    s_pForcedLeveledPick = apPick;
}

uint32_t TESNPC::GetLeveledPickFormId(uint32_t aTempNpcFormId) noexcept
{
    std::lock_guard lock(s_leveledPicksLock);

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
