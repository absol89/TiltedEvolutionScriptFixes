#pragma once

#include "Message.h"
#include <Structs/Inventory.h>
#include <Structs/Vector3_NetQuantize.h>

struct NotifyActorDrop final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyActorDrop;

    NotifyActorDrop()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyActorDrop& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && ServerId == acRhs.ServerId && Item == acRhs.Item && DropId == acRhs.DropId && HasLocation == acRhs.HasLocation &&
               (!HasLocation || Location == acRhs.Location) && HasRotation == acRhs.HasRotation && (!HasRotation || Rotation == acRhs.Rotation) && HasClientDropId == acRhs.HasClientDropId &&
               (!HasClientDropId || ClientDropId == acRhs.ClientDropId);
    }

    uint32_t ServerId{};
    Inventory::Entry Item{};
    uint64_t DropId{};
    bool HasClientDropId = false;
    uint32_t ClientDropId{};
    bool HasLocation = false;
    Vector3_NetQuantize Location{};
    bool HasRotation = false;
    Vector3_NetQuantize Rotation{};
};
