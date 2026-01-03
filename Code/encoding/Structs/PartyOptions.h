#pragma once

using TiltedPhoques::Buffer;

struct PartyOptions
{
    enum Flags : uint32_t
    {
        kSyncFastTravelMarkers = 1u << 0,
        kShowPartyMemberMarkers = 1u << 1,
        kSyncDeadBodyLoot = 1u << 2,
        kLockPartyToLeaderCell = 1u << 3,
    };

    uint32_t FlagsMask{ kShowPartyMemberMarkers };

    bool operator==(const PartyOptions& acRhs) const noexcept { return FlagsMask == acRhs.FlagsMask; }
    bool operator!=(const PartyOptions& acRhs) const noexcept { return !(*this == acRhs); }

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    bool SyncFastTravelMarkers() const noexcept { return (FlagsMask & kSyncFastTravelMarkers) != 0u; }
    bool ShowPartyMemberMarkers() const noexcept { return (FlagsMask & kShowPartyMemberMarkers) != 0u; }
    bool SyncDeadBodyLoot() const noexcept { return (FlagsMask & kSyncDeadBodyLoot) != 0u; }
    bool LockPartyToLeaderCell() const noexcept { return (FlagsMask & kLockPartyToLeaderCell) != 0u; }

    void SetSyncFastTravelMarkers(bool aEnabled) noexcept;
    void SetShowPartyMemberMarkers(bool aEnabled) noexcept;
    void SetSyncDeadBodyLoot(bool aEnabled) noexcept;
    void SetLockPartyToLeaderCell(bool aEnabled) noexcept;

private:
    void SetFlag(uint32_t aFlag, bool aEnabled) noexcept;
};
