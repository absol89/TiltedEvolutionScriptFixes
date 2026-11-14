#pragma once

struct Actor;

/**
 * @brief Client-only logging of the damage and health sync path, to track down hits that are applied twice or not at all.
 *
 * Every line starts with "[DmgDiag]" in the normal client log. Lines that look wrong are logged as warnings and carry a
 * "FLAG" with the reason, so a log can be searched for "[DmgDiag]" and "FLAG". Nothing here changes what is sent to the
 * server or how damage is applied.
 */
namespace DamageDiagnostics
{
// HookDamageActor saw a hit. aApplied is false when this client leaves the hit to the actor's owner. Returns the hit's number.
uint64_t OnDamageHook(Actor* apHittee, Actor* apHitter, float aRawDamage, float aRealDamage, bool aKillMove, const char* acpPath, bool aApplied) noexcept;

// The game ran the hit that OnDamageHook() numbered. aHealthBefore is the hittee's health before the game applied it.
void OnDamageApplied(uint64_t aHitId, Actor* apHittee, float aHealthBefore) noexcept;

// A health delta for an actor is being sent to the server. aBatched is true for the small changes sent on a timer.
void OnHealthChangeSent(uint32_t aServerId, uint32_t aFormId, float aDelta, bool aBatched) noexcept;

// Another client's health delta arrived and was applied to apActor.
void OnHealthBroadcastReceived(uint32_t aServerId, Actor* apActor, float aDelta, float aHealthBefore, float aHealthAfter) noexcept;

// The owner's absolute health for a player arrived. The client ignores it for health, so this only compares it with the local copy.
void OnHealthSnapshot(uint32_t aServerId, Actor* apActor, float aReportedHealth) noexcept;
} // namespace DamageDiagnostics
