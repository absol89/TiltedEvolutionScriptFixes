#pragma once

#include "Message.h"
#include <Structs/GameId.h>
#include <Structs/Vector3_NetQuantize.h>

struct NotifyPartyLeaderCellLock final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyPartyLeaderCellLock;

    NotifyPartyLeaderCellLock()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyPartyLeaderCellLock& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && WorldSpaceId == acRhs.WorldSpaceId && CellId == acRhs.CellId && Position == acRhs.Position && CountdownSeconds == acRhs.CountdownSeconds && Cancelled == acRhs.Cancelled;
    }

    GameId WorldSpaceId{};
    GameId CellId{};
    Vector3_NetQuantize Position{};
    uint16_t CountdownSeconds{};
    bool Cancelled{};
};
