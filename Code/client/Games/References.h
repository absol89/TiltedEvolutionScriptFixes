#pragma once

#include <Games/ActorExtension.h>
#include <PlayerCharacter.h>

namespace GameplayFormulas
{
float CalculateRealDamage(Actor* apHittee, float aDamage, bool aKillMove) noexcept;
}

namespace Settings
{
int32_t* GetDifficulty() noexcept;
float* GetGreetDistance() noexcept;
float* GetVATSSelectTargetTimeMultiplier() noexcept;
} // namespace Settings

// Actor.PlaySubGraphAnimation without sending it to the other players. Returns whether a worn item's graph took the
// event, which it can't while that item isn't loaded.
bool SendSubGraphEventUnsynced(TESObjectREFR* apRef, const char* acpEventName) noexcept;

void FadeOutGame(bool aFadingOut, bool aBlackFade, float aFadeDuration, bool aRemainVisible, float aSecondsToFade) noexcept;

struct ExActor : Actor, ActorExtension
{
};

struct ExPlayerCharacter : PlayerCharacter, ActorExtension
{
};
