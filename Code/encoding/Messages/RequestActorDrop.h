#pragma once

#include "Message.h"
#include <Structs/Inventory.h>
#include <Structs/Vector3_NetQuantize.h>

struct RequestActorDrop final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestActorDrop;

    RequestActorDrop()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestActorDrop& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && ServerId == acRhs.ServerId && Item == acRhs.Item && ClientDropId == acRhs.ClientDropId && HasLocation == acRhs.HasLocation &&
               (!HasLocation || Location == acRhs.Location) && HasRotation == acRhs.HasRotation && (!HasRotation || Rotation == acRhs.Rotation);
    }

    uint32_t ServerId{};
    Inventory::Entry Item{};
    uint32_t ClientDropId{};
    bool HasLocation = false;
    Vector3_NetQuantize Location{};
    bool HasRotation = false;
    Vector3_NetQuantize Rotation{};
};
