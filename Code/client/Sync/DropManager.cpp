#include "DropManager.h"

#include <TiltedCore/Stl.hpp>

namespace DropManager
{
namespace
{
    TiltedPhoques::Map<uint32_t, LocalDropData> s_localDrops;
    TiltedPhoques::Map<uint64_t, ServerDropData> s_serverDrops;
    TiltedPhoques::Map<uint32_t, uint64_t> s_handleBindings;
    uint32_t s_nextClientDropId = 1;

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

void RemoveServerDrop(uint64_t dropId) noexcept
{
    const auto it = s_serverDrops.find(dropId);
    if (it == s_serverDrops.end())
        return;

    UnbindHandle(it->second.HandleBits);
    s_serverDrops.erase(it);
}
} // namespace DropManager
