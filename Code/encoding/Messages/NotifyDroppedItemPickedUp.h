#pragma once

#include "Message.h"
#include <Structs/Inventory.h>

struct NotifyDroppedItemPickedUp final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyDroppedItemPickedUp;

    NotifyDroppedItemPickedUp()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyDroppedItemPickedUp& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && ServerId == acRhs.ServerId && Item == acRhs.Item && DropId == acRhs.DropId;
    }

    uint32_t ServerId{};
    Inventory::Entry Item{};
    uint64_t DropId{};
};
