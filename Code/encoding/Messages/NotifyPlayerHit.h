#pragma once

#include "Message.h"
#include <Structs/GameId.h>

/**
 * A hit the attacker's client saw land on another player. The damage is before the victim's armor and block, which
 * the victim's own client applies, see Actor::ApplyHitFromOwner().
 */
struct NotifyPlayerHit final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyPlayerHit;

    NotifyPlayerHit()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyPlayerHit& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && HitteeId == acRhs.HitteeId && AttackerId == acRhs.AttackerId && WeaponId == acRhs.WeaponId && Damage == acRhs.Damage &&
               Flags == acRhs.Flags;
    }

    // Server ids, AttackerId is 0 when the attacker isn't synced.
    uint32_t HitteeId{};
    uint32_t AttackerId{};
    GameId WeaponId{};
    // HitData physical damage: after the attacker's perks, sneak and critical, before the target's armor and block.
    float Damage{};
    // The HitData flags the armor and block step reads (power attack, bash, left hand).
    uint32_t Flags{};
};
