#pragma once

#include "Message.h"
#include <Structs/GameId.h>

struct RequestDroppedItems final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestDroppedItems;

    RequestDroppedItems()
        : ClientMessage(Opcode)
    {
    }

    virtual ~RequestDroppedItems() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestDroppedItems& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && RequestAll == acRhs.RequestAll && HasCellFilter == acRhs.HasCellFilter && (!HasCellFilter || CellId == acRhs.CellId) && HasWorldSpaceFilter == acRhs.HasWorldSpaceFilter &&
               (!HasWorldSpaceFilter || WorldSpaceId == acRhs.WorldSpaceId);
    }

    uint32_t RequestId{};
    bool RequestAll{false};
    bool HasCellFilter{false};
    GameId CellId{};
    bool HasWorldSpaceFilter{false};
    GameId WorldSpaceId{};
};
