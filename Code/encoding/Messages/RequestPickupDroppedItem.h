#pragma once

#include "Message.h"

struct RequestPickupDroppedItem final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestPickupDroppedItem;

    RequestPickupDroppedItem()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestPickupDroppedItem& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && ServerId == acRhs.ServerId && DropId == acRhs.DropId;
    }

    uint32_t ServerId{};
    uint64_t DropId{};
};
