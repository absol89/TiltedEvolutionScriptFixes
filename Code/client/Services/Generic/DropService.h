#pragma once

#include <entt/entt.hpp>

#include <Messages/NotifyActorDrop.h>
#include <Messages/NotifyDroppedItemPickedUp.h>
#include <Messages/NotifyDroppedItems.h>
#include <Messages/RequestActorDrop.h>
#include <Messages/RequestPickupDroppedItem.h>
#include <Messages/RequestDroppedItems.h>
#include <Events/CellChangeEvent.h>
#include <Events/ConnectedEvent.h>
#include <Services/Generic/DropStorage.h>

#include <optional>
#include <string>
#include <unordered_map>
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
    ~DropService();

private:
    void OnDropEvent(const DropItemEvent& acEvent) noexcept;
    void OnPickupEvent(const PickupDroppedItemEvent& acEvent) noexcept;
    void OnNotifyDrop(const NotifyActorDrop& acMessage) noexcept;
    void OnNotifyPickup(const NotifyDroppedItemPickedUp& acMessage) noexcept;
    void OnNotifyDroppedItems(const NotifyDroppedItems& acMessage) noexcept;
    void OnConnected(const ConnectedEvent& acEvent) noexcept;
    void OnCellChange(const CellChangeEvent& acEvent) noexcept;
    void OnUpdate(const UpdateEvent& acEvent) noexcept;

    std::optional<uint32_t> ResolveServerId(uint32_t aFormId) const noexcept;
    bool EnsureActorReady(Actor* apActor, const char* apContext) const noexcept;
    bool ApplyDrop(const NotifyActorDrop& acMessage) noexcept;
    bool ApplyPickup(const NotifyDroppedItemPickedUp& acMessage) noexcept;
    bool EnsureStorageReady() noexcept;
    uint32_t SendDropSyncRequest(bool aRequestAll, bool aHasCellFilter, const GameId& acCellId, bool aHasWorldFilter, const GameId& acWorldId) noexcept;
    void HandleDropSyncResponse(const NotifyDroppedItems& acMessage) noexcept;
    void ProcessDropEntry(const NotifyDroppedItems::Entry& acEntry, bool aForceMaterialize) noexcept;
    bool MaterializeDrop(uint64_t aDropId, const DropManager::ServerDropData& acData, bool aForce) noexcept;
    bool SpawnLocalDrop(const DropManager::ServerDropData& acData, uint64_t aDropId) const noexcept;
    bool RemoveNearbyReference(uint64_t aDropId, const char* apReason, float aRadiusSq) noexcept;
    bool RemoveReferenceById(const GameId& acReferenceId, const char* apReason) noexcept;
    bool RemoveReferenceByLocation(const Inventory::Entry& acItem, const Vector3_NetQuantize& acLocation, const char* apReason, float aRadiusSq) noexcept;
    bool TryBindExistingReference(uint64_t aDropId, const DropManager::ServerDropData& acData) noexcept;
    TESObjectREFR* GetReferenceById(const GameId& acReferenceId) noexcept;
    bool HandleUntrackedPickup(const NotifyDroppedItemPickedUp& acMessage) noexcept;
    void ReconcileCachedDrops(const GameId& acCellId, const GameId& acWorldId, const TiltedPhoques::Vector<uint64_t>& acAuthoritativeDropIds) noexcept;
    GameId GetPlayerCellId() noexcept;
    GameId GetPlayerWorldId() noexcept;
    void RequestCellSync() noexcept;
    void ForgetLocalDrop(uint64_t aDropId) noexcept;
    bool IsPickupRelevant(const NotifyDroppedItemPickedUp& acMessage) noexcept;

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
        uint32_t RetryCounter{0};
    };

    World& m_world;
    entt::dispatcher& m_dispatcher;
    TransportService& m_transport;

    entt::scoped_connection m_dropEventConnection;
    entt::scoped_connection m_pickupEventConnection;
    entt::scoped_connection m_notifyDropConnection;
    entt::scoped_connection m_notifyPickupConnection;
    entt::scoped_connection m_notifyDroppedItemsConnection;
    entt::scoped_connection m_connectedEventConnection;
    entt::scoped_connection m_cellChangeConnection;
    entt::scoped_connection m_updateConnection;
    TiltedPhoques::Vector<PendingAction> m_pendingActions;
    DropStorage m_dropStorage;
    std::string m_cachedUsername;
    uint32_t m_nextDropSyncRequestId{1};
    TiltedPhoques::Set<uint64_t> m_materializingDrops;
    TiltedPhoques::Set<uint64_t> m_localDrops;

    struct DropSyncContext
    {
        bool IsFullSync{false};
        GameId CellId{};
        GameId WorldSpaceId{};
    };

    std::unordered_map<uint32_t, DropSyncContext> m_pendingDropSyncs;
    // Tracks the latest spawn epoch processed per server drop to ignore stale notifications
    TiltedPhoques::Map<uint64_t, uint64_t> m_knownSpawnEpochs;
};
