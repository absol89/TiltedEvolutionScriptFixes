#pragma once

#include "Message.h"

using TiltedPhoques::String;

struct PlayerActorNameUpdateRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kPlayerActorNameUpdateRequest;

    PlayerActorNameUpdateRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const PlayerActorNameUpdateRequest& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && ActorName == acRhs.ActorName;
    }

    String ActorName{};
};
