#include <Services/InventoryService.h>

#include <cstdlib>
#include <TiltedCore/Stl.hpp>

#include <Messages/RequestObjectInventoryChanges.h>
#include <Messages/NotifyObjectInventoryChanges.h>
#include <Messages/RequestInventoryChanges.h>
#include <Messages/NotifyInventoryChanges.h>
#include <Messages/RequestEquipmentChanges.h>
#include <Messages/NotifyEquipmentChanges.h>
#include <Messages/DrawWeaponRequest.h>
#include <Messages/NotifyDrawWeapon.h>

#include <Events/UpdateEvent.h>
#include <Events/InventoryChangeEvent.h>
#include <Events/EquipmentChangeEvent.h>

#include <Components.h>
#include <World.h>
#include <Games/Skyrim/Interface/UI.h>
#include <PlayerCharacter.h>
#include <Forms/TESObjectCELL.h>
#include <Actor.h>
#include <Structs/ObjectData.h>
#include <Forms/TESWorldSpace.h>
#include <Games/TES.h>
#include <Games/Overrides.h>
#include <EquipManager.h>
#include <Games/ActorExtension.h>
#include <Forms/TESNPC.h>
#include <DefaultObjectManager.h>
#include <Games/Primitives.h>

InventoryService::InventoryService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
    , m_transport(aTransport)
{
    m_updateConnection = m_dispatcher.sink<UpdateEvent>().connect<&InventoryService::OnUpdate>(this);
    m_inventoryConnection = m_dispatcher.sink<InventoryChangeEvent>().connect<&InventoryService::OnInventoryChangeEvent>(this);
    m_equipmentConnection = m_dispatcher.sink<EquipmentChangeEvent>().connect<&InventoryService::OnEquipmentChangeEvent>(this);
    m_inventoryChangeConnection = m_dispatcher.sink<NotifyInventoryChanges>().connect<&InventoryService::OnNotifyInventoryChanges>(this);
    m_equipmentChangeConnection = m_dispatcher.sink<NotifyEquipmentChanges>().connect<&InventoryService::OnNotifyEquipmentChanges>(this);
}

void InventoryService::OnUpdate(const UpdateEvent& acUpdateEvent) noexcept
{
    ProcessPendingEquipment();
    RunWeaponStateUpdates();
    RunNakedNPCBugChecks();
}

void InventoryService::OnInventoryChangeEvent(const InventoryChangeEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    auto view = m_world.view<FormIdComponent>();

    const auto iter = std::find_if(std::begin(view), std::end(view), [view, formId = acEvent.FormId](auto entity) { return view.get<FormIdComponent>(entity).Id == formId; });

    if (iter == std::end(view))
        return;

    std::optional<uint32_t> serverIdRes = Utils::GetServerId(*iter);
    if (!serverIdRes.has_value())
    {
        spdlog::error(__FUNCTION__ ": failed to find server id, target form id: {:X}, item id: {:X}, count: {}", acEvent.FormId, acEvent.Item.BaseId.BaseId, acEvent.Item.Count);
        return;
    }

    RequestInventoryChanges request;
    request.ServerId = serverIdRes.value();
    request.Item = acEvent.Item;
    request.Drop = acEvent.Drop;
    request.UpdateClients = acEvent.UpdateClients;
    if (acEvent.DropLocation)
    {
        request.HasDropLocation = true;
        request.DropLocation = *acEvent.DropLocation;
    }
    if (acEvent.DropRotation)
    {
        request.HasDropRotation = true;
        request.DropRotation = *acEvent.DropRotation;
    }
    if (acEvent.DropInstanceId)
    {
        request.HasDropInstanceId = true;
        request.DropInstanceId = *acEvent.DropInstanceId;
    }

    m_transport.Send(request);

    spdlog::info("Sending item request, item: {:X}, count: {}, target object: {:X}", acEvent.Item.BaseId.BaseId, acEvent.Item.Count, acEvent.FormId);
}

void InventoryService::OnEquipmentChangeEvent(const EquipmentChangeEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    auto view = m_world.view<FormIdComponent>();

    const auto iter = std::find_if(std::begin(view), std::end(view), [view, formId = acEvent.ActorId](auto entity) { return view.get<FormIdComponent>(entity).Id == formId; });

    if (iter == std::end(view))
        return;

    std::optional<uint32_t> serverIdRes = Utils::GetServerId(*iter);
    if (!serverIdRes.has_value())
    {
        spdlog::error(__FUNCTION__ ": failed to find server id, actor id: {:X}, item id: {:X}, isAmmo: {}, unequip: {}, slot: {:X}", acEvent.ActorId, acEvent.ItemId, acEvent.IsAmmo, acEvent.Unequip, acEvent.EquipSlotId);
        return;
    }

    Actor* pActor = Cast<Actor>(TESForm::GetById(acEvent.ActorId));
    if (!pActor)
        return;

    auto& modSystem = World::Get().GetModSystem();

    RequestEquipmentChanges request;
    request.ServerId = serverIdRes.value();

    if (!modSystem.GetServerModId(acEvent.EquipSlotId, request.EquipSlotId))
        return;
    if (!modSystem.GetServerModId(acEvent.ItemId, request.ItemId))
        return;

    const int32_t cEffectiveCount = acEvent.Count == 0 ? 1 : acEvent.Count;
    request.Count = cEffectiveCount;
    request.Unequip = acEvent.Unequip;
    request.IsSpell = acEvent.IsSpell;
    request.IsShout = acEvent.IsShout;
    request.IsAmmo = acEvent.IsAmmo;
    request.CurrentInventory = pActor->GetEquipment();

    m_transport.Send(request);

    spdlog::info("Sending equipment request, item: {:X}, count: {}, target object: {:X}", acEvent.ItemId, cEffectiveCount, acEvent.ActorId);
}

void InventoryService::OnNotifyInventoryChanges(const NotifyInventoryChanges& acMessage) noexcept
{
    TESObjectREFR* pObject = Utils::GetByServerId<TESObjectREFR>(acMessage.ServerId);
    if (!pObject)
    {
        spdlog::error("{}: could not find server id {:X} for inventory change", __FUNCTION__, acMessage.ServerId);
        return;
    }

    Actor* pActor = Cast<Actor>(pObject);

    if (acMessage.Silent)
    {
        ScopedInventoryOverride _;
        pObject->AddOrRemoveItem(acMessage.Item);
        return;
    }

    std::optional<uint32_t> dropInstanceId{};
    if (acMessage.HasDropInstanceId)
        dropInstanceId = acMessage.DropInstanceId;

    NiPoint3 dropLocation{};
    NiPoint3 dropRotation{};
    NiPoint3* pDropLocation = nullptr;
    NiPoint3* pDropRotation = nullptr;

    if (acMessage.HasDropLocation)
    {
        dropLocation = NiPoint3(acMessage.DropLocation);
        pDropLocation = &dropLocation;
    }

    if (acMessage.HasDropRotation)
    {
        dropRotation = NiPoint3(acMessage.DropRotation);
        pDropRotation = &dropRotation;
    }

    if (acMessage.Drop)
    {
        if (!pActor)
        {
            spdlog::error("{}: could not find actor server id {:X}", __FUNCTION__, acMessage.ServerId);
            return;
        }

        struct DropSyncScope
        {
            DropSyncScope(uint32_t aActorFormId, const std::optional<uint32_t>& aDropId)
            {
                if (aDropId)
                {
                    DropSync::PendingDropActorFormId = aActorFormId;
                    DropSync::PendingDropId = aDropId;
                    bActive = true;
                }
            }

            ~DropSyncScope()
            {
                if (bActive)
                {
                    DropSync::PendingDropId.reset();
                    DropSync::PendingDropActorFormId = 0;
                }
            }

            bool bActive{false};
        } dropScope(pActor->formID, dropInstanceId);

        ScopedInventoryOverride _;
        pActor->DropOrPickUpObject(acMessage.Item, pDropLocation, pDropRotation);
        return;
    }

    if (dropInstanceId && pActor)
    {
        if (auto handleOpt = Actor::ConsumeTrackedDrop(pActor->formID, *dropInstanceId))
        {
            uint32_t handleBits = *handleOpt;
            if (auto* pDroppedRef = TESObjectREFR::GetByHandle(handleBits))
            {
                ScopedInventoryOverride _;
                int32_t count = acMessage.Item.Count > 0 ? acMessage.Item.Count : -acMessage.Item.Count;
                if (count <= 0)
                    count = 1;
                pActor->PickUpObject(pDroppedRef, count, false, 0.0f);
                return;
            }
            else
            {
                Actor::TrackRemoteDrop(pActor->formID, *dropInstanceId, handleBits);
            }
        }
    }

    ScopedInventoryOverride _;

    if (pActor)
        pActor->DropOrPickUpObject(acMessage.Item, pDropLocation, pDropRotation);
    else
        pObject->AddOrRemoveItem(acMessage.Item);
}

void InventoryService::OnNotifyEquipmentChanges(const NotifyEquipmentChanges& acMessage) noexcept
{
    Actor* pActor = Utils::GetByServerId<Actor>(acMessage.ServerId);
    if (!pActor)
    {
        spdlog::error("{}: could not find actor server id {:X}", __FUNCTION__, acMessage.ServerId);
        return;
    }

    if (!pActor->GetNiNode())
    {
        auto view = m_world.view<FormIdComponent>();
        const auto itor = std::find_if(std::begin(view), std::end(view), [formId = pActor->formID, view](entt::entity entity) { return view.get<FormIdComponent>(entity).Id == formId; });

        if (itor != std::end(view))
        {
            auto* pPending = m_world.try_get<PendingEquipmentComponent>(*itor);
            if (!pPending)
                pPending = &m_world.emplace<PendingEquipmentComponent>(*itor);

            pPending->PendingChanges.push_back(acMessage);
            spdlog::debug("Queued equipment change for actor {:X} until 3D is ready", pActor->formID);
        }
        else
        {
            spdlog::warn("{}: could not queue equipment change, entity not found for form id {:X}", __FUNCTION__, pActor->formID);
        }

        return;
    }

    ApplyEquipmentChange(pActor, acMessage);
}

void InventoryService::ApplyEquipmentChange(Actor* pActor, const NotifyEquipmentChanges& acMessage) noexcept
{
    auto& modSystem = World::Get().GetModSystem();

    uint32_t itemId = modSystem.GetGameId(acMessage.ItemId);
    TESForm* pItem = TESForm::GetById(itemId);

    if (!pItem)
    {
        spdlog::error("Could not find inventory item {:X}:{:X}", acMessage.ItemId.ModId, acMessage.ItemId.BaseId);
        return;
    }

    uint32_t equipSlotId = modSystem.GetGameId(acMessage.EquipSlotId);
    TESForm* pEquipSlot = TESForm::GetById(equipSlotId);

    uint32_t slotId = 0;
    if (pEquipSlot == DefaultObjectManager::Get().rightEquipSlot)
        slotId = 1;

    auto* pEquipManager = EquipManager::Get();

    if (acMessage.IsSpell)
    {
        if (acMessage.Unequip)
            pEquipManager->UnEquipSpell(pActor, pItem, slotId);
        else
            pEquipManager->EquipSpell(pActor, pItem, slotId);

        return;
    }
    else if (acMessage.IsShout)
    {
        if (acMessage.Unequip)
            pEquipManager->UnEquipShout(pActor, pItem);
        else
            pEquipManager->EquipShout(pActor, pItem);

        return;
    }

    auto* pObject = Cast<TESBoundObject>(pItem);

    // TODO: ExtraData necessary? probably
    const int32_t count = acMessage.Count == 0 ? 1 : acMessage.Count;

    if (acMessage.Unequip)
    {
        pEquipManager->UnEquip(pActor, pItem, nullptr, count, pEquipSlot, false, true, false, false, nullptr);
    }
    else
    {
        // Unequip all armor first, since the game won't auto unequip armor
        Inventory wornArmor{};
        if (pItem->formType == FormType::Armor)
        {
            wornArmor = pActor->GetWornArmor();
            for (const auto& armor : wornArmor.Entries)
            {
                uint32_t armorId = modSystem.GetGameId(armor.BaseId);
                TESForm* pArmor = TESForm::GetById(armorId);
                if (pArmor)
                    pEquipManager->UnEquip(pActor, pArmor, nullptr, 1, pEquipSlot, false, true, false, false, nullptr);
            }
        }

        pEquipManager->Equip(pActor, pItem, nullptr, count, pEquipSlot, false, true, false, false);

        for (const auto& armor : wornArmor.Entries)
        {
            uint32_t armorId = modSystem.GetGameId(armor.BaseId);
            TESForm* pArmor = TESForm::GetById(armorId);
            if (pArmor)
                pEquipManager->Equip(pActor, pArmor, nullptr, 1, pEquipSlot, false, true, false, false);
        }
    }
}

void InventoryService::ProcessPendingEquipment() noexcept
{
    auto view = m_world.view<FormIdComponent, PendingEquipmentComponent>();
    TiltedPhoques::Vector<entt::entity> toClear;

    for (auto entity : view)
    {
        auto& formIdComponent = view.get<FormIdComponent>(entity);
        auto& pending = view.get<PendingEquipmentComponent>(entity);

        Actor* pActor = Cast<Actor>(TESForm::GetById(formIdComponent.Id));
        if (!pActor || !pActor->GetNiNode())
            continue;

        for (const auto& change : pending.PendingChanges)
            ApplyEquipmentChange(pActor, change);

        toClear.push_back(entity);
    }

    for (auto entity : toClear)
        m_world.remove<PendingEquipmentComponent>(entity);
}

void InventoryService::RunWeaponStateUpdates() noexcept
{
    if (!m_transport.IsConnected())
        return;

    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenUpdates = 500ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenUpdates)
        return;

    lastSendTimePoint = now;

    auto view = m_world.view<FormIdComponent, LocalComponent>();

    for (auto entity : view)
    {
        const auto& formIdComponent = view.get<FormIdComponent>(entity);
        Actor* const pActor = Cast<Actor>(TESForm::GetById(formIdComponent.Id));
        auto& localComponent = view.get<LocalComponent>(entity);

        bool isWeaponDrawn = pActor->actorState.IsWeaponDrawn();
        if (isWeaponDrawn != localComponent.IsWeaponDrawn)
        {
            localComponent.IsWeaponDrawn = isWeaponDrawn;

            DrawWeaponRequest request;
            request.Id = localComponent.Id;
            request.IsWeaponDrawn = isWeaponDrawn;

            m_transport.Send(request);
        }
    }
}

void InventoryService::RunNakedNPCBugChecks() noexcept
{
    if (!m_transport.IsConnected())
        return;

    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenUpdates = 1000ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenUpdates)
        return;

    lastSendTimePoint = now;

    auto view = m_world.view<FormIdComponent>();

    for (auto entity : view)
    {
        const auto& formIdComponent = view.get<FormIdComponent>(entity);
        Actor* pActor = Cast<Actor>(TESForm::GetById(formIdComponent.Id));
        if (!pActor)
            continue;

        if (pActor->GetExtension()->IsPlayer())
            continue;

        if (pActor->IsDead())
            continue;

        if (pActor->IsWearingBodyPiece())
            continue;

        if (!pActor->ShouldWearBodyPiece())
            continue;

        // Don't broadcast changes, it'll just make things messier.
        // If all clients have this problem, they'll all fix it individually.
        ScopedEquipOverride seo;
        ScopedInventoryOverride sio;

        pActor->ResetInventory(false);
    }
}
