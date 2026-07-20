#pragma once

#include <cstdint>

// Transient bookkeeping for an actor that just handed off from remote to local (ownership
// transfer / (re)localization): the animation reconcile grace window. Stored as an entt
// component (NOT on ActorExtension, per review).
struct LocalizedActorState
{
    // Tick (World clock) of last (re)localize; drives the Animation.cpp reset-action
    // de-dupe grace window. 0 = no window active.
    uint64_t LocalizedTick = 0;

    // Issue #810: a freshly (re)localized NPC arrives with inert behavior/animation state
    // from its remote lifetime and cannot transition to move/draw on a threat. A bare
    // StopCombat() is a no-op on an already-non-combat inert actor, but a real combat ->
    // StopCombat cycle (observed in playtest) repairs it: the actor then draws on its own
    // via vanilla when it next sees the player. On localization we StartCombat(player) to
    // wake the behavior graph (see CharacterService). We intentionally do NOT StopCombat:
    // the single StartCombat(player) kick at localization (CharacterService) puts the actor
    // in combat with the present player -- which is what makes a handed-off peaceful bandit
    // draw and attack.

    // Reset-action broadcast dedupe flags for the grace window.
    bool BroadcastedUnequip = false;
    bool BroadcastedCombatStanceStop = false;

    // Grace window (World ticks) for reset-action broadcast de-dupe (~1s @ 60Hz).
    static constexpr uint64_t kAnimReconcileGraceTicks = 60;
};
