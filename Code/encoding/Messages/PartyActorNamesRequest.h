#pragma once

#include "Message.h"

// Client -> Server: request current party actor names.
struct PartyActorNamesRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kPartyActorNamesRequest;

    PartyActorNamesRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;
};
