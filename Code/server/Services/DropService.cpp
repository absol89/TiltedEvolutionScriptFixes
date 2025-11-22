#include "DropService.h"

#include <World.h>
#include <Components.h>
#include <GameServer.h>
#include <Messages/NotifyActorDrop.h>
#include <Messages/NotifyDroppedItemPickedUp.h>
#include <Setting.h>

namespace
{
Console::Setting bEnableItemDrops{"Gameplay:bEnableItemDrops", "(Experimental) Syncs dropped items by players", true};
}

DropService::DropService(World& aWorld, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
{
    m_requestDropConnection = aDispatcher.sink<PacketEvent<RequestActorDrop>>().connect<&DropService::OnDropRequest>(this);
    m_requestPickupConnection = aDispatcher.sink<PacketEvent<RequestPickupDroppedItem>>().connect<&DropService::OnPickupRequest>(this);
}

void DropService::OnDropRequest(const PacketEvent<RequestActorDrop>& acMessage) noexcept
{
    if (!bEnableItemDrops)
        return;

    const auto& message = acMessage.Packet;

    const auto entity = m_world.TryResolveEntity(message.ServerId);
    if (!entity)
    {
        spdlog::warn("Drop requested for unknown entity {:X}", message.ServerId);
        return;
    }

    auto view = m_world.view<InventoryComponent, OwnerComponent>();
    const auto it = view.find(*entity);
    if (it == view.end())
    {
        spdlog::warn("Drop requested for entity {:X} without InventoryComponent", message.ServerId);
        return;
    }

    auto& ownerComponent = view.get<OwnerComponent>(*it);
    if (ownerComponent.GetOwner() != acMessage.pPlayer)
    {
        spdlog::warn("Drop denied for {:X}: player {:X} not owner", message.ServerId, acMessage.pPlayer->GetConnectionId());
        return;
    }

    auto& inventoryComponent = view.get<InventoryComponent>(*it);
    inventoryComponent.Content.AddOrRemoveEntry(message.Item);

    ActiveDrop drop{};
    drop.DropId = m_nextDropId++;
    drop.ServerId = message.ServerId;
    drop.DropEntry = message.Item;
    drop.PickupEntry = message.Item;
    if (drop.PickupEntry.Count < 0)
        drop.PickupEntry.Count = -drop.PickupEntry.Count;
    drop.HasLocation = message.HasLocation;
    drop.Location = message.Location;
    drop.HasRotation = message.HasRotation;
    drop.Rotation = message.Rotation;

    m_activeDrops[drop.DropId] = drop;

    NotifyActorDrop notify{};
    notify.ServerId = message.ServerId;
    notify.Item = drop.DropEntry;
    notify.DropId = drop.DropId;
    if (message.HasLocation)
    {
        notify.HasLocation = true;
        notify.Location = message.Location;
    }
    if (message.HasRotation)
    {
        notify.HasRotation = true;
        notify.Rotation = message.Rotation;
    }

    if (message.ClientDropId)
    {
        NotifyActorDrop selfNotify = notify;
        selfNotify.HasClientDropId = true;
        selfNotify.ClientDropId = message.ClientDropId;
        acMessage.pPlayer->Send(selfNotify);
    }

    if (!GameServer::Get()->SendToPlayersInRange(notify, *entity, acMessage.GetSender()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}

void DropService::OnPickupRequest(const PacketEvent<RequestPickupDroppedItem>& acMessage) noexcept
{
    if (!bEnableItemDrops)
        return;

    const auto& message = acMessage.Packet;
    const auto dropIt = m_activeDrops.find(message.DropId);
    if (dropIt == m_activeDrops.end())
    {
        spdlog::debug("Pickup requested for unknown drop id {}", message.DropId);
        return;
    }

    ActiveDrop drop = dropIt->second;
    if (drop.ServerId != message.ServerId)
    {
        spdlog::warn("Pickup requested for mismatched actor {:X} drop {}", message.ServerId, message.DropId);
        return;
    }

    const auto entity = m_world.TryResolveEntity(drop.ServerId);
    if (!entity)
    {
        m_activeDrops.erase(dropIt);
        spdlog::warn("Pickup requested for missing entity {:X}", drop.ServerId);
        return;
    }

    auto view = m_world.view<InventoryComponent, OwnerComponent>();
    const auto it = view.find(*entity);
    if (it == view.end())
    {
        spdlog::warn("Pickup requested for entity {:X} without InventoryComponent", drop.ServerId);
        return;
    }

    auto& ownerComponent = view.get<OwnerComponent>(*it);
    if (ownerComponent.GetOwner() != acMessage.pPlayer)
    {
        spdlog::warn("Pickup denied for {:X}: player {:X} not owner", drop.ServerId, acMessage.pPlayer->GetConnectionId());
        return;
    }

    auto& inventoryComponent = view.get<InventoryComponent>(*it);
    inventoryComponent.Content.AddOrRemoveEntry(drop.PickupEntry);

    NotifyDroppedItemPickedUp notify{};
    notify.ServerId = drop.ServerId;
    notify.Item = drop.PickupEntry;
    notify.DropId = drop.DropId;

    if (!GameServer::Get()->SendToPlayersInRange(notify, *entity, acMessage.GetSender()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);

    m_activeDrops.erase(dropIt);
}
