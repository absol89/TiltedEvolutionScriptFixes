#pragma once

#include <Structs/Inventory.h>
#include <Structs/GameId.h>
#include <Structs/ServerItemType.h>
#include <Games/Primitives.h>
#include <TiltedCore/Stl.hpp>

#include <filesystem>

namespace CoSaveStorage
{
struct Entry
{
    uint64_t DropId{};
    ServerItemType Type{ServerItemType::Dropped};
    uint32_t ServerId{};
    GameId CellId{};
    GameId WorldSpaceId{};
    GameId ReferenceId{};
    Inventory::Entry Item{};
    NiPoint3 Location{};
    NiPoint3 Rotation{};
    uint32_t RefFormId{};
    uint64_t LastSeenTimestamp{};
};

bool Load(const std::filesystem::path& aPath, TiltedPhoques::Map<uint64_t, Entry>& aOut) noexcept;
bool Save(const std::filesystem::path& aPath, const TiltedPhoques::Map<uint64_t, Entry>& aIn) noexcept;
} // namespace CoSaveStorage
