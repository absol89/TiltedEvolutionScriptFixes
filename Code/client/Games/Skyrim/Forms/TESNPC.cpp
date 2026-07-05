#include <TiltedOnlinePCH.h>

#include <Forms/TESNPC.h>

TP_THIS_FUNCTION(TSetLeveledNpc, TESNPC*, TESNPC, TESNPC*);
static TSetLeveledNpc* RealSetLeveledNpc = nullptr;

// The engine resolves a leveled spawn by creating a temporary TESNPC from
// (placed base, picked NPC); named leveled NPCs hide the pick from the temp
// NPC's template chain, so remember it here. CAUTION: temp form ids are
// recycled by the engine and cell attach resolves without this hook, so an
// entry may describe a previous occupant of its id - consumers must prefer
// the chain and treat this map as a last resort. The lock is needed since
// resolution can run on a loader thread while services read from the game
// thread.
static std::mutex s_leveledPicksLock;
static TiltedPhoques::Map<uint32_t, uint32_t> s_leveledPicks;

TESNPC* TP_MAKE_THISCALL(HookSetLeveledNpc, TESNPC, TESNPC* apSelectedNpc)
{
    TESNPC* pResult = TiltedPhoques::ThisCall(RealSetLeveledNpc, apThis, apSelectedNpc);

    spdlog::debug("Leveled resolution: placed base {:X} -> pick {:X}, temp base {:X}", apThis ? apThis->formID : 0, apSelectedNpc ? apSelectedNpc->formID : 0, pResult ? pResult->formID : 0);

    if (pResult && apSelectedNpc)
    {
        std::lock_guard lock(s_leveledPicksLock);
        s_leveledPicks[pResult->formID] = apSelectedNpc->formID;
    }

    return pResult;
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
