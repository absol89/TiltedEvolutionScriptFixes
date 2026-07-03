#include <TiltedOnlinePCH.h>

#include <Forms/TESNPC.h>

TP_THIS_FUNCTION(TSetLeveledNpc, TESNPC*, TESNPC, TESNPC*);
static TSetLeveledNpc* RealSetLeveledNpc = nullptr;

// Temporary investigation probe, removed in the apply task.
static void DumpNpc(const char* apLabel, TESNPC* apNpc)
{
    if (!apNpc)
    {
        spdlog::info("[LvlProbe] {}: null", apLabel);
        return;
    }

    const char* pName = "<not an npc>";
    if (apNpc->formType == FormType::Npc && apNpc->fullName.value)
        pName = apNpc->fullName.value;

    spdlog::info("[LvlProbe] {}: formID={:X}, temp={}, formType={}, name='{}'", apLabel, apNpc->formID, apNpc->IsTemporary(), static_cast<uint8_t>(apNpc->formType), pName);
}

static void DumpTemplateChain(TESNPC* apNpc)
{
    if (!apNpc)
        return;

    TESNPC* pCurrent = apNpc->npcTemplate;
    int depth = 0;

    while (pCurrent && depth < 10)
    {
        DumpNpc("chain", pCurrent);

        // A chain entry can be a TESLevCharacter posing as TESNPC*; its layout
        // is too small to hold npcTemplate, so stop before reading past it.
        if (pCurrent->formType != FormType::Npc)
            break;

        pCurrent = pCurrent->npcTemplate;
        ++depth;
    }
}

TESNPC* TP_MAKE_THISCALL(HookSetLeveledNpc, TESNPC, TESNPC* apSelectedNpc)
{
    DumpNpc("this", apThis);
    DumpNpc("selected", apSelectedNpc);

    TESNPC* pResult = TiltedPhoques::ThisCall(RealSetLeveledNpc, apThis, apSelectedNpc);

    DumpNpc("result", pResult);
    spdlog::info("[LvlProbe] result == this: {}", pResult == apThis);
    DumpTemplateChain(pResult);

    return pResult;
}

static TiltedPhoques::Initializer s_npcInitHooks(
    []()
    {
        POINTER_SKYRIMSE(TSetLeveledNpc, s_SetLeveledNpc, 14375);

        RealSetLeveledNpc = s_SetLeveledNpc.Get();

        TP_HOOK(&RealSetLeveledNpc, HookSetLeveledNpc);
    });
