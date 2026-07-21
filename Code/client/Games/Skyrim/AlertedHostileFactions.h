#pragma once

#include <cstdint>
#include <vector>

struct Actor;

namespace AlertedHostileFactions
{
const std::vector<uint32_t>& GetAlertedHostileFactionIds() noexcept;

bool IsAlertedHostileFaction(const Actor* apActor) noexcept;
}
