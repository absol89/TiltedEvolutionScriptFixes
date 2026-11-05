#pragma once

#include "Message.h"

#include <Structs/PlayerPerk.h>

using TiltedPhoques::Vector;

struct PlayerPerksRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kPlayerPerksRequest;

    PlayerPerksRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const PlayerPerksRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Perks == acRhs.Perks; }

    Vector<PlayerPerk> Perks{};
};
