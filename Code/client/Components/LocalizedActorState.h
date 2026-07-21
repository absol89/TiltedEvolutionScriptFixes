#pragma once

#include <cstdint>

// Transient bookkeeping for an actor that just handed off from remote to local (ownership
// transfer / (re)localization): the animation reconcile grace window + detection latch.
// Stored as an entt component (NOT on ActorExtension, per review).
struct LocalizedActorState
{
    // Tick (World clock) of last (re)localize; drives the Animation.cpp reset-action
    // de-dupe grace window. 0 = no window active.
    uint64_t LocalizedTick = 0;

    // One-shot latch for the detection-hook engage (Issue #810): set true after the hook
    // fires StartCombat once, so it never re-kicks the same actor into a draw/sheathe loop.
    bool EngagedFromDetection = false;

    // Reset-action broadcast dedupe flags for the grace window.
    bool BroadcastedUnequip = false;
    bool BroadcastedCombatStanceStop = false;

    // Grace window (World ticks) for reset-action broadcast de-dupe (~1s @ 60Hz).
    static constexpr uint64_t kAnimReconcileGraceTicks = 60;
};
