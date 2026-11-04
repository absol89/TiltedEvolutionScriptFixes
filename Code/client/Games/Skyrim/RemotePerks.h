#pragma once

#include <Forms/BGSPerk.h>
#include <Structs/PlayerPerk.h>

#include <memory>
#include <unordered_map>
#include <vector>

/**
 * Remote player copies have none of the perks their player earned: Character::VisitPerks only reads the TESNPC, those
 * perks are stored on PlayerCharacter. When a remote player hits or casts on the local player, this client runs the
 * attacker side of the hit against that copy, so the attacker's perks have to come from here.
 * The perks are never added to the copy, since AddPerk would also run the perk's abilities on it.
 */
namespace RemotePerks
{
struct PerkData
{
    std::vector<PerkRankData> Perks;
    // Sorted the same way as AIProcess's perk entry arrays
    std::vector<BGSPerkEntry*> Entries[BGSEntryPointPerkEntry::kEntryPointCount];
};

using PerkDataPtr = std::shared_ptr<const PerkData>;
using PerkMap = std::unordered_map<uint32_t, PerkDataPtr>;

// Perks that aren't in the local player's load order are skipped.
PerkDataPtr BuildPerkData(const TiltedPhoques::Vector<PlayerPerk>& acPerks) noexcept;
// Replaces the remote players' perks, keyed by the form id of their copy.
void SetPerks(PerkMap&& aPerks) noexcept;
// The local player's perks, those on the base form included.
void CollectLocalPlayerPerks(TiltedPhoques::Vector<PlayerPerk>& aPerks) noexcept;
} // namespace RemotePerks

// While alive, HasPerk and GetPerkRank on remote players answer from their perk list on this thread.
struct ScopedRemotePerks
{
    ScopedRemotePerks() noexcept;
    ~ScopedRemotePerks() noexcept;

    TP_NOCOPYMOVE(ScopedRemotePerks);
};

// While alive, remote players' perk lists are ignored on this thread.
struct ScopedIgnoreRemotePerks
{
    ScopedIgnoreRemotePerks() noexcept;
    ~ScopedIgnoreRemotePerks() noexcept;

    TP_NOCOPYMOVE(ScopedIgnoreRemotePerks);
};
