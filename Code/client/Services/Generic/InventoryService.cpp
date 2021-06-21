#include <Services/InventoryService.h>

#include <Messages/RequestInventoryChanges.h>
#include <Messages/NotifyInventoryChanges.h>

#include <Events/UpdateEvent.h>
#include <Events/InventoryChangeEvent.h>

#include <World.h>
#include <PlayerCharacter.h>
#include <Forms/TESObjectCELL.h>
#include <Actor.h>

InventoryService::InventoryService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
    , m_transport(aTransport)
{
    m_updateConnection = m_dispatcher.sink<UpdateEvent>().connect<&InventoryService::OnUpdate>(this);
    m_inventoryConnection = m_dispatcher.sink<InventoryChangeEvent>().connect<&InventoryService::OnInventoryChangeEvent>(this);
    // TODO: connections
}

void InventoryService::OnUpdate(const UpdateEvent& acUpdateEvent) noexcept
{
    RunObjectInventoryUpdates();
    RunCharacterInventoryUpdates();

    ApplyCachedObjectInventoryChanges();
    ApplyCachedCharacterInventoryChanges();
}

void InventoryService::OnInventoryChangeEvent(const InventoryChangeEvent& acEvent) noexcept
{
    const auto* pForm = TESForm::GetById(acEvent.FormId);
    if (RTTI_CAST(pForm, TESForm, Actor))
    {
        if (m_charactersWithInventoryChanges.find(acEvent.FormId) != std::end(m_charactersWithInventoryChanges))
            m_charactersWithInventoryChanges.insert(acEvent.FormId);
    }

    if (m_objectsWithInventoryChanges.find(acEvent.FormId) != std::end(m_objectsWithInventoryChanges))
        m_objectsWithInventoryChanges.insert(acEvent.FormId);
}

void InventoryService::OnInventoryChanges(const NotifyInventoryChanges& acEvent) noexcept
{
}

void InventoryService::RunObjectInventoryUpdates() noexcept
{
    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenSnapshots = 250ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenSnapshots)
        return;

    lastSendTimePoint = now;

    if (!m_objectsWithInventoryChanges.empty())
    {
        RequestInventoryChanges message;

        for (const auto objectId : m_objectsWithInventoryChanges)
        {
            const auto* pObject = RTTI_CAST(TESForm::GetById(objectId), TESForm, TESObjectREFR);

            if (!pObject)
                continue;

            uint32_t baseId = 0;
            uint32_t modId = 0;
            if (!m_world.GetModSystem().GetServerModId(pObject->formID, modId, baseId))
                continue;

            uint32_t cellBaseId = 0;
            uint32_t cellModId = 0;
            if (!m_world.GetModSystem().GetServerModId(pObject->parentCell->formID, cellModId, cellBaseId))
                continue;

            Inventory inventory;
            inventory.Buffer = pObject->SerializeInventory();

            message.Changes[objectId] = inventory;
        }

        m_transport.Send(message);

        m_objectsWithInventoryChanges.clear();
    }
}

void InventoryService::RunCharacterInventoryUpdates() noexcept
{
    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenSnapshots = 250ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenSnapshots)
        return;

    lastSendTimePoint = now;

    if (!m_charactersWithInventoryChanges.empty())
    {
        RequestInventoryChanges message;

        auto animatedLocalView = m_world.view<LocalComponent, LocalAnimationComponent, FormIdComponent>();
        for (auto entity : animatedLocalView)
        {
            auto& formIdComponent = animatedLocalView.get<FormIdComponent>(entity);
            auto& localComponent = animatedLocalView.get<LocalComponent>(entity);

            if (m_charactersWithInventoryChanges.find(formIdComponent.Id) == std::end(m_charactersWithInventoryChanges))
                continue;

            const auto* pForm = TESForm::GetById(formIdComponent.Id);
            const auto* pActor = RTTI_CAST(pForm, TESForm, Actor);
            if (!pActor)
                continue;

            message.Changes[localComponent.Id] = pActor->GetInventory();
        }

        m_transport.Send(message);

        m_charactersWithInventoryChanges.clear();
    }
}

void InventoryService::ApplyCachedObjectInventoryChanges() noexcept
{

}

void InventoryService::ApplyCachedCharacterInventoryChanges() noexcept
{
}
