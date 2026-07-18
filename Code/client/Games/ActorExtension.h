#pragma once

#include <Structs/ActionEvent.h>

struct ActorExtension
{
    enum
    {
        kRemote = 1 << 0,
        kPlayer = 1 << 1,
    };

    bool IsRemote() const noexcept;
    bool IsLocal() const noexcept;
    bool IsPlayer() const noexcept;
    bool IsRemotePlayer() const noexcept;
    bool IsLocalPlayer() const noexcept;
    void SetRemote(bool aSet) noexcept;
    void SetPlayer(bool aSet) noexcept;

    ActionEvent LatestAnimation{};
    size_t GraphDescriptorHash = 0;

    // Tick (World clock) at which this actor was last (re)localized via SetRemote(false).
    // Used by Animation.cpp to collapse the brief burst of reset/un-equip actions the engine
    // replays right after AI is unlocked (see issue #810: NPCs spam "Unequip"/"combatStanceStop"
    // and get stuck, refusing to aggro). 0 means "no grace window active".
    uint64_t LocalizedTick = 0;

    // After a (re)localize, records whether the NPC arrived already in a combat-ready
    // state (IsWeaponDrawn == true from the previous owner). Non-hostile NPCs arrive
    // sheathed (false); transferred hostiles (bandits, etc.) arrive drawn (true). Used by
    // the detection hook to engage combat ONLY for NPCs that were already hostile, so
    // peaceful NPCs in friendly areas are never force-aggro'd.
    bool ArrivedHostile = false;

    // Issue #810: one-shot latch for the detection-hook combat engage. Once we have fired
    // StartCombatEx for this NPC->player pair we set this true and NEVER fire again for the
    // life of the actor. Without it the hook re-kicks StartCombatEx on every detection
    // re-evaluation; StartCombatEx internally does StopCombat()+StartCombat(), and the
    // StopCombat() sheathes the weapon -- producing the visible draw/sheathe OSCILLATION
    // seen when a hostile NPC's ownership bounces between two nearby players (Embershard
    // bandits looping). GetCombatTarget()!=target is NOT a sufficient latch because
    // StopCombat() clears the combat target, so the next eval re-qualifies. This flag is
    // the real latch: engage once, then leave all further combat/sheathe decisions to the
    // engine. It is intentionally NOT reset on re-localize -- a contested NPC must not
    // re-trigger the burst each time it flips back to local.
    bool EngagedFromDetection = false;

    // Issue #810: one-shot latch for the OnHit retaliation engage. Fire/DoT damage delivers
    // a HitEvent every damage tick; OnHitEvent re-ran StartCombatEx on each one, and
    // StartCombatEx's internal StopCombat() sheathes + clears the combat target, so the
    // GetCombatTarget()!=hitter guard re-qualified next tick => draw/sheathe OSCILLATION
    // while an NPC burns. Latch the retaliation so we StartCombatEx once, then leave combat
    // to the engine. Reset when the NPC actually leaves combat so a later separate fight
    // still triggers.
    bool EngagedFromHit = false;

    // During the reconcile grace window we still perform every action locally (so visuals stay
    // correct) but only broadcast each distinct reset action ONCE. These two flags track which
    // of the two loop actions we have already forwarded, so the server sees a single clean
    // transition instead of a stuck spam loop -- without ever freezing the NPC.
    bool BroadcastedUnequip = false;
    bool BroadcastedCombatStanceStop = false;

    // Grace window (in World ticks) during which reset-type actions from a freshly-localized
    // NPC are de-duplicated on broadcast. ~1s at 60Hz tick.
    static constexpr uint64_t kAnimReconcileGraceTicks = 60;

  private:
    uint32_t onlineFlags{0};
};
