#pragma once

#include <Games/Skyrim/Forms/TESObjectCELL.h>

struct NiPoint3;

namespace RespawnOverrides
{
bool GetRespawnPos(const char* apCellEditorId, NiPoint3& aOutPos) noexcept;
bool GetRespawnPos(const TESObjectCELL* apCell, NiPoint3& aOutPos) noexcept;
}
