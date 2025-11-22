#include "DropService.h"

#include <World.h>
#include <Components.h>
#include <Services/TransportService.h>
#include <Services/RunnerService.h>
#include <Events/UpdateEvent.h>
#include <Events/DropItemEvent.h>
#include <Events/PickupDroppedItemEvent.h>
#include <Events/ConnectedEvent.h>
#include <Events/CellChangeEvent.h>

#include <Messages/NotifyDroppedItems.h>
#include <Messages/RequestDroppedItems.h>

#include <Sync/DropManager.h>
#include <Sync/DropExecutionContext.h>

#include <Games/Overrides.h>
#include <Games/TES.h>

#include <Actor.h>
#include <Games/ActorExtension.h>
#include <PlayerCharacter.h>

#include <TESObjectREFR.h>
#include <Forms/TESObjectCELL.h>
#include <Forms/TESWorldSpace.h>
#include <Forms/TESForm.h>

#include <Utils.h>
#include <ExtraData/ExtraContainerChanges.h>
#include <Games/Primitives.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr float kDropSearchRadiusSquared = 200.f * 200.f;

NiPoint3 ToPoint(const Vector3_NetQuantize& aVector)
{
    return NiPoint3(aVector);
}

Vector3_NetQuantize ToNetVector(const NiPoint3& aVector)
{
    Vector3_NetQuantize value;
    value.x = aVector.x;
    value.y = aVector.y;
    value.z = aVector.z;
    return value;
}

TESBoundObject* ResolveDroppedObject(const Inventory::Entry& acEntry)
{
    auto& modSystem = World::Get().GetModSystem();
    const uint32_t objectId = modSystem.GetGameId(acEntry.BaseId);
    return Cast<TESBoundObject>(TESForm::GetById(objectId));
}

TESObjectREFR* FindReferenceNear(TESBoundObject* apObject, const NiPoint3& acCenter, float aRadiusSq = kDropSearchRadiusSquared)
{
    if (!apObject)
        return nullptr;

    TES* pTes = TES::Get();
    if (!pTes || !pTes->cells || !pTes->cells->arr)
        return nullptr;

    const int dimension = pTes->cells->dimension;
    if (dimension <= 0)
        return nullptr;

    TESObjectREFR* pClosest = nullptr;
    float closestDistanceSq = aRadiusSq;

    const int cellCount = dimension * dimension;
    for (int i = 0; i < cellCount; ++i)
    {
        TESObjectCELL* pCell = pTes->cells->arr[i];
        if (!pCell || !pCell->IsValid())
            continue;

        auto* pReferences = pCell->refData.refArray;
        if (!pReferences)
            continue;

        const uint32_t referenceCount = pCell->refData.Count();
        for (uint32_t j = 0; j < referenceCount; ++j)
        {
            TESObjectREFR* pCandidate = pReferences[j].Get();
            if (!pCandidate || pCandidate->baseForm != apObject || pCandidate->formType == Actor::Type)
                continue;

            const float diffX = pCandidate->position.x - acCenter.x;
            const float diffY = pCandidate->position.y - acCenter.y;
            const float diffZ = pCandidate->position.z - acCenter.z;
            const float distanceSq = diffX * diffX + diffY * diffY + diffZ * diffZ;

            if (distanceSq < closestDistanceSq)
            {
                closestDistanceSq = distanceSq;
                pClosest = pCandidate;
            }
        }
    }

    return pClosest;
}

std::pair<GameId, GameId> ResolveCellMetadata(World& aWorld, Actor* apActor)
{
    GameId cell{};
    GameId world{};

    if (!apActor)
        return {cell, world};

    TESObjectCELL* pCell = apActor->parentCell ? apActor->parentCell : apActor->GetParentCell();
    if (pCell)
    {
        aWorld.GetModSystem().GetServerModId(pCell->formID, cell);
        if (pCell->worldspace)
            aWorld.GetModSystem().GetServerModId(pCell->worldspace->formID, world);
    }

    return {cell, world};
}
} // namespace

DropService::DropService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
    , m_transport(aTransport)
{
    m_dropEventConnection = m_dispatcher.sink<DropItemEvent>().connect<&DropService::OnDropEvent>(this);
    m_pickupEventConnection = m_dispatcher.sink<PickupDroppedItemEvent>().connect<&DropService::OnPickupEvent>(this);
    m_notifyDropConnection = m_dispatcher.sink<NotifyActorDrop>().connect<&DropService::OnNotifyDrop>(this);
    m_notifyPickupConnection = m_dispatcher.sink<NotifyDroppedItemPickedUp>().connect<&DropService::OnNotifyPickup>(this);
    m_notifyDroppedItemsConnection = m_dispatcher.sink<NotifyDroppedItems>().connect<&DropService::OnNotifyDroppedItems>(this);
    m_connectedEventConnection = m_dispatcher.sink<ConnectedEvent>().connect<&DropService::OnConnected>(this);
    m_cellChangeConnection = m_dispatcher.sink<CellChangeEvent>().connect<&DropService::OnCellChange>(this);
    m_updateConnection = m_dispatcher.sink<UpdateEvent>().connect<&DropService::OnUpdate>(this);

    DropManager::SetStorageListener(&m_dropStorage);
    EnsureStorageReady();
}

DropService::~DropService()
{
    DropManager::SetStorageListener(nullptr);
}

void DropService::OnDropEvent(const DropItemEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    auto serverIdRes = ResolveServerId(acEvent.ActorFormId);
    if (!serverIdRes)
    {
        spdlog::warn("{}: failed to resolve server id for actor {:X}", __FUNCTION__, acEvent.ActorFormId);
        return;
    }

    RequestActorDrop request{};
    request.ServerId = *serverIdRes;
    request.Item = acEvent.Item;
    request.ClientDropId = acEvent.ClientDropId;
    request.HasLocation = true;
    request.Location = ToNetVector(acEvent.Location);
    request.HasRotation = true;
    request.Rotation = ToNetVector(acEvent.Rotation);

    spdlog::info("DropService: requesting drop for actor {:X}, server {:X}, item {:X}:{:X}", acEvent.ActorFormId, *serverIdRes, acEvent.Item.BaseId.ModId, acEvent.Item.BaseId.BaseId);
    m_transport.Send(request);
}

void DropService::OnPickupEvent(const PickupDroppedItemEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    spdlog::info("DropService: local pickup event actor {:X} drop {}", acEvent.ActorFormId, acEvent.DropId);

    auto serverIdRes = ResolveServerId(acEvent.ActorFormId);
    if (!serverIdRes)
    {
        spdlog::warn("{}: failed to resolve server id for actor {:X}", __FUNCTION__, acEvent.ActorFormId);
        return;
    }

    RequestPickupDroppedItem request{};
    request.ServerId = *serverIdRes;
    request.DropId = acEvent.DropId;
    if (const auto dropOpt = DropManager::GetServerDrop(acEvent.DropId); dropOpt)
        request.Item = dropOpt->Item;
    spdlog::info("DropService: requesting pickup for actor {:X} (server {:X}), drop {}", acEvent.ActorFormId, *serverIdRes, acEvent.DropId);
    m_transport.Send(request);
}

void DropService::OnNotifyDrop(const NotifyActorDrop& acMessage) noexcept
{
    if (!ApplyDrop(acMessage))
    {
        PendingAction pending{};
        pending.Type = PendingType::Drop;
        pending.DropMessage = acMessage;
        m_pendingActions.push_back(std::move(pending));
        spdlog::debug("DropService: queued drop {} for later", acMessage.DropId);
    }
    else
    {
        spdlog::info("DropService: processed drop {} immediately", acMessage.DropId);
    }
}

bool DropService::ApplyDrop(const NotifyActorDrop& acMessage) noexcept
{
    Actor* pActor = Utils::GetByServerId<Actor>(acMessage.ServerId);
    if (!pActor)
    {
        spdlog::warn("{}: could not find actor for server id {:X}", __FUNCTION__, acMessage.ServerId);
        return false;
    }

    if (acMessage.HasClientDropId)
    {
        auto localDataOpt = DropManager::ConsumeLocalDrop(acMessage.ClientDropId);
        if (!localDataOpt)
        {
            spdlog::warn("{}: missing local drop data for client drop {}", __FUNCTION__, acMessage.ClientDropId);
            return false;
        }

        DropManager::ServerDropData serverData{};
        serverData.ActorFormId = localDataOpt->ActorFormId;
        serverData.Item = localDataOpt->Item;
        serverData.Location = localDataOpt->Location;
        serverData.Rotation = localDataOpt->Rotation;
        serverData.HandleBits = localDataOpt->HandleBits;
        serverData.CellId = localDataOpt->CellId;
        serverData.WorldSpaceId = localDataOpt->WorldSpaceId;

        DropManager::TrackServerDrop(acMessage.DropId, serverData);
        return true;
    }

    if (!EnsureActorReady(pActor, "drop"))
        return false;

    NiPoint3 location = acMessage.HasLocation ? ToPoint(acMessage.Location) : pActor->position;
    NiPoint3 rotation = acMessage.HasRotation ? ToPoint(acMessage.Rotation) : pActor->rotation;

    DropManager::ServerDropData serverData{};
    serverData.ActorFormId = pActor->formID;
    serverData.Item = acMessage.Item;
    serverData.Location = location;
    serverData.Rotation = rotation;
    serverData.HandleBits = 0;
    auto [cellId, worldId] = ResolveCellMetadata(m_world, pActor);
    serverData.CellId = cellId;
    serverData.WorldSpaceId = worldId;
    DropManager::TrackServerDrop(acMessage.DropId, serverData);

    {
        DropExecution::Scope scope(DropExecution::Mode::RemoteDrop, pActor->formID, acMessage.DropId);
        ScopedInventoryOverride _;
        pActor->DropOrPickUpObject(acMessage.Item, &location, &rotation);
    }

    spdlog::info("DropService: applied remote drop {} for actor {:X}", acMessage.DropId, pActor->formID);

    return true;
}

void DropService::OnNotifyPickup(const NotifyDroppedItemPickedUp& acMessage) noexcept
{
    if (!ApplyPickup(acMessage))
    {
        PendingAction pending{};
        pending.Type = PendingType::Pickup;
        pending.PickupMessage = acMessage;
        m_pendingActions.push_back(std::move(pending));
        spdlog::debug("DropService: queued pickup {} for later", acMessage.DropId);
    }
    else
    {
        spdlog::info("DropService: processed pickup {} immediately", acMessage.DropId);
    }
}

bool DropService::ApplyPickup(const NotifyDroppedItemPickedUp& acMessage) noexcept
{
    Actor* pActor = Utils::GetByServerId<Actor>(acMessage.ServerId);
    if (!pActor)
    {
        DropManager::RemoveServerDrop(acMessage.DropId);
        spdlog::warn("{}: could not find actor {:X} for pickup", __FUNCTION__, acMessage.ServerId);
        return true;
    }

    if (pActor->GetExtension()->IsLocalPlayer())
    {
        DropManager::RemoveServerDrop(acMessage.DropId);
        return true;
    }

    if (!EnsureActorReady(pActor, "pickup"))
        return false;

    const auto handleOpt = DropManager::GetHandleForDrop(acMessage.DropId);
    const int32_t pickupCount = acMessage.Item.Count > 0 ? acMessage.Item.Count : 1;

    DropExecution::Scope scope(DropExecution::Mode::RemotePickup, pActor->formID, acMessage.DropId);

    if (handleOpt)
    {
        if (auto* pDroppedRef = TESObjectREFR::GetByHandle(*handleOpt))
        {
            ScopedInventoryOverride _;
            pActor->PickUpObject(pDroppedRef, pickupCount, false, 0.0f);
        }
        else
        {
            spdlog::debug("{}: drop handle {:X} missing for actor {:X}", __FUNCTION__, *handleOpt, pActor->formID);
            RemoveNearbyReference(acMessage.DropId, "missing reference handle during pickup");
        }
    }
    else
    {
        if (!RemoveNearbyReference(acMessage.DropId, "missing handle for pickup"))
        {
            ScopedInventoryOverride _;
            pActor->DropOrPickUpObject(acMessage.Item, nullptr, nullptr);
        }
    }

    DropManager::RemoveServerDrop(acMessage.DropId);
    spdlog::info("DropService: applied remote pickup {} for actor {:X}", acMessage.DropId, pActor->formID);
    return true;
}

void DropService::OnNotifyDroppedItems(const NotifyDroppedItems& acMessage) noexcept
{
    spdlog::info("DropService: received {} drops from server (request {})", acMessage.Entries.size(), acMessage.RequestId);
    HandleDropSyncResponse(acMessage);
}

void DropService::OnConnected(const ConnectedEvent&) noexcept
{
    EnsureStorageReady();
    RequestFullDropSync();
    RequestCellSync();
}

void DropService::OnCellChange(const CellChangeEvent& acEvent) noexcept
{
    SendDropSyncRequest(false, true, acEvent.CellId, true, acEvent.WorldSpaceId);
}

void DropService::OnUpdate(const UpdateEvent&) noexcept
{
    if (m_pendingActions.empty())
        return;

    TiltedPhoques::Vector<PendingAction> remaining;
    remaining.reserve(m_pendingActions.size());
    for (auto& action : m_pendingActions)
    {
        bool applied = false;
        if (action.Type == PendingType::Drop)
            applied = ApplyDrop(action.DropMessage);
        else
            applied = ApplyPickup(action.PickupMessage);

        if (!applied)
            remaining.push_back(std::move(action));
    }

    m_pendingActions = std::move(remaining);
    if (!m_pendingActions.empty())
        spdlog::debug("DropService: {} pending drop actions remain", m_pendingActions.size());
}

std::optional<uint32_t> DropService::ResolveServerId(uint32_t aFormId) const noexcept
{
    auto view = m_world.view<FormIdComponent>();

    const auto it = std::find_if(std::begin(view), std::end(view), [view, formId = aFormId](auto entity) { return view.get<FormIdComponent>(entity).Id == formId; });
    if (it != std::end(view))
        return Utils::GetServerId(*it);

    if (auto* pPlayer = PlayerCharacter::Get(); pPlayer && pPlayer->formID == aFormId)
    {
        const uint32_t playerId = m_transport.GetLocalPlayerId();
        if (playerId)
            return playerId;
    }

    return std::nullopt;
}

bool DropService::EnsureActorReady(Actor* apActor, const char* apContext) const noexcept
{
    if (!apActor)
        return false;

    if (!apActor->GetNiNode())
    {
        spdlog::debug("{}: actor {:X} missing 3D during {}", __FUNCTION__, apActor->formID, apContext);
        return false;
    }

    if (ExtraContainerChanges::Data* pContainerChanges = apActor->GetContainerChanges();
        !pContainerChanges || !pContainerChanges->entries)
    {
        spdlog::debug("{}: actor {:X} missing container data during {}", __FUNCTION__, apActor->formID, apContext);
        return false;
    }

    return true;
}

bool DropService::EnsureStorageReady() noexcept
{
    std::string username = m_transport.GetLoginUsername();
    if (username.empty())
        username = "default";

    if (username != m_cachedUsername)
    {
        m_cachedUsername = username;
        m_dropStorage.SetActiveUser(username);
    }

    return m_dropStorage.EnsureInitialized();
}

void DropService::RequestFullDropSync() noexcept
{
    SendDropSyncRequest(true, false, {}, false, {});
}

uint32_t DropService::SendDropSyncRequest(bool aRequestAll, bool aHasCellFilter, const GameId& acCellId, bool aHasWorldFilter, const GameId& acWorldId) noexcept
{
    if (!m_transport.IsConnected())
        return 0;

    RequestDroppedItems request{};
    request.RequestId = m_nextDropSyncRequestId++;
    if (request.RequestId == 0)
        request.RequestId = m_nextDropSyncRequestId++;

    request.RequestAll = aRequestAll;
    request.HasCellFilter = aHasCellFilter;
    if (aHasCellFilter)
        request.CellId = acCellId;
    request.HasWorldSpaceFilter = aHasWorldFilter;
    if (aHasWorldFilter)
        request.WorldSpaceId = acWorldId;

    m_transport.Send(request);

    DropSyncContext context{};
    context.IsFullSync = aRequestAll;
    if (aHasCellFilter)
        context.CellId = acCellId;
    if (aHasWorldFilter)
        context.WorldSpaceId = acWorldId;

    if (request.RequestId != 0)
        m_pendingDropSyncs[request.RequestId] = context;

    spdlog::info("DropService: requested drop sync {}, all={}, cell {:X}:{:X}", request.RequestId, aRequestAll, context.CellId.ModId, context.CellId.BaseId);
    return request.RequestId;
}

void DropService::HandleDropSyncResponse(const NotifyDroppedItems& acMessage) noexcept
{
    std::optional<DropSyncContext> context{};
    if (acMessage.RequestId != 0)
    {
        auto it = m_pendingDropSyncs.find(acMessage.RequestId);
        if (it != m_pendingDropSyncs.end())
        {
            context = it->second;
            m_pendingDropSyncs.erase(it);
        }
    }

    TiltedPhoques::Vector<uint64_t> serverDropIds;
    serverDropIds.reserve(acMessage.Entries.size());

    for (const auto& entry : acMessage.Entries)
    {
        const bool forceMaterialize = context && !context->IsFullSync;
        ProcessDropEntry(entry, forceMaterialize);
        serverDropIds.push_back(entry.DropId);
    }

    if (context && !context->IsFullSync)
    {
        ReconcileCachedDrops(context->CellId, context->WorldSpaceId, serverDropIds);
    }
    else if (context && context->IsFullSync)
    {
        TiltedPhoques::Map<uint64_t, bool> liveIds;
        for (auto id : serverDropIds)
            liveIds[id] = true;

        auto cached = m_dropStorage.GetAllDrops();
        for (const auto& entry : cached)
        {
            if (liveIds.find(entry.DropId) != std::end(liveIds))
                continue;

            bool removed = false;
            if (entry.RefFormId)
            {
                if (auto* pForm = TESForm::GetById(entry.RefFormId))
                {
                    if (auto* pRef = Cast<TESObjectREFR>(pForm))
                    {
                        pRef->Delete();
                        removed = true;
                    }
                }
            }

            if (!removed)
            {
                if (TESBoundObject* pObject = ResolveDroppedObject(entry.Item))
                {
                    if (TESObjectREFR* pRef = FindReferenceNear(pObject, entry.Location))
                    {
                        pRef->Delete();
                        removed = true;
                    }
                }
            }

            if (removed)
                spdlog::warn("DropService: removed stale cached drop {} during full sync", entry.DropId);

            m_dropStorage.RemoveCachedDrop(entry.DropId);
        }
    }
}

void DropService::ProcessDropEntry(const NotifyDroppedItems::Entry& acEntry, bool aForceMaterialize) noexcept
{
    DropManager::ServerDropData data{};
    data.ActorFormId = acEntry.ActorFormId;
    data.Item = acEntry.Item;
    data.Location = acEntry.HasLocation ? ToPoint(acEntry.Location) : NiPoint3{};
    data.Rotation = acEntry.HasRotation ? ToPoint(acEntry.Rotation) : NiPoint3{};
    data.HandleBits = 0;
    data.CellId = acEntry.CellId;
    data.WorldSpaceId = acEntry.WorldSpaceId;

    DropManager::TrackServerDrop(acEntry.DropId, data);
    MaterializeDrop(acEntry.DropId, data, aForceMaterialize);
}

void DropService::MaterializeDrop(uint64_t aDropId, const DropManager::ServerDropData& acData, bool aForce) noexcept
{
    if (DropManager::GetHandleForDrop(aDropId))
        return;

    const bool hasLocation = std::abs(acData.Location.x) > std::numeric_limits<float>::epsilon() || std::abs(acData.Location.y) > std::numeric_limits<float>::epsilon() || std::abs(acData.Location.z) > std::numeric_limits<float>::epsilon();
    if (!hasLocation)
        return;

    if (!aForce)
    {
        if (!(GetPlayerCellId() == acData.CellId))
            return;
    }

    if (!SpawnLocalDrop(acData, aDropId))
        spdlog::warn("DropService: failed to materialize drop {} in cell {:X}:{:X}", aDropId, acData.CellId.ModId, acData.CellId.BaseId);
}

bool DropService::SpawnLocalDrop(const DropManager::ServerDropData& acData, uint64_t aDropId) const noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return false;

    NiPoint3 dropLocation = acData.Location;
    NiPoint3 dropRotation = acData.Rotation;

    DropExecution::Scope scope(DropExecution::Mode::RemoteDrop, acData.ActorFormId, aDropId);
    ScopedInventoryOverride _;
    pPlayer->DropOrPickUpObject(acData.Item, &dropLocation, &dropRotation);
    spdlog::info("DropService: spawned local drop {} at ({:.2f}, {:.2f}, {:.2f})", aDropId, dropLocation.x, dropLocation.y, dropLocation.z);
    return true;
}

bool DropService::RemoveNearbyReference(uint64_t aDropId, const char* apReason) noexcept
{
    const auto dropOpt = DropManager::GetServerDrop(aDropId);
    if (!dropOpt)
        return false;

    TESBoundObject* pObject = ResolveDroppedObject(dropOpt->Item);
    if (!pObject)
        return false;

    if (TESObjectREFR* pRef = FindReferenceNear(pObject, dropOpt->Location))
    {
        pRef->Delete();
        spdlog::warn("DropService: {} -> removed fallback drop {} ({:X}:{:X})", apReason ? apReason : "cleanup", aDropId, dropOpt->Item.BaseId.ModId, dropOpt->Item.BaseId.BaseId);
        return true;
    }

    spdlog::debug("DropService: {} fallback failed for drop {}", apReason ? apReason : "cleanup", aDropId);
    return false;
}

void DropService::ReconcileCachedDrops(const GameId& acCellId, const GameId& acWorldId, const TiltedPhoques::Vector<uint64_t>& acAuthoritativeDropIds) noexcept
{
    auto cachedDrops = m_dropStorage.GetDropsForCell(acCellId, acWorldId);
    TiltedPhoques::Map<uint64_t, bool> liveIds;
    for (auto id : acAuthoritativeDropIds)
        liveIds[id] = true;

    for (const auto& cached : cachedDrops)
    {
        if (liveIds.find(cached.DropId) != std::end(liveIds))
            continue;

        bool removed = false;
        if (cached.RefFormId)
        {
            if (auto* pForm = TESForm::GetById(cached.RefFormId))
            {
                if (auto* pRef = Cast<TESObjectREFR>(pForm))
                {
                    pRef->Delete();
                    removed = true;
                }
            }
        }

        if (!removed)
        {
            if (TESBoundObject* pObject = ResolveDroppedObject(cached.Item))
            {
                if (TESObjectREFR* pRef = FindReferenceNear(pObject, cached.Location))
                {
                    pRef->Delete();
                    removed = true;
                }
            }
        }

        if (removed)
            spdlog::warn("DropService: removed stale cached drop {} in cell {:X}:{:X}", cached.DropId, acCellId.ModId, acCellId.BaseId);
        else
            spdlog::warn("DropService: cached drop {} missing locally for cell {:X}:{:X}", cached.DropId, acCellId.ModId, acCellId.BaseId);

        m_dropStorage.RemoveCachedDrop(cached.DropId);
    }
}

GameId DropService::GetPlayerCellId() noexcept
{
    GameId cell{};
    auto* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return cell;

    if (auto* pCell = pPlayer->parentCell)
        m_world.GetModSystem().GetServerModId(pCell->formID, cell);

    return cell;
}

GameId DropService::GetPlayerWorldId() noexcept
{
    GameId world{};
    auto* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return world;

    if (auto* pWorld = pPlayer->GetWorldSpace())
        m_world.GetModSystem().GetServerModId(pWorld->formID, world);

    return world;
}

void DropService::RequestCellSync() noexcept
{
    const GameId cellId = GetPlayerCellId();
    if (!cellId)
        return;

    const GameId worldId = GetPlayerWorldId();
    SendDropSyncRequest(false, true, cellId, static_cast<bool>(worldId), worldId);
}
