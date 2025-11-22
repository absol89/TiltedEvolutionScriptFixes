#pragma once

#include <Structs/Inventory.h>
#include <Games/Primitives.h>
#include <optional>

namespace DropManager
{
struct LocalDropData
{
    uint32_t ActorFormId{};
    Inventory::Entry Item{};
    NiPoint3 Location{};
    NiPoint3 Rotation{};
    uint32_t HandleBits{};
};

struct ServerDropData
{
    uint32_t ActorFormId{};
    Inventory::Entry Item{};
    NiPoint3 Location{};
    NiPoint3 Rotation{};
    uint32_t HandleBits{};
};

uint32_t RegisterLocalDrop(LocalDropData data) noexcept;
std::optional<LocalDropData> ConsumeLocalDrop(uint32_t clientDropId) noexcept;

void TrackServerDrop(uint64_t dropId, const ServerDropData& data) noexcept;
bool BindHandleToServerDrop(uint64_t dropId, uint32_t actorFormId, uint32_t handleBits) noexcept;

std::optional<uint64_t> GetDropIdForHandle(uint32_t handleBits) noexcept;
std::optional<uint32_t> GetHandleForDrop(uint64_t dropId) noexcept;
std::optional<ServerDropData> GetServerDrop(uint64_t dropId) noexcept;

void RemoveServerDrop(uint64_t dropId) noexcept;
} // namespace DropManager
