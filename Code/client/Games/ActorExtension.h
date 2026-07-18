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
