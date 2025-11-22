#pragma once

#include <Events/PacketEvent.h>
#include <Messages/RequestActorDrop.h>
#include <Messages/RequestPickupDroppedItem.h>
#include <TiltedCore/Stl.hpp>

struct NotifyActorDrop;
struct NotifyDroppedItemPickedUp;

class World;
struct InventoryComponent;
struct OwnerComponent;

class DropService
{
public:
    DropService(World& aWorld, entt::dispatcher& aDispatcher);
    ~DropService() = default;

private:
    struct ActiveDrop
    {
        uint64_t DropId{};
        uint32_t ServerId{};
        Inventory::Entry DropEntry{};
        Inventory::Entry PickupEntry{};
        bool HasLocation{false};
        Vector3_NetQuantize Location{};
        bool HasRotation{false};
        Vector3_NetQuantize Rotation{};
    };

    void OnDropRequest(const PacketEvent<RequestActorDrop>& acMessage) noexcept;
    void OnPickupRequest(const PacketEvent<RequestPickupDroppedItem>& acMessage) noexcept;

    World& m_world;
    entt::scoped_connection m_requestDropConnection;
    entt::scoped_connection m_requestPickupConnection;

    uint64_t m_nextDropId{1};
    TiltedPhoques::Map<uint64_t, ActiveDrop> m_activeDrops;
};
