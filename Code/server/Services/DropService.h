#pragma once

#include <Events/PacketEvent.h>
#include <Messages/RequestActorDrop.h>
#include <Messages/RequestPickupDroppedItem.h>
#include <Messages/RequestDroppedItems.h>
#include <TiltedCore/Stl.hpp>
#include <filesystem>
#include <Structs/GameId.h>

struct NotifyActorDrop;
struct NotifyDroppedItemPickedUp;
struct NotifyDroppedItems;
struct sqlite3;

struct World;
struct InventoryComponent;
struct OwnerComponent;

class DropService
{
public:
    DropService(World& aWorld, entt::dispatcher& aDispatcher);
    ~DropService();

private:
    struct ActiveDrop
    {
        uint64_t DropId{};
        uint32_t ServerId{};
        uint32_t ActorFormId{};
        Inventory::Entry DropEntry{};
        Inventory::Entry PickupEntry{};
        bool HasLocation{false};
        Vector3_NetQuantize Location{};
        bool HasRotation{false};
        Vector3_NetQuantize Rotation{};
        GameId CellId{};
        GameId WorldSpaceId{};
    };

    void OnDropRequest(const PacketEvent<RequestActorDrop>& acMessage) noexcept;
    void OnPickupRequest(const PacketEvent<RequestPickupDroppedItem>& acMessage) noexcept;
    void OnDroppedItemsRequest(const PacketEvent<RequestDroppedItems>& acMessage) noexcept;
    bool InitializeDatabase() noexcept;
    void ShutdownDatabase() noexcept;
    void LoadPersistedDrops() noexcept;
    void PersistDrop(const ActiveDrop& acDrop) noexcept;
    void RemovePersistedDrop(uint64_t aDropId) noexcept;

    World& m_world;
    entt::scoped_connection m_requestDropConnection;
    entt::scoped_connection m_requestPickupConnection;
    entt::scoped_connection m_requestDroppedItemsConnection;

    uint64_t m_nextDropId{1};
    TiltedPhoques::Map<uint64_t, ActiveDrop> m_activeDrops;
    sqlite3* m_pDatabase{nullptr};
    std::filesystem::path m_databasePath;
};
