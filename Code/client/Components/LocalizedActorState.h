#pragma once

#include <cstdint>

// Transient bookkeeping for an actor that just handed off from remote to local (ownership
// transfer / (re)localization): the animation reconcile grace window and the deferred
// combat-cycle reconcile (stop -> start -> stop) that repairs an inert remote-lifecycle
// behavior/animation state. Stored as an entt component (NOT on ActorExtension, per review).
struct LocalizedActorState
{
    // Tick (World clock) of last (re)localize; drives the Animation.cpp reset-action
    // de-dupe grace window. 0 = no window active.
    uint64_t LocalizedTick = 0;

    // Issue #810: a freshly (re)localized NPC arrives with inert behavior/animation state
    // from its remote lifetime and cannot transition to move/draw on a threat. The original
    // bug was repaired only by a real combat -> StopCombat cycle (the actor then draws on its
    // own via vanilla when it next sees the player). A bare StopCombat() is a no-op on an
    // already-non-combat inert actor, so we replay the cycle: StartCombat(player) wakes the
    // behavior graph, then a deferred StopCombat() (kCombatCycleStopDelayTicks later) leaves
    // it clean. The following fields drive that deferred second half.
    bool CombatCycleStarted = false;      // StartCombat issued at localization
    uint64_t CombatCycleStopTick = 0;     // World tick at which to issue the deferred StopCombat()

    // Grace window (World ticks) before issuing the deferred StopCombat (a few frames so the
    // combat-enter transition actually registers before we exit it).
    static constexpr uint64_t kCombatCycleStopDelayTicks = 5;

    // Reset-action broadcast dedupe flags for the grace window.
    bool BroadcastedUnequip = false;
    bool BroadcastedCombatStanceStop = false;

    // Grace window (World ticks) for reset-action broadcast de-dupe (~1s @ 60Hz).
    static constexpr uint64_t kAnimReconcileGraceTicks = 60;
};
