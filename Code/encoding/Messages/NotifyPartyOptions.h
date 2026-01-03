#pragma once

#include "Message.h"

#include <Structs/PartyOptions.h>

// Server -> Client: announce party option changes.
struct NotifyPartyOptions final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyPartyOptions;

    NotifyPartyOptions()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyPartyOptions& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && Options == acRhs.Options;
    }

    PartyOptions Options{};
};
