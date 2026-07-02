#include <TiltedOnlinePCH.h>

#include <Games/Skyrim/ObScript.h>
#include <Games/References.h>

#include <Events/ScriptAnimationEvent.h>

#include <World.h>

// Count from CommonLibSSE-NG SCRIPT_FUNCTION::Commands: kConsoleCommandsEnd (0x01B4) - kConsoleOpBase (0x0100).
static constexpr uint32_t kConsoleCommandCount = 0xB4;

using TParseParameters = bool(const ObScriptParam* apParamInfo, ObScriptData* apScriptData, uint32_t& arOpcodeOffsetPtr, TESObjectREFR* apThisObj, TESObjectREFR* apContainingObj, Script* apScriptObj, ScriptLocals* apLocals, ...);

static ObScriptCommand::TExecute* RealSendAnimationEventExecute = nullptr;
static ObScriptCommand* s_pSaeCommand = nullptr; // kept for a future unhook/restore path

static bool HookSendAnimationEventExecute(const ObScriptParam* apParamInfo, ObScriptData* apScriptData, TESObjectREFR* apThisObj, TESObjectREFR* apContainingObj, Script* apScriptObj, ScriptLocals* apLocals, double& arResult, uint32_t& arOpcodeOffsetPtr)
{
    POINTER_SKYRIMSE(TParseParameters, s_parseParameters, 21910);

    // Parse a copy so the original execute below still sees the unconsumed offset.
    uint32_t opcodeOffset = arOpcodeOffsetPtr;
    char eventNameBuffer[512]{};

    const bool cParsed = s_parseParameters.Get()(apParamInfo, apScriptData, opcodeOffset, apThisObj, apContainingObj, apScriptObj, apLocals, eventNameBuffer);

    const bool cResult = RealSendAnimationEventExecute(apParamInfo, apScriptData, apThisObj, apContainingObj, apScriptObj, apLocals, arResult, arOpcodeOffsetPtr);

    if (cResult && cParsed && apThisObj && eventNameBuffer[0] != '\0')
    {
        spdlog::debug("Console sae captured: ref {:X}, event {}", apThisObj->formID, eventNameBuffer);
        World::Get().GetRunner().Trigger(ScriptAnimationEvent(apThisObj->formID, String{}, eventNameBuffer));
    }

    return cResult;
}

static TiltedPhoques::Initializer s_obScriptHooks(
    []()
    {
        // First console command, AE address-library id 365650 (CommonLibSSE-NG include/RE/Offsets.h:480).
        POINTER_SKYRIMSE(ObScriptCommand, s_firstConsoleCommand, 365650);

        ObScriptCommand* pCommands = s_firstConsoleCommand.Get();
        if (!pCommands)
        {
            spdlog::error("ObScript: console command table not found, sae sync disabled");
            return;
        }

        for (uint32_t i = 0; i < kConsoleCommandCount; ++i)
        {
            ObScriptCommand& command = pCommands[i];

            if (command.pFunctionName && !strcmp(command.pFunctionName, "SendAnimationEvent"))
            {
                s_pSaeCommand = &command;
                RealSendAnimationEventExecute = command.pExecuteFunction;
                command.pExecuteFunction = HookSendAnimationEventExecute;

                spdlog::info("ObScript: hooked console SendAnimationEvent (sae)");
                return;
            }
        }

        spdlog::error("ObScript: SendAnimationEvent console command not found, sae sync disabled");
    });
