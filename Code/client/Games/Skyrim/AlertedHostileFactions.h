#pragma once

#include <Games/Skyrim/Forms/TESFaction.h>
#include <Games/Skyrim/Forms/TESActorBase.h>

namespace AlertedHostileFactions
{
std::vector<TESFaction*> GetAlertedFactions();

// True if the actor/world model belongs to a configured alerted hostile faction.
bool IsAlertedHostileFaction(TESActorBase* apActorBase);
bool IsAlertedHostileFaction(TESForm* apForm);
}
