#pragma once

struct Actor;

/**
 * @brief Client-only logging of damage involving players, weapon and spell hits,, so playtesters can check for hits applied twice.
 *
 * Every line starts with "[DmgDiag]" in the normal client log. Lines that look wrong are warnings carrying "FLAG" and
 * the reason. Damage-over-time ticks under 1 point (fire cloaks and the like) are not logged. Nothing here changes what
 * is sent or how damage is applied.
 */
namespace DamageDiagnostics
{
// HookDamageActor applies a hit on this client. Returns the hit's number for OnDamageApplied().
uint64_t OnLocalHit(Actor* apHittee, Actor* apHitter, float aRawDamage, float aRealDamage, const char* acpPath) noexcept;
// The game applied the hit OnLocalHit() numbered. aHealthBefore is the hittee's health before it.
void OnDamageApplied(uint64_t aHitId, Actor* apHittee, float aHealthBefore) noexcept;
// A weapon hit on a remote player is sent to that player's client.
void OnPlayerHitSent(Actor* apHittee, Actor* apAttacker, float aPhysicalDamage) noexcept;
// Another client's weapon hit is applied to the local player.
void OnPlayerHitReceived(Actor* apAttacker, float aPhysicalDamage, float aAfterArmor, float aAfterPerks, float aHealthBefore, float aHealthAfter) noexcept;
// A hostile spell from this client's player or an NPC it runs hit a remote player, and is sent to that player's client.
void OnSpellHitSent(Actor* apHittee, Actor* apCaster, const char* acpSpellName, uint32_t aEffectId, float aMagnitude) noexcept;
// Another client's spell hit was applied to the local player. aApplied is what the game's AddTarget returned.
void OnSpellHitReceived(Actor* apCaster, const char* acpSpellName, uint32_t aEffectId, float aMagnitude, bool aApplied, float aHealthBefore, float aHealthAfter) noexcept;
// A health delta from another client is applied to apActor.
void OnHealthBroadcastReceived(uint32_t aServerId, float aDelta) noexcept;
} // namespace DamageDiagnostics
