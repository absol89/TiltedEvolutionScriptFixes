#pragma once

#include "Message.h"
#include <Structs/Inventory.h>
#include <Structs/Vector3_NetQuantize.h>

struct RequestInventoryChanges final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestInventoryChanges;

    RequestInventoryChanges()
        : ClientMessage(Opcode)
    {
    }

    virtual ~RequestInventoryChanges() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestInventoryChanges& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && ServerId == acRhs.ServerId && Item == acRhs.Item && Drop == acRhs.Drop && UpdateClients == acRhs.UpdateClients && HasDropInstanceId == acRhs.HasDropInstanceId &&
               (!HasDropInstanceId || DropInstanceId == acRhs.DropInstanceId) && HasDropLocation == acRhs.HasDropLocation && (!HasDropLocation || DropLocation == acRhs.DropLocation) && HasDropRotation == acRhs.HasDropRotation &&
               (!HasDropRotation || DropRotation == acRhs.DropRotation);
    }

    uint32_t ServerId{};
    Inventory::Entry Item{};
    bool Drop = false;
    bool UpdateClients = true;
    bool HasDropInstanceId = false;
    uint32_t DropInstanceId = 0;
    bool HasDropLocation = false;
    Vector3_NetQuantize DropLocation{};
    bool HasDropRotation = false;
    Vector3_NetQuantize DropRotation{};
};
