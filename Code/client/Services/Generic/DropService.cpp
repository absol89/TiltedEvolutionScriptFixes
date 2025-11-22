#include "DropService.h"

#include <World.h>
#include <Components.h>
#include <Services/TransportService.h>
#include <Services/RunnerService.h>
#include <Events/UpdateEvent.h>
#include <Events/DropItemEvent.h>
#include <Events/PickupDroppedItemEvent.h>
#include <Sync/DropManager.h>
#include <Sync/DropExecutionContext.h>
#include <Games/Overrides.h>
#include <Actor.h>
#include <Games/ActorExtension.h>
#include <TESObjectREFR.h>
#include <Utils.h>
#include <ExtraData/ExtraContainerChanges.h>
#include <Games/Primitives.h>

#include <algorithm>

#include <spdlog/spdlog.h>

namespace
{
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

    m_transport.Send(request);
}

void DropService::OnPickupEvent(const PickupDroppedItemEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    auto serverIdRes = ResolveServerId(acEvent.ActorFormId);
    if (!serverIdRes)
    {
        spdlog::warn("{}: failed to resolve server id for actor {:X}", __FUNCTION__, acEvent.ActorFormId);
        return;
    }

    RequestPickupDroppedItem request{};
    request.ServerId = *serverIdRes;
    request.DropId = acEvent.DropId;
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
    DropManager::TrackServerDrop(acMessage.DropId, serverData);

    {
        DropExecution::Scope scope(DropExecution::Mode::RemoteDrop, pActor->formID, acMessage.DropId);
        ScopedInventoryOverride _;
        pActor->DropOrPickUpObject(acMessage.Item, &location, &rotation);
    }

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
        }
    }
    else
    {
        ScopedInventoryOverride _;
        pActor->DropOrPickUpObject(acMessage.Item, nullptr, nullptr);
    }

    DropManager::RemoveServerDrop(acMessage.DropId);
    return true;
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
}

std::optional<uint32_t> DropService::ResolveServerId(uint32_t aFormId) const noexcept
{
    auto view = m_world.view<FormIdComponent>();

    const auto it = std::find_if(std::begin(view), std::end(view), [view, formId = aFormId](auto entity) { return view.get<FormIdComponent>(entity).Id == formId; });
    if (it == std::end(view))
        return std::nullopt;

    return Utils::GetServerId(*it);
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
