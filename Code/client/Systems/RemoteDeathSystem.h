#pragma once

#include <Games/References.h>
#include <AI/AIProcess.h>

namespace RemoteDeathSystem
{
inline bool CanKill(Actor* apActor) noexcept
{
    // SE 1.6.1170 KillImpl (37896) requires a high process and loaded body for its physical death path.
    // AIProcess::movementType is the existing name for the process-level byte at 0x137 (high == 0).
    return apActor && apActor->currentProcess && apActor->currentProcess->movementType == 0 &&
           apActor->currentProcess->middleProcess && apActor->GetNiNode() && apActor->animationGraphHolder.IsReady();
}

inline void Apply(Actor* apActor, bool aIsDead) noexcept
{
    if (!apActor || apActor->GetExtension()->IsPlayer() || apActor->IsDead() == aIsDead)
        return;

    if (!aIsDead)
    {
        spdlog::info("Respawning remote NPC {:X} because its owner has it alive", apActor->formID);
        apActor->Respawn();
    }
    else if (CanKill(apActor))
    {
        spdlog::info("Applying loaded remote NPC death {:X}", apActor->formID);
        apActor->Kill();
    }
}
} // namespace RemoteDeathSystem
