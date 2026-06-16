#pragma once

#include <Games/Skyrim/Forms/TESObjectCELL.h>

struct NiPoint3;

namespace RespawnOverrides
{
// If the cell has a known override, write the respawn position into aOutPos and return true.
bool GetRespawnPos(TESObjectCELL* apCell, NiPoint3& aOutPos) noexcept;
}
