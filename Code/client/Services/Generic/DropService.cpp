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
#include <Forms/AlchemyItem.h>
#include <Forms/EnchantmentItem.h>

#include <Utils.h>
#include <ExtraData/ExtraContainerChanges.h>
#include <Games/Primitives.h>

#include <spdlog/spdlog.h>
#include <TiltedCore/Stl.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace
{
constexpr float kDropSearchRadiusSquared = 400.f * 400.f;
constexpr float kPickupRemovalRadiusSquared = 2500.f * 2500.f;
constexpr float kMaterializeGraceSeconds = 0.06f;
TiltedPhoques::Map<uint64_t, float> g_materializeGrace;

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

uint16_t ClampExtraCount(int32_t aCount) noexcept
{
    int32_t count = aCount;
    if (count < 0)
        count = -count;
    if (count <= 0)
        count = 1;

    constexpr int32_t kMaxExtraCount = std::numeric_limits<int16_t>::max();
    if (count > kMaxExtraCount)
        count = kMaxExtraCount;

    return static_cast<uint16_t>(count);
}

void ApplyDropExtraData(TESObjectREFR* apReference, const Inventory::Entry& acItem, uint16_t aCount) noexcept
{
    if (!apReference)
        return;

    ExtraDataList* pExtraDataList = apReference->GetExtraDataList();
    if (!pExtraDataList)
        return;

    pExtraDataList->SetCount(aCount);

    if (acItem.ExtraCharge > 0.f)
        pExtraDataList->SetChargeData(acItem.ExtraCharge);

    if (acItem.ExtraEnchantId != 0)
    {
        auto& modSystem = World::Get().GetModSystem();

        EnchantmentItem* pEnchantment = nullptr;
        if (acItem.ExtraEnchantId.ModId == 0xFFFFFFFF)
        {
            pEnchantment = EnchantmentItem::Create(acItem.EnchantData);
        }
        else
        {
            const uint32_t enchantId = modSystem.GetGameId(acItem.ExtraEnchantId);
            pEnchantment = Cast<EnchantmentItem>(TESForm::GetById(enchantId));
        }

        TP_ASSERT(pEnchantment, "No Enchantment created or found.");
        if (pEnchantment)
            pExtraDataList->SetEnchantmentData(pEnchantment, acItem.ExtraEnchantCharge, acItem.ExtraEnchantRemoveUnequip);
    }

    if (acItem.ExtraPoisonId != 0)
    {
        auto& modSystem = World::Get().GetModSystem();
        const uint32_t poisonId = modSystem.GetGameId(acItem.ExtraPoisonId);
        if (AlchemyItem* pPoison = Cast<AlchemyItem>(TESForm::GetById(poisonId)))
            pExtraDataList->SetPoison(pPoison, acItem.ExtraPoisonCount);
    }

    if (acItem.ExtraHealth > 0.f)
        pExtraDataList->SetHealth(acItem.ExtraHealth);

    if (acItem.ExtraSoulLevel > 0 && acItem.ExtraSoulLevel <= 5)
        pExtraDataList->SetSoulData(static_cast<SOUL_LEVEL>(acItem.ExtraSoulLevel));

    if (acItem.ExtraWorn)
        pExtraDataList->SetWorn(false);
    if (acItem.ExtraWornLeft)
        pExtraDataList->SetWorn(true);
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
    request.ActorFormId = acEvent.ActorFormId;
    request.Item = acEvent.Item;
    request.ClientDropId = acEvent.ClientDropId;
    request.HasLocation = true;
    request.Location = ToNetVector(acEvent.Location);
    request.HasRotation = true;
    request.Rotation = ToNetVector(acEvent.Rotation);
    request.CellId = acEvent.CellId;
    request.WorldSpaceId = acEvent.WorldSpaceId;
    request.ReferenceId = acEvent.ReferenceId;

    spdlog::info("DropService: requesting drop for actor {:X}, server {:X}, item {:X}:{:X}", acEvent.ActorFormId, *serverIdRes, acEvent.Item.BaseId.ModId, acEvent.Item.BaseId.BaseId);
    m_transport.Send(request);

    // Client drop payload is captured in the request; mapping cache is server-authoritative.
    DropManager::ConsumeLocalDrop(acEvent.ClientDropId);
}

void DropService::OnPickupEvent(const PickupDroppedItemEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    uint64_t resolvedDropId = 0;
    if (acEvent.ReferenceFormId)
    {
        EnsureStorageReady();
        if (const auto mapped = m_dropStorage.FindDropIdByRefFormId(acEvent.ReferenceFormId, acEvent.CellId, acEvent.WorldSpaceId); mapped)
        {
            resolvedDropId = *mapped;
            spdlog::debug("DropService: resolved pickup ref {:X} to drop {} via cache", acEvent.ReferenceFormId, resolvedDropId);
        }
    }
    if (resolvedDropId == 0)
        resolvedDropId = acEvent.DropId;

    spdlog::info("DropService: local pickup event actor {:X} drop {}", acEvent.ActorFormId, resolvedDropId);

    auto serverIdRes = ResolveServerId(acEvent.ActorFormId);
    if (!serverIdRes)
    {
        spdlog::warn("{}: failed to resolve server id for actor {:X}", __FUNCTION__, acEvent.ActorFormId);
        return;
    }

    RequestPickupDroppedItem request{};
    request.ServerId = *serverIdRes;
    request.DropId = resolvedDropId;
    if (resolvedDropId)
    {
        if (const auto dropOpt = DropManager::GetServerDrop(resolvedDropId); dropOpt)
        {
            request.Item = dropOpt->Item;
            request.HasLocation = true;
            request.Location = ToNetVector(dropOpt->Location);
            request.HasRotation = true;
            request.Rotation = ToNetVector(dropOpt->Rotation);
            request.CellId = dropOpt->CellId;
            request.WorldSpaceId = dropOpt->WorldSpaceId;
            request.ReferenceId = dropOpt->ReferenceId ? dropOpt->ReferenceId : acEvent.ReferenceId;
        }
        else if (acEvent.HasItemData)
        {
            request.Item = acEvent.Item;
            request.HasLocation = acEvent.HasLocation;
            if (acEvent.HasLocation)
                request.Location = ToNetVector(acEvent.Location);
            request.HasRotation = acEvent.HasRotation;
            if (acEvent.HasRotation)
                request.Rotation = ToNetVector(acEvent.Rotation);
            request.CellId = acEvent.CellId;
            request.WorldSpaceId = acEvent.WorldSpaceId;
            request.ReferenceId = acEvent.ReferenceId;
        }
    }
    else
    {
        if (acEvent.HasItemData)
            request.Item = acEvent.Item;

        request.HasLocation = acEvent.HasLocation;
        if (acEvent.HasLocation)
            request.Location = ToNetVector(acEvent.Location);

        request.HasRotation = acEvent.HasRotation;
        if (acEvent.HasRotation)
            request.Rotation = ToNetVector(acEvent.Rotation);

        request.CellId = acEvent.CellId;
        request.WorldSpaceId = acEvent.WorldSpaceId;
        request.ReferenceId = acEvent.ReferenceId;
    }

    spdlog::info("DropService: requesting pickup for actor {:X} (server {:X}), drop {}", acEvent.ActorFormId, *serverIdRes, resolvedDropId);
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
    if (const auto itEpoch = m_knownSpawnEpochs.find(acMessage.DropId);
        itEpoch != std::end(m_knownSpawnEpochs) && acMessage.SpawnEpoch < itEpoch->second)
    {
        spdlog::debug("DropService: ignoring stale drop {} epoch {} (known {})", acMessage.DropId, acMessage.SpawnEpoch, itEpoch->second);
        return true;
    }

    if (const auto itEpoch = m_knownSpawnEpochs.find(acMessage.DropId);
        itEpoch == std::end(m_knownSpawnEpochs) || acMessage.SpawnEpoch > itEpoch->second)
    {
        m_knownSpawnEpochs[acMessage.DropId] = acMessage.SpawnEpoch;
    }

    Actor* pActor = Utils::GetByServerId<Actor>(acMessage.ServerId);
    PlayerCharacter* pPlayer = PlayerCharacter::Get();

    DropManager::ServerDropData serverData{};
    serverData.ServerId = acMessage.ServerId;
    serverData.ActorFormId = acMessage.ActorFormId ? acMessage.ActorFormId : (pActor ? pActor->formID : 0);
    serverData.CellId = acMessage.CellId;
    serverData.WorldSpaceId = acMessage.WorldSpaceId;
    serverData.ReferenceId = acMessage.ReferenceId;
    serverData.Item = acMessage.Item;

    auto [fallbackCellId, fallbackWorldId] = ResolveCellMetadata(m_world, pActor ? pActor : static_cast<Actor*>(pPlayer));
    if (!serverData.CellId)
        serverData.CellId = fallbackCellId;
    if (!serverData.WorldSpaceId)
        serverData.WorldSpaceId = fallbackWorldId;

    if (acMessage.HasLocation)
        serverData.Location = ToPoint(acMessage.Location);
    else if (pActor)
        serverData.Location = pActor->position;
    else if (pPlayer)
        serverData.Location = pPlayer->position;

    if (acMessage.HasRotation)
        serverData.Rotation = ToPoint(acMessage.Rotation);
    else if (pActor)
        serverData.Rotation = pActor->rotation;
    else if (pPlayer)
        serverData.Rotation = pPlayer->rotation;

    DropManager::TrackServerDrop(acMessage.DropId, serverData);

    if (const auto handleOpt = DropManager::GetHandleForDrop(acMessage.DropId); handleOpt)
    {
        if (TESObjectREFR::GetByHandle(*handleOpt))
        {
            m_localDrops.insert(acMessage.DropId);
            spdlog::debug("DropService: drop {} already materialized, skipping spawn", acMessage.DropId);
            return true;
        }

        DropManager::ClearHandleBinding(acMessage.DropId);
    }

    if (TryBindExistingReference(acMessage.DropId, serverData))
    {
        spdlog::debug("DropService: bound existing reference for drop {}, skipping spawn", acMessage.DropId);
        return true;
    }

    const GameId playerCell = GetPlayerCellId();
    if (playerCell && serverData.CellId && playerCell != serverData.CellId)
    {
        spdlog::debug("DropService: drop {} in other cell {:X}:{:X}, skipping spawn", acMessage.DropId, serverData.CellId.ModId, serverData.CellId.BaseId);
        return true;
    }

    if (!MaterializeDrop(acMessage.DropId, serverData, true))
        return false;

    m_localDrops.insert(acMessage.DropId);
    spdlog::info("DropService: applied drop {} for actor {:X}", acMessage.DropId, serverData.ActorFormId);
    return true;
}

void DropService::OnNotifyPickup(const NotifyDroppedItemPickedUp& acMessage) noexcept
{
    spdlog::info("DropService: received pickup notify drop {} from actor {:X}", acMessage.DropId, acMessage.ServerId);
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
    if (acMessage.DropId == 0)
        return HandleUntrackedPickup(acMessage);

    EnsureStorageReady();

    const uint64_t dropId = acMessage.DropId;
    const uint32_t localPlayerId = m_transport.GetLocalPlayerId();
    const bool isLocalPicker = localPlayerId != 0 && acMessage.ServerId == localPlayerId;

    if (!IsPickupRelevant(acMessage) && !isLocalPicker)
    {
        DropManager::RemoveServerDrop(dropId);
        ForgetLocalDrop(dropId);
        spdlog::debug("DropService: ignored pickup {} (out of range)", dropId);
        return true;
    }

    bool removed = false;

    if (const auto handleOpt = DropManager::GetHandleForDrop(dropId); handleOpt)
    {
        if (TESObjectREFR* pDroppedRef = TESObjectREFR::GetByHandle(*handleOpt))
        {
            if (pDroppedRef->IsTemporary())
                pDroppedRef->Delete();
            else
                pDroppedRef->Disable();
            removed = true;
        }
        else
        {
            DropManager::ClearHandleBinding(dropId);
        }
    }

    if (!removed)
    {
        if (const auto refFormId = m_dropStorage.GetRefFormId(dropId); refFormId)
        {
            if (TESForm* pForm = TESForm::GetById(*refFormId))
            {
                if (TESObjectREFR* pRef = Cast<TESObjectREFR>(pForm))
                {
                    if (pRef->IsTemporary())
                        pRef->Delete();
                    else
                        pRef->Disable();
                    removed = true;
                }
            }
        }
    }

    if (!removed && acMessage.HasLocation)
        removed = RemoveReferenceByLocation(acMessage.Item, acMessage.Location, "pickup notify location fallback", kPickupRemovalRadiusSquared);
    if (!removed)
        removed = RemoveNearbyReference(dropId, "pickup notify nearby fallback", kPickupRemovalRadiusSquared);

    DropManager::RemoveServerDrop(dropId);
    ForgetLocalDrop(dropId);

    if (isLocalPicker || removed)
        m_dropStorage.RemoveCachedDrop(dropId);

    spdlog::info("DropService: applied pickup {} (removed={}, localPicker={})", dropId, removed, isLocalPicker);
    return true;
}

bool DropService::HandleUntrackedPickup(const NotifyDroppedItemPickedUp& acMessage) noexcept
{
    const GameId playerCell = GetPlayerCellId();
    const GameId playerWorld = GetPlayerWorldId();

    if (acMessage.CellId && playerCell && acMessage.CellId != playerCell)
    {
        spdlog::debug("DropService: ignoring pickup in cell {:X}:{:X}, player cell {:X}:{:X}", acMessage.CellId.ModId, acMessage.CellId.BaseId, playerCell.ModId, playerCell.BaseId);
        return true;
    }

    if (acMessage.WorldSpaceId && playerWorld && acMessage.WorldSpaceId != playerWorld)
    {
        spdlog::debug("DropService: ignoring pickup in world {:X}:{:X}, player world {:X}:{:X}", acMessage.WorldSpaceId.ModId, acMessage.WorldSpaceId.BaseId, playerWorld.ModId, playerWorld.BaseId);
        return true;
    }

    bool removed = RemoveReferenceById(acMessage.ReferenceId, "untracked pickup via reference id");
    if (!removed && acMessage.HasLocation)
        removed = RemoveReferenceByLocation(acMessage.Item, acMessage.Location, "untracked pickup via location", kPickupRemovalRadiusSquared);
    if (!removed)
    {
        if (auto* pPlayer = PlayerCharacter::Get())
        {
            Vector3_NetQuantize approximate = ToNetVector(pPlayer->position);
            removed = RemoveReferenceByLocation(acMessage.Item, approximate, "untracked pickup player proximity", kPickupRemovalRadiusSquared);
        }
    }

    if (!removed)
        spdlog::warn("DropService: pickup notify without drop id could not find reference ({:X}:{:X})", acMessage.Item.BaseId.ModId, acMessage.Item.BaseId.BaseId);

    return true;
}

bool DropService::IsPickupRelevant(const NotifyDroppedItemPickedUp& acMessage) noexcept
{
    const GameId playerCell = GetPlayerCellId();
    const GameId playerWorld = GetPlayerWorldId();

    if (acMessage.CellId && playerCell && acMessage.CellId != playerCell)
        return false;

    if (acMessage.WorldSpaceId && playerWorld && acMessage.WorldSpaceId != playerWorld)
        return false;

    return true;
}

void DropService::OnNotifyDroppedItems(const NotifyDroppedItems& acMessage) noexcept
{
    spdlog::info("DropService: received {} drops and {} creation-engine pickups from server (request {})", acMessage.Entries.size(), acMessage.CreationEnginePickedUpReferences.size(), acMessage.RequestId);
    HandleDropSyncResponse(acMessage);
}

void DropService::OnConnected(const ConnectedEvent&) noexcept
{
    EnsureStorageReady();
    m_pendingCreationEngineRemovals.clear();
    RequestCellSync();
}

void DropService::OnCellChange(const CellChangeEvent& acEvent) noexcept
{
    m_pendingCreationEngineRemovals.clear();
    SendDropSyncRequest(false, true, acEvent.CellId, true, acEvent.WorldSpaceId);
}

void DropService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    // Decrease grace timers for materialization gating
    if (!g_materializeGrace.empty())
    {
        const float delta = static_cast<float>(acEvent.Delta);
        TiltedPhoques::Vector<uint64_t> toErase;
        toErase.reserve(g_materializeGrace.size());
        for (const auto& kv : g_materializeGrace)
        {
            const uint64_t id = kv.first;
            const float newValue = kv.second - delta;
            if (newValue <= 0.f)
            {
                toErase.push_back(id);
            }
            else
            {
                g_materializeGrace[id] = newValue;
            }
        }
        for (const auto id : toErase)
        {
            g_materializeGrace.erase(id);
        }
    }

    ProcessPendingCreationEngineRemovals();

    if (m_pendingActions.empty())
        return;

    TiltedPhoques::Vector<PendingAction> remaining;
    remaining.reserve(m_pendingActions.size());
    for (auto& action : m_pendingActions)
    {
        bool applied = false;
        if (action.Type == PendingType::Drop)
        {
            // Respect grace wait for materialization
            auto it = g_materializeGrace.find(action.DropMessage.DropId);
            if (it != g_materializeGrace.end() && it->second > 0.f)
            {
                remaining.push_back(std::move(action));
                continue;
            }

            applied = ApplyDrop(action.DropMessage);
            if (!applied)
                ++action.RetryCounter;
        }
        else
        {
            applied = ApplyPickup(action.PickupMessage);
            if (!applied)
                ++action.RetryCounter;
        }

        if (!applied && action.RetryCounter < 10)
            remaining.push_back(std::move(action));
        else if (!applied)
            spdlog::warn("DropService: dropping pending {} after {} retries", action.Type == PendingType::Drop ? "drop" : "pickup", action.RetryCounter);
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
        auto& knownEpoch = m_knownSpawnEpochs[entry.DropId];
        if (entry.SpawnEpoch > knownEpoch)
            knownEpoch = entry.SpawnEpoch;
        ProcessDropEntry(entry, forceMaterialize);
        serverDropIds.push_back(entry.DropId);
    }

    if (context && !context->IsFullSync)
    {
        ReconcileCachedDrops(context->CellId, context->WorldSpaceId, serverDropIds);
        ApplyCreationEngineCellSync(*context, acMessage.CreationEnginePickedUpReferences);
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
            if (entry.ReferenceId)
                removed = RemoveReferenceById(entry.ReferenceId, "full sync stale reference");
            if (entry.RefFormId && !removed)
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
    data.ServerId = acEntry.ServerId;
    data.ActorFormId = acEntry.ActorFormId;
    data.Item = acEntry.Item;
    data.Location = acEntry.HasLocation ? ToPoint(acEntry.Location) : NiPoint3{};
    data.Rotation = acEntry.HasRotation ? ToPoint(acEntry.Rotation) : NiPoint3{};
    data.HandleBits = 0;
    data.CellId = acEntry.CellId;
    data.WorldSpaceId = acEntry.WorldSpaceId;
    data.ReferenceId = acEntry.ReferenceId;

    DropManager::TrackServerDrop(acEntry.DropId, data);

    if (const auto handleOpt = DropManager::GetHandleForDrop(acEntry.DropId); handleOpt && TESObjectREFR::GetByHandle(*handleOpt))
    {
        m_localDrops.insert(acEntry.DropId);
        spdlog::debug("DropService: skip drop {} from sync (already present locally)", acEntry.DropId);
        return;
    }

    if (!MaterializeDrop(acEntry.DropId, data, aForceMaterialize))
    {
        if (!aForceMaterialize && g_materializeGrace.find(acEntry.DropId) == std::end(g_materializeGrace))
            g_materializeGrace[acEntry.DropId] = kMaterializeGraceSeconds;

        PendingAction pending{};
        pending.Type = PendingType::Drop;
        pending.DropMessage.DropId = acEntry.DropId;
        pending.DropMessage.ServerId = acEntry.ServerId;
        pending.DropMessage.ActorFormId = acEntry.ActorFormId;
        pending.DropMessage.Item = acEntry.Item;
        pending.DropMessage.HasLocation = acEntry.HasLocation;
        if (acEntry.HasLocation)
            pending.DropMessage.Location = acEntry.Location;
        pending.DropMessage.HasRotation = acEntry.HasRotation;
        if (acEntry.HasRotation)
            pending.DropMessage.Rotation = acEntry.Rotation;
        pending.DropMessage.CellId = acEntry.CellId;
        pending.DropMessage.WorldSpaceId = acEntry.WorldSpaceId;
        pending.DropMessage.SpawnEpoch = acEntry.SpawnEpoch;
        m_pendingActions.push_back(std::move(pending));
        spdlog::debug("DropService: queued drop {} for delayed materialization", acEntry.DropId);
    }
}

bool DropService::MaterializeDrop(uint64_t aDropId, const DropManager::ServerDropData& acData, bool aForce) noexcept
{
    if (const auto handleOpt = DropManager::GetHandleForDrop(aDropId); handleOpt)
    {
        if (TESObjectREFR::GetByHandle(*handleOpt))
            return true;

        spdlog::debug("DropService: stale handle {:X} for drop {}, clearing and re-materializing", *handleOpt, aDropId);
        DropManager::ClearHandleBinding(aDropId);
        m_localDrops.erase(aDropId);
    }

    if (TryBindExistingReference(aDropId, acData))
        return true;

    if (m_materializingDrops.find(aDropId) != std::end(m_materializingDrops))
    {
        spdlog::debug("DropService: drop {} already materializing, skipping duplicate spawn", aDropId);
        return true;
    }

    const bool hasLocation = std::abs(acData.Location.x) > std::numeric_limits<float>::epsilon() || std::abs(acData.Location.y) > std::numeric_limits<float>::epsilon() || std::abs(acData.Location.z) > std::numeric_limits<float>::epsilon();
    if (!hasLocation)
        return false;

    if (!(GetPlayerCellId() == acData.CellId))
        return false;

    if (!aForce)
    {
        auto itTimer = g_materializeGrace.find(aDropId);
        if (itTimer == std::end(g_materializeGrace))
        {
            g_materializeGrace[aDropId] = kMaterializeGraceSeconds;

            PendingAction pending{};
            pending.Type = PendingType::Drop;
            pending.DropMessage.DropId = aDropId;
            pending.DropMessage.ServerId = acData.ServerId;
            pending.DropMessage.ActorFormId = acData.ActorFormId;
            pending.DropMessage.Item = acData.Item;
            pending.DropMessage.HasLocation = true;
            pending.DropMessage.Location = ToNetVector(acData.Location);
            pending.DropMessage.HasRotation = true;
            pending.DropMessage.Rotation = ToNetVector(acData.Rotation);
            pending.DropMessage.CellId = acData.CellId;
            pending.DropMessage.WorldSpaceId = acData.WorldSpaceId;
            pending.DropMessage.ReferenceId = acData.ReferenceId;
            pending.DropMessage.SpawnEpoch = (m_knownSpawnEpochs.find(aDropId) != std::end(m_knownSpawnEpochs)) ? m_knownSpawnEpochs[aDropId] : 0;
            m_pendingActions.push_back(std::move(pending));

            spdlog::debug("DropService: scheduled grace wait before spawning drop {}", aDropId);
            return false;
        }

        if (itTimer->second > 0.f)
            return false;
    }

    m_materializingDrops.insert(aDropId);
    const bool spawned = SpawnLocalDrop(acData, aDropId);
    m_materializingDrops.erase(aDropId);

    if (!spawned)
    {
        spdlog::warn("DropService: failed to materialize drop {} in cell {:X}:{:X}", aDropId, acData.CellId.ModId, acData.CellId.BaseId);
        return false;
    }

    m_localDrops.insert(aDropId);
    return true;
}

bool DropService::SpawnLocalDrop(const DropManager::ServerDropData& acData, uint64_t aDropId) const noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return false;

    if (!EnsureActorReady(pPlayer, "materialize drop"))
        return false;

    TESBoundObject* pObject = ResolveDroppedObject(acData.Item);
    if (!pObject)
        return false;

    TESObjectCELL* pCell = pPlayer->parentCell ? pPlayer->parentCell : pPlayer->GetParentCell();
    if (!pCell)
        return false;

    TESWorldSpace* pWorldSpace = pPlayer->GetWorldSpace();
    NiPoint3 dropLocation = acData.Location;
    NiPoint3 dropRotation = acData.Rotation;

    const uint32_t handleBits = ModManager::Get()->SpawnReference(pObject, dropLocation, dropRotation, pCell, pWorldSpace, nullptr, false);
    if (handleBits == 0)
        return false;

    TESObjectREFR* pReference = TESObjectREFR::GetByHandle(handleBits);
    if (!pReference)
        return false;

    Inventory::Entry extraEntry = acData.Item;
    extraEntry.ExtraWorn = false;
    extraEntry.ExtraWornLeft = false;
    const uint16_t dropCount = ClampExtraCount(extraEntry.Count);
    ApplyDropExtraData(pReference, extraEntry, dropCount);

    DropManager::BindHandleToServerDrop(aDropId, acData.ActorFormId, handleBits);

    spdlog::info("DropService: spawned local drop {} at ({:.2f}, {:.2f}, {:.2f}) handle {:X}", aDropId, dropLocation.x, dropLocation.y, dropLocation.z, handleBits);
    return true;
}

bool DropService::RemoveNearbyReference(uint64_t aDropId, const char* apReason, float aRadiusSq) noexcept
{
    const auto dropOpt = DropManager::GetServerDrop(aDropId);
    if (!dropOpt)
        return false;

    TESBoundObject* pObject = ResolveDroppedObject(dropOpt->Item);
    if (!pObject)
        return false;

    if (TESObjectREFR* pRef = FindReferenceNear(pObject, dropOpt->Location, aRadiusSq))
    {
        if (pRef->IsTemporary())
            pRef->Delete();
        else
            pRef->Disable();
        spdlog::warn("DropService: {} -> removed fallback drop {} ({:X}:{:X})", apReason ? apReason : "cleanup", aDropId, dropOpt->Item.BaseId.ModId, dropOpt->Item.BaseId.BaseId);
        return true;
    }

    spdlog::debug("DropService: {} fallback failed for drop {}", apReason ? apReason : "cleanup", aDropId);
    return false;
}

TESObjectREFR* DropService::GetReferenceById(const GameId& acReferenceId) noexcept
{
    if (!acReferenceId)
        return nullptr;

    auto& modSystem = m_world.GetModSystem();
    const uint32_t formId = modSystem.GetGameId(acReferenceId);
    if (!formId)
        return nullptr;

    TESForm* pForm = TESForm::GetById(formId);
    return Cast<TESObjectREFR>(pForm);
}

bool DropService::RemoveReferenceById(const GameId& acReferenceId, const char* apReason) noexcept
{
    TESObjectREFR* pReference = GetReferenceById(acReferenceId);
    if (!pReference)
        return false;

    if (pReference->IsTemporary())
        pReference->Delete();
    else
        pReference->Disable();
    spdlog::info("DropService: {} -> removed reference {:X}:{:X}", apReason ? apReason : "cleanup", acReferenceId.ModId, acReferenceId.BaseId);
    return true;
}

bool DropService::RemoveReferenceByLocation(const Inventory::Entry& acItem, const Vector3_NetQuantize& acLocation, const char* apReason, float aRadiusSq) noexcept
{
    TESBoundObject* pObject = ResolveDroppedObject(acItem);
    if (!pObject)
        return false;

    const NiPoint3 location = ToPoint(acLocation);
    if (TESObjectREFR* pRef = FindReferenceNear(pObject, location, aRadiusSq))
    {
        if (pRef->IsTemporary())
            pRef->Delete();
        else
            pRef->Disable();
        spdlog::info("DropService: {} -> removed reference near ({:.2f}, {:.2f}, {:.2f}) for item {:X}:{:X}", apReason ? apReason : "cleanup", location.x, location.y, location.z, acItem.BaseId.ModId,
                     acItem.BaseId.BaseId);
        return true;
    }

    spdlog::debug("DropService: {} failed to remove reference by location for item {:X}:{:X}", apReason ? apReason : "cleanup", acItem.BaseId.ModId, acItem.BaseId.BaseId);
    return false;
}

bool DropService::TryBindExistingReference(uint64_t aDropId, const DropManager::ServerDropData& acData) noexcept
{
    auto tryBind = [&](TESObjectREFR* apReference) -> bool {
        if (!apReference)
            return false;

        auto handle = apReference->GetHandle();
        if (!handle || handle.handle.iBits == 0)
            return false;

        if (!DropManager::BindHandleToServerDrop(aDropId, acData.ActorFormId, handle.handle.iBits))
            return false;

        GameId referenceId = acData.ReferenceId;
        if (!referenceId)
        {
            auto& modSystem = m_world.GetModSystem();
            modSystem.GetServerModId(apReference->formID, referenceId);
        }

        if (referenceId)
            DropManager::SetReferenceForDrop(aDropId, referenceId);

        m_localDrops.insert(aDropId);

        spdlog::debug("DropService: rebound existing reference {:X}:{:X} for drop {}", referenceId.ModId, referenceId.BaseId, aDropId);
        return true;
    };

    EnsureStorageReady();

    const auto cachedRefFormId = m_dropStorage.GetRefFormId(aDropId);
    TESBoundObject* pObject = ResolveDroppedObject(acData.Item);

    if (cachedRefFormId)
    {
        if (TESForm* pForm = TESForm::GetById(*cachedRefFormId))
        {
            if (TESObjectREFR* pRef = Cast<TESObjectREFR>(pForm))
            {
                if (pObject && pRef->baseForm != pObject)
                {
                    spdlog::warn("DropService: cached ref {:X} base mismatch for drop {}, ignoring", *cachedRefFormId, aDropId);
                }
                else if (tryBind(pRef))
                {
                    return true;
                }
            }
        }
    }

    if (acData.ReferenceId)
    {
        if (TESObjectREFR* pRef = GetReferenceById(acData.ReferenceId))
        {
            if (tryBind(pRef))
                return true;
        }
    }

    if (!pObject)
        return false;

    if (TESObjectREFR* pNearby = FindReferenceNear(pObject, acData.Location, kDropSearchRadiusSquared))
        return tryBind(pNearby);

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
        if (cached.ReferenceId)
            removed = RemoveReferenceById(cached.ReferenceId, "cell sync stale reference id");
        if (cached.RefFormId && !removed)
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

void DropService::ApplyCreationEngineCellSync(const DropSyncContext& acContext, const TiltedPhoques::Vector<GameId>& acPickedUpRefs) noexcept
{
    if (acPickedUpRefs.empty())
        return;

    const GameId playerCell = GetPlayerCellId();
    const GameId playerWorld = GetPlayerWorldId();

    if (acContext.CellId && playerCell && acContext.CellId != playerCell)
        return;

    if (acContext.WorldSpaceId && playerWorld && acContext.WorldSpaceId != playerWorld)
        return;

    for (const auto& refId : acPickedUpRefs)
    {
        if (!refId)
            continue;

        if (RemoveReferenceById(refId, "cell sync creation-engine pickup"))
        {
            m_pendingCreationEngineRemovals.erase(refId);
            continue;
        }

        auto& pending = m_pendingCreationEngineRemovals[refId];
        pending.CellId = acContext.CellId;
        pending.WorldSpaceId = acContext.WorldSpaceId;
        pending.RemainingRetries = 30;
    }
}

void DropService::ProcessPendingCreationEngineRemovals() noexcept
{
    if (m_pendingCreationEngineRemovals.empty())
        return;

    const GameId playerCell = GetPlayerCellId();
    const GameId playerWorld = GetPlayerWorldId();

    TiltedPhoques::Vector<GameId> toErase;
    toErase.reserve(m_pendingCreationEngineRemovals.size());

    for (auto& [refId, pending] : m_pendingCreationEngineRemovals)
    {
        if (!refId || pending.RemainingRetries == 0)
        {
            toErase.push_back(refId);
            continue;
        }

        if (pending.CellId && playerCell && pending.CellId != playerCell)
            continue;

        if (pending.WorldSpaceId && playerWorld && pending.WorldSpaceId != playerWorld)
            continue;

        if (RemoveReferenceById(refId, "cell sync deferred creation-engine pickup"))
        {
            toErase.push_back(refId);
            continue;
        }

        --pending.RemainingRetries;
        if (pending.RemainingRetries == 0)
            toErase.push_back(refId);
    }

    for (const auto& refId : toErase)
        m_pendingCreationEngineRemovals.erase(refId);
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

void DropService::ForgetLocalDrop(uint64_t aDropId) noexcept
{
    if (aDropId == 0)
        return;

    m_localDrops.erase(aDropId);
}
