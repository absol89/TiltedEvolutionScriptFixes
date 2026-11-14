#pragma once

/**
 * @brief Dispatched when the local player or an NPC owned here lands a weapon hit on a remote player.
 *
 * Damage is the hit's physical damage before the victim's armor and block, see Actor::ApplyHitFromOwner().
 */
struct PlayerHitEvent
{
    uint32_t HitteeId;
    uint32_t AttackerId;
    uint32_t WeaponId;
    float Damage;
    uint32_t Flags;
};
