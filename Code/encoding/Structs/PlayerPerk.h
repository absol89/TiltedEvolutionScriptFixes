#pragma once

#include <Structs/GameId.h>

/**
 * @brief A perk a player has, and the rank they have it at.
 */
struct PlayerPerk
{
    bool operator==(const PlayerPerk& acRhs) const noexcept { return Id == acRhs.Id && Rank == acRhs.Rank; }
    bool operator!=(const PlayerPerk& acRhs) const noexcept { return !operator==(acRhs); }

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    GameId Id{};
    uint8_t Rank{};
};
