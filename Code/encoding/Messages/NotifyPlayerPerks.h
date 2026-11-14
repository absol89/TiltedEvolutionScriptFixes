#pragma once

#include "Message.h"

#include <Structs/PlayerPerk.h>

using TiltedPhoques::Vector;

struct NotifyPlayerPerks final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyPlayerPerks;

    NotifyPlayerPerks()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyPlayerPerks& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && PlayerId == acRhs.PlayerId && Perks == acRhs.Perks; }

    uint32_t PlayerId{};
    Vector<PlayerPerk> Perks{};
};
