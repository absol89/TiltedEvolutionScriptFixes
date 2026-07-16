#pragma once

#include "Message.h"

using TiltedPhoques::String;

struct NotifyPlayerActorName final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyPlayerActorName;

    NotifyPlayerActorName()
        : ServerMessage(Opcode)
    {
    }

    virtual ~NotifyPlayerActorName() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyPlayerActorName& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && PlayerId == acRhs.PlayerId && ActorName == acRhs.ActorName;
    }

    uint32_t PlayerId{};
    String ActorName{};
};
