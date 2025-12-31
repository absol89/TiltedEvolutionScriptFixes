#pragma once

#include <entt/entt.hpp>

#include <Messages/NotifyActorDrop.h>
#include <Messages/NotifyDroppedItemPickedUp.h>
#include <Messages/NotifyDroppedItems.h>
#include <Messages/NotifyDroppedItemMove.h>
#include <Messages/NotifyDroppedItemPhysicsDisabled.h>
#include <Messages/RequestActorDrop.h>
#include <Messages/RequestPickupDroppedItem.h>
#include <Messages/RequestDroppedItems.h>
#include <Messages/RequestDroppedItemMove.h>
#include <Messages/RequestDroppedItemPhysicsDisabled.h>
#include <Events/CellChangeEvent.h>
#include <Events/GridCellChangeEvent.h>
#include <Events/ConnectedEvent.h>
#include <Events/EventDispatcher.h>
#include <Games/Events.h>
#include <Services/Generic/CoSaveService.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <TiltedCore/Stl.hpp>
#include <deque>

struct DropItemEvent;
struct PickupDroppedItemEvent;
struct UpdateEvent;

struct World;
struct TransportService;
struct Actor;

class DropService : public BSTEventSink<TESGrabReleaseEvent>, public BSTEventSink<TESLoadGameEvent>
{
public:
    DropService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept;
    ~DropService();

private:
    struct DropSyncContext;

    void OnDropEvent(const DropItemEvent& acEvent) noexcept;
    void OnPickupEvent(const PickupDroppedItemEvent& acEvent) noexcept;
    void OnNotifyDrop(const NotifyActorDrop& acMessage) noexcept;
    void OnNotifyPickup(const NotifyDroppedItemPickedUp& acMessage) noexcept;
    void OnNotifyDroppedItems(const NotifyDroppedItems& acMessage) noexcept;
    void OnNotifyDropMove(const NotifyDroppedItemMove& acMessage) noexcept;
    void OnNotifyDropPhysicsDisabled(const NotifyDroppedItemPhysicsDisabled& acMessage) noexcept;
    void OnConnected(const ConnectedEvent& acEvent) noexcept;
    void OnCellChange(const CellChangeEvent& acEvent) noexcept;
    void OnGridCellChange(const GridCellChangeEvent& acEvent) noexcept;
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    BSTEventResult OnEvent(const TESGrabReleaseEvent* apEvent, const EventDispatcher<TESGrabReleaseEvent>* apSender) override;
    BSTEventResult OnEvent(const TESLoadGameEvent* apEvent, const EventDispatcher<TESLoadGameEvent>* apSender) override;

    std::optional<uint32_t> ResolveServerId(uint32_t aFormId) const noexcept;
    bool EnsureActorReady(Actor* apActor, const char* apContext) const noexcept;
    bool ApplyDrop(const NotifyActorDrop& acMessage) noexcept;
    bool ApplyPickup(const NotifyDroppedItemPickedUp& acMessage) noexcept;
    bool EnsureStorageReady() noexcept;
    uint32_t SendDropSyncRequest(bool aRequestAll, bool aHasCellFilter, const GameId& acCellId, bool aHasWorldFilter, const GameId& acWorldId, TiltedPhoques::Vector<RequestDroppedItems::DiscoveryEntry> aDiscoveries) noexcept;
    void QueueDropSync(const GameId& acCellId, const GameId& acWorldId, bool aIncludeDiscovery) noexcept;
    void QueueLoadedExteriorCells(const GameId& acWorldId) noexcept;
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
    void UpdateDropPhysics(const UpdateEvent& acEvent) noexcept;
    void SendDropMoveRequest(uint64_t aDropId, TESObjectREFR* apReference, bool aForce) noexcept;
    void SendDropPhysicsDisabledRequest(uint64_t aDropId, TESObjectREFR* apReference) noexcept;
    void SendReferenceMoveRequest(const GameId& acReferenceId, TESObjectREFR* apReference) noexcept;
    void ReconcileCachedDrops(const GameId& acCellId, const GameId& acWorldId, const TiltedPhoques::Vector<uint64_t>& acAuthoritativeDropIds) noexcept;
    void ApplyCreationEngineCellSync(const DropSyncContext& acContext, const TiltedPhoques::Vector<GameId>& acPickedUpRefs) noexcept;
    void ProcessPendingCreationEngineRemovals() noexcept;
    GameId GetPlayerCellId() noexcept;
    GameId GetPlayerWorldId() noexcept;
    void RequestCellSync(bool aIncludeDiscovery) noexcept;
    TiltedPhoques::Vector<RequestDroppedItems::DiscoveryEntry> BuildDiscoveryEntries(const GameId& acCellId, const GameId& acWorldId) noexcept;
    void ForgetLocalDrop(uint64_t aDropId) noexcept;
    bool IsPickupRelevant(const NotifyDroppedItemPickedUp& acMessage) noexcept;
    bool IsDropCellLoaded(const GameId& acCellId, const GameId& acWorldId) noexcept;
    bool IsDropLocallyActive(uint64_t aDropId, const GameId& acReferenceId) const noexcept;

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
    CoSaveService& m_coSaveService;
    DropStorage& m_dropStorage;

    entt::scoped_connection m_dropEventConnection;
    entt::scoped_connection m_pickupEventConnection;
    entt::scoped_connection m_notifyDropConnection;
    entt::scoped_connection m_notifyPickupConnection;
    entt::scoped_connection m_notifyDroppedItemsConnection;
    entt::scoped_connection m_notifyDropMoveConnection;
    entt::scoped_connection m_notifyDropPhysicsDisabledConnection;
    entt::scoped_connection m_connectedEventConnection;
    entt::scoped_connection m_cellChangeConnection;
    entt::scoped_connection m_gridCellChangeConnection;
    entt::scoped_connection m_updateConnection;
    TiltedPhoques::Vector<PendingAction> m_pendingActions;
    std::string m_cachedUsername;
    uint32_t m_nextDropSyncRequestId{1};
    TiltedPhoques::Set<uint64_t> m_materializingDrops;
    TiltedPhoques::Set<uint64_t> m_localDrops;
    double m_periodicPlayerCellSyncAccumulator{0.0};

    struct QueuedDropSync
    {
        GameId CellId{};
        GameId WorldSpaceId{};
        bool IncludeDiscovery{false};
    };

    std::deque<QueuedDropSync> m_dropSyncQueue{};
    TiltedPhoques::Set<GameId> m_dropSyncQueuedCells{};
    GameId m_dropSyncWorldSpace{};
    double m_dropSyncQueueAccumulator{0.0};

    struct DropSyncContext
    {
        bool IsFullSync{false};
        GameId CellId{};
        GameId WorldSpaceId{};
    };

    std::unordered_map<uint32_t, DropSyncContext> m_pendingDropSyncs;
    // Tracks the latest spawn epoch processed per server drop to ignore stale notifications
    TiltedPhoques::Map<uint64_t, uint64_t> m_knownSpawnEpochs;
    TiltedPhoques::Set<uint64_t> m_grabbedDrops;
    TiltedPhoques::Map<uint64_t, float> m_dropPhysicsCooldowns;
    TiltedPhoques::Map<uint64_t, float> m_dropMoveSyncTimers;
    TiltedPhoques::Map<uint64_t, float> m_dropPhysicsDisableSuppressions;
    TiltedPhoques::Set<GameId> m_grabbedReferences;
    TiltedPhoques::Map<GameId, float> m_referencePhysicsCooldowns;
    TiltedPhoques::Map<GameId, float> m_referenceMoveSyncTimers;
    double m_grabEventSuppressionRemaining{0.0};
    bool m_suspendProcessing{false};
    bool m_requestResyncAfterSuspend{false};
    double m_suspendProcessingAccumulator{0.0};

    struct PendingCreationEngineRemoval
    {
        GameId CellId{};
        GameId WorldSpaceId{};
        uint32_t RemainingRetries{0};
    };

    std::unordered_map<GameId, PendingCreationEngineRemoval> m_pendingCreationEngineRemovals;
};
