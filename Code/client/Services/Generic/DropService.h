#pragma once

#include <entt/entt.hpp>

#include <Messages/NotifyActorDrop.h>
#include <Messages/NotifyDroppedItemPickedUp.h>
#include <Messages/RequestActorDrop.h>
#include <Messages/RequestPickupDroppedItem.h>

#include <optional>
#include <TiltedCore/Stl.hpp>

struct DropItemEvent;
struct PickupDroppedItemEvent;
struct UpdateEvent;

struct World;
struct TransportService;
struct Actor;

class DropService
{
public:
    DropService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept;
    ~DropService() = default;

private:
    void OnDropEvent(const DropItemEvent& acEvent) noexcept;
    void OnPickupEvent(const PickupDroppedItemEvent& acEvent) noexcept;
    void OnNotifyDrop(const NotifyActorDrop& acMessage) noexcept;
    void OnNotifyPickup(const NotifyDroppedItemPickedUp& acMessage) noexcept;
    void OnUpdate(const UpdateEvent& acEvent) noexcept;

    std::optional<uint32_t> ResolveServerId(uint32_t aFormId) const noexcept;
    bool EnsureActorReady(Actor* apActor, const char* apContext) const noexcept;
    bool ApplyDrop(const NotifyActorDrop& acMessage) noexcept;
    bool ApplyPickup(const NotifyDroppedItemPickedUp& acMessage) noexcept;

    enum class PendingType
    {
        Drop,
        Pickup
    };

    struct PendingAction
    {
        PendingType Type{};
        NotifyActorDrop DropMessage{};
        NotifyDroppedItemPickedUp PickupMessage{};
    };

    World& m_world;
    entt::dispatcher& m_dispatcher;
    TransportService& m_transport;

    entt::scoped_connection m_dropEventConnection;
    entt::scoped_connection m_pickupEventConnection;
    entt::scoped_connection m_notifyDropConnection;
    entt::scoped_connection m_notifyPickupConnection;
    entt::scoped_connection m_updateConnection;
    TiltedPhoques::Vector<PendingAction> m_pendingActions;
};
