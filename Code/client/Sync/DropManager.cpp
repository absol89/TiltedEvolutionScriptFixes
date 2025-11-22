#include "DropManager.h"

#include <TiltedCore/Stl.hpp>
#include <spdlog/spdlog.h>

namespace DropManager
{
namespace
{
    TiltedPhoques::Map<uint32_t, LocalDropData> s_localDrops;
    TiltedPhoques::Map<uint64_t, ServerDropData> s_serverDrops;
    TiltedPhoques::Map<uint32_t, uint64_t> s_handleBindings;
    uint32_t s_nextClientDropId = 1;
    StorageListener* s_pStorageListener = nullptr;

    void UnbindHandle(uint32_t handleBits) noexcept
    {
        if (handleBits == 0)
            return;

        s_handleBindings.erase(handleBits);
    }

    void BindHandle(uint32_t handleBits, uint64_t dropId) noexcept
    {
        if (handleBits == 0)
            return;
        s_handleBindings[handleBits] = dropId;
    }
} // namespace

uint32_t RegisterLocalDrop(LocalDropData data) noexcept
{
    const uint32_t clientDropId = s_nextClientDropId++;
    s_localDrops[clientDropId] = std::move(data);
    return clientDropId;
}

std::optional<LocalDropData> ConsumeLocalDrop(uint32_t clientDropId) noexcept
{
    const auto it = s_localDrops.find(clientDropId);
    if (it == s_localDrops.end())
        return std::nullopt;

    auto data = it->second;
    s_localDrops.erase(it);
    return data;
}

void TrackServerDrop(uint64_t dropId, const ServerDropData& data) noexcept
{
    s_serverDrops[dropId] = data;
    if (data.HandleBits)
        BindHandle(data.HandleBits, dropId);

    if (s_pStorageListener)
        s_pStorageListener->OnServerDropTracked(dropId, data);

    spdlog::info("DropManager: tracked drop {} actor {:X} item {:X}:{:X} loc ({:.2f}, {:.2f}, {:.2f}) handle {:X}", dropId, data.ActorFormId, data.Item.BaseId.ModId, data.Item.BaseId.BaseId,
                 data.Location.x, data.Location.y, data.Location.z, data.HandleBits);
}

bool BindHandleToServerDrop(uint64_t dropId, uint32_t actorFormId, uint32_t handleBits) noexcept
{
    auto dropIt = s_serverDrops.find(dropId);
    if (dropIt == s_serverDrops.end())
        return false;

    auto& drop = dropIt.value();
    drop.ActorFormId = actorFormId;
    drop.HandleBits = handleBits;
    BindHandle(handleBits, dropId);
    if (s_pStorageListener)
        s_pStorageListener->OnDropHandleBound(dropId, handleBits);
    if (s_pStorageListener)
        s_pStorageListener->OnDropHandleBound(dropId, handleBits);

    spdlog::info("DropManager: bound handle {:X} to drop {} actor {:X}", handleBits, dropId, actorFormId);
    return true;
}

std::optional<uint64_t> GetDropIdForHandle(uint32_t handleBits) noexcept
{
    if (handleBits == 0)
        return std::nullopt;

    const auto handleIt = s_handleBindings.find(handleBits);
    if (handleIt == s_handleBindings.end())
        return std::nullopt;

    return handleIt->second;
}

std::optional<uint32_t> GetHandleForDrop(uint64_t dropId) noexcept
{
    const auto it = s_serverDrops.find(dropId);
    if (it == s_serverDrops.end())
        return std::nullopt;

    if (it->second.HandleBits == 0)
        return std::nullopt;

    return it->second.HandleBits;
}

std::optional<ServerDropData> GetServerDrop(uint64_t dropId) noexcept
{
    const auto it = s_serverDrops.find(dropId);
    if (it == s_serverDrops.end())
        return std::nullopt;

    return it->second;
}

std::optional<uint64_t> FindDropBySignature(const GameId& aBaseId, const NiPoint3& aLocation, float aRadiusSq) noexcept
{
    uint64_t bestDropId = 0;
    float bestDistanceSq = aRadiusSq;

    for (const auto& [dropId, data] : s_serverDrops)
    {
        if (data.Item.BaseId != aBaseId)
            continue;

        const float dx = data.Location.x - aLocation.x;
        const float dy = data.Location.y - aLocation.y;
        const float dz = data.Location.z - aLocation.z;
        const float distSq = dx * dx + dy * dy + dz * dz;
        if (distSq <= bestDistanceSq)
        {
            bestDistanceSq = distSq;
            bestDropId = dropId;
        }
    }

    if (bestDropId == 0)
    {
        spdlog::debug("DropManager: no drop match for {:X}:{:X} near ({:.2f}, {:.2f}, {:.2f})", aBaseId.ModId, aBaseId.BaseId, aLocation.x, aLocation.y, aLocation.z);
        return std::nullopt;
    }

    spdlog::info("DropManager: matched drop {} for {:X}:{:X} within {:.2f}", bestDropId, aBaseId.ModId, aBaseId.BaseId, std::sqrt(bestDistanceSq));
    return bestDropId;
}

void RemoveServerDrop(uint64_t dropId) noexcept
{
    const auto it = s_serverDrops.find(dropId);
    if (it == s_serverDrops.end())
        return;

    UnbindHandle(it->second.HandleBits);
    s_serverDrops.erase(it);
    if (s_pStorageListener)
        s_pStorageListener->OnServerDropRemoved(dropId);

    spdlog::info("DropManager: removed drop {}", dropId);
}

void SetStorageListener(StorageListener* apListener) noexcept
{
    s_pStorageListener = apListener;
}
} // namespace DropManager
