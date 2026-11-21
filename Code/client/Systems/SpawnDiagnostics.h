#pragma once

struct Actor;
struct TESNPC;
struct Inventory;

/**
 * @brief Client-only logging to hunt duplicated NPCs and copies that show the wrong model or armor.
 *
 * Every line starts with "[SpawnDiag]" in the normal client log. Lines that point at the bugs are warnings carrying
 * "FLAG" and the reason. Nothing here changes what is spawned, sent or shown.
 *
 * - Lookalikes: NPCs of the same kind (same leveled pick or base) standing close together where at least one of them
 *   was created at runtime (a temporary reference, like a random encounter or a copy this client spawned for another
 *   player's NPC). Two Thalmor patrols or two copies of the same bard show up here.
 * - Two entities pointing at the same game reference.
 * - Another client's appearance loaded onto an NPC base that other actors here share, which changes them all.
 * - A remote player's puppet whose base, race or outfit changes after it was spawned, or that isn't a puppet base.
 * - A remote NPC's worn armor replaced by an inventory from its owner.
 */
namespace SpawnDiagnostics
{
// Looks for lookalikes, shared references and changed player puppets. Cheap to call every frame, it runs every few seconds.
void Update() noexcept;

// OnCharacterSpawn or CreateCharacterForEntity created a copy of another client's NPC for server id aServerId.
// aFromCustomForm: the owner's NPC was a temporary reference, so this client made a new actor for it.
void OnRemoteSpawned(Actor* apActor, uint32_t aServerId, bool aFromCustomForm, bool aIsPlayer) noexcept;

// Called before and after another client's appearance is loaded onto apNpc, an existing base of this game.
struct BaseSnapshot
{
    uint32_t RaceId = 0;
    uint32_t OutfitId = 0;
    uint32_t FaceNpcId = 0;
};
BaseSnapshot SnapshotBase(const TESNPC* apNpc) noexcept;
void OnBaseAppearanceLoaded(const TESNPC* apNpc, const BaseSnapshot& acBefore, uint32_t aServerId, uint32_t aChangeFlags) noexcept;

// Actor::SetActorInventory is about to replace acCurrent with acNew on apActor.
void OnInventoryApplied(Actor* apActor, const Inventory& acCurrent, const Inventory& acNew) noexcept;
} // namespace SpawnDiagnostics
