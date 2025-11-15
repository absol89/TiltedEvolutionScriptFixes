#pragma once

#include "Message.h"
#include <Structs/GameId.h>

struct NotifyHealingProximity final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyHealingProximity;

    NotifyHealingProximity()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyHealingProximity& acRhs) const noexcept 
    { 
        return CasterId == acRhs.CasterId && 
               CasterX == acRhs.CasterX && 
               CasterY == acRhs.CasterY && 
               CasterZ == acRhs.CasterZ && 
               SpellFormId == acRhs.SpellFormId && 
               GetOpcode() == acRhs.GetOpcode(); 
    }

    uint32_t CasterId;
    float CasterX;
    float CasterY;
    float CasterZ;
    GameId SpellFormId{};
};