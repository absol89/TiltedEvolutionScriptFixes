#pragma once

#include "Message.h"

#include <Structs/PartyOptions.h>

// Client -> Server: update party options (leader-only).
struct PartyOptionsUpdateRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kPartyOptionsUpdateRequest;

    PartyOptionsUpdateRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    PartyOptions Options{};
};
