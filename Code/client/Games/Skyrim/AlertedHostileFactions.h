#pragma once

#include <Games/Skyrim/Forms/TESFaction.h>
#include <Games/Skyrim/Forms/TESActorBase.h>

namespace AlertedHostileFactions
{
// Sorted set of form IDs loaded from AlertedHostileFactions.ini.
const std::vector<uint32_t>& GetAlertedHostileFactionIds() noexcept;

// True if the actor belongs to a configured alerted hostile faction.
bool IsAlertedHostileFaction(const Actor* apActor) noexcept;
}
