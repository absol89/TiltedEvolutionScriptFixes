#include "DropService.h"

#include <World.h>
#include <Components.h>
#include <GameServer.h>
#include <Messages/NotifyActorDrop.h>
#include <Messages/NotifyDroppedItemPickedUp.h>
#include <Messages/NotifyDroppedItems.h>
#include <Setting.h>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/ViewBuffer.hpp>

#include <array>
#include <cstdlib>
#include <filesystem>

#include <sqlite3.h>
#if defined(_WIN32)
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#else
#    include <unistd.h>
#endif

namespace
{
Console::Setting bEnableItemDrops{"Gameplay:bEnableItemDrops", "(Experimental) Syncs dropped items by players", true};

constexpr const char* kCreateDropsTableSql = R"SQL(
    CREATE TABLE IF NOT EXISTS dropped_items(
        drop_id INTEGER PRIMARY KEY,
        server_id INTEGER NOT NULL,
        actor_form_id INTEGER NOT NULL,
        cell_mod_id INTEGER NOT NULL,
        cell_base_id INTEGER NOT NULL,
        world_mod_id INTEGER NOT NULL,
        world_base_id INTEGER NOT NULL,
        has_location INTEGER NOT NULL,
        pos_x REAL,
        pos_y REAL,
        pos_z REAL,
        has_rotation INTEGER NOT NULL,
        rot_x REAL,
        rot_y REAL,
        rot_z REAL,
        item_blob BLOB NOT NULL,
        created_at INTEGER DEFAULT (strftime('%s','now'))
    );
)SQL";

std::filesystem::path ResolveExecutableDirectory() noexcept
{
    namespace fs = std::filesystem;

#if defined(_WIN32)
    std::array<wchar_t, MAX_PATH> buffer{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length != 0 && length < buffer.size())
    {
        return fs::path(buffer.data()).parent_path();
    }
#else
    std::error_code ec;
    auto exePath = fs::canonical("/proc/self/exe", ec);
    if (!ec)
        return exePath.parent_path();
#endif

    std::error_code ec;
    const auto current = fs::current_path(ec);
    if (!ec)
        return current;

    return {};
}

std::filesystem::path ResolveItemsDatabasePath() noexcept
{
    namespace fs = std::filesystem;

    if (auto exeDirectory = ResolveExecutableDirectory(); !exeDirectory.empty())
    {
        return exeDirectory / "items.db";
    }

#if defined(_WIN32)
    if (char* localAppData = nullptr; _dupenv_s(&localAppData, nullptr, "LOCALAPPDATA") == 0 && localAppData)
    {
        fs::path path(localAppData);
        free(localAppData);
        path /= "SkyrimTogether";
        path /= "Server";
        path /= "items.db";
        return path;
    }
#else
    if (const char* xdgDataHome = std::getenv("XDG_DATA_HOME"); xdgDataHome && xdgDataHome[0] != '\0')
    {
        return fs::path(xdgDataHome) / "skyrimtogether" / "server" / "items.db";
    }

    if (const char* home = std::getenv("HOME"); home && home[0] != '\0')
    {
        return fs::path(home) / ".local" / "share" / "skyrimtogether" / "server" / "items.db";
    }
#endif

    std::error_code ec;
    const auto current = fs::current_path(ec);
    if (!ec)
        return current / "data" / "items.db";

    return fs::path("items.db");
}
}

DropService::DropService(World& aWorld, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
{
    m_requestDropConnection = aDispatcher.sink<PacketEvent<RequestActorDrop>>().connect<&DropService::OnDropRequest>(this);
    m_requestPickupConnection = aDispatcher.sink<PacketEvent<RequestPickupDroppedItem>>().connect<&DropService::OnPickupRequest>(this);
    m_requestDroppedItemsConnection = aDispatcher.sink<PacketEvent<RequestDroppedItems>>().connect<&DropService::OnDroppedItemsRequest>(this);

    if (!InitializeDatabase())
        spdlog::error("DropService: failed to initialize drop persistence database");
    else
        LoadPersistedDrops();
}

DropService::~DropService()
{
    ShutdownDatabase();
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

    uint32_t actorFormId = 0;
    if (auto* pFormIdComponent = m_world.try_get<FormIdComponent>(*entity))
        actorFormId = pFormIdComponent->Id;
    spdlog::info("DropService: drop request actor {:X} server {:X}", message.ServerId, actorFormId);
    auto& inventoryComponent = view.get<InventoryComponent>(*it);
    inventoryComponent.Content.AddOrRemoveEntry(message.Item);

    auto* pCellComponent = m_world.try_get<CellIdComponent>(*entity);
    auto* pFormIdComponent = m_world.try_get<FormIdComponent>(*entity);
    if (!pFormIdComponent)
        spdlog::warn("DropService: drop requested for entity {:X} without FormIdComponent", message.ServerId);
    if (!pCellComponent)
        spdlog::warn("DropService: drop requested for entity {:X} without CellIdComponent", message.ServerId);

    ActiveDrop drop{};
    drop.DropId = m_nextDropId++;
    drop.ServerId = message.ServerId;
    drop.ActorFormId = pFormIdComponent ? pFormIdComponent->Id : 0;
    drop.DropEntry = message.Item;
    drop.PickupEntry = message.Item;
    if (drop.PickupEntry.Count < 0)
        drop.PickupEntry.Count = -drop.PickupEntry.Count;
    drop.HasLocation = message.HasLocation;
    drop.Location = message.Location;
    drop.HasRotation = message.HasRotation;
    drop.Rotation = message.Rotation;
    drop.CellId = pCellComponent ? pCellComponent->Cell : GameId{};
    drop.WorldSpaceId = pCellComponent ? pCellComponent->WorldSpaceId : GameId{};

    m_activeDrops[drop.DropId] = drop;
    spdlog::info("DropService: tracked drop {} for actor {:X}, item {:X}:{:X}", drop.DropId, drop.ServerId, drop.DropEntry.BaseId.ModId, drop.DropEntry.BaseId.BaseId);
    PersistDrop(drop);

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

    spdlog::info("DropService: pickup request actor {:X} drop {}", acMessage.Packet.ServerId, acMessage.Packet.DropId);

    const auto& message = acMessage.Packet;
    const auto dropIt = m_activeDrops.find(message.DropId);
    if (dropIt == m_activeDrops.end())
    {
        spdlog::warn("DropService: pickup requested for unknown drop id {}", message.DropId);
        return;
    }

    ActiveDrop drop = dropIt->second;

    const auto pickerEntity = m_world.TryResolveEntity(message.ServerId);
    if (!pickerEntity)
    {
        spdlog::warn("DropService: pickup requested for missing entity {:X}", message.ServerId);
        return;
    }

    auto view = m_world.view<InventoryComponent, OwnerComponent>();
    const auto it = view.find(*pickerEntity);
    if (it == view.end())
    {
        spdlog::warn("DropService: pickup requested for entity {:X} without InventoryComponent", message.ServerId);
        return;
    }

    auto& ownerComponent = view.get<OwnerComponent>(*it);
    if (ownerComponent.GetOwner() != acMessage.pPlayer)
    {
        spdlog::warn("DropService: pickup denied for {:X}: player {:X} not owner", message.ServerId, acMessage.pPlayer->GetConnectionId());
        return;
    }

    Inventory::Entry pickupEntry = drop.PickupEntry;
    if (message.Item.BaseId)
        pickupEntry = message.Item;

    auto& inventoryComponent = view.get<InventoryComponent>(*it);
    inventoryComponent.Content.AddOrRemoveEntry(pickupEntry);

    NotifyDroppedItemPickedUp notify{};
    notify.ServerId = message.ServerId;
    notify.Item = pickupEntry;
    notify.DropId = drop.DropId;

    GameServer::Get()->SendToPlayers(notify, nullptr);

    m_activeDrops.erase(dropIt);
    RemovePersistedDrop(drop.DropId);
    spdlog::info("DropService: drop {} picked up by actor {:X}", drop.DropId, message.ServerId);
}

void DropService::OnDroppedItemsRequest(const PacketEvent<RequestDroppedItems>& acMessage) noexcept
{
    if (!bEnableItemDrops)
        return;

    const auto& request = acMessage.Packet;
    NotifyDroppedItems notify{};
    notify.RequestId = request.RequestId;
    spdlog::info("DropService: drop sync request {} from player {:X}, all={}, cell {:X}:{:X}", request.RequestId, acMessage.GetSender()->GetConnectionId(), request.RequestAll,
                 request.HasCellFilter ? request.CellId.ModId : 0, request.HasCellFilter ? request.CellId.BaseId : 0);

    for (const auto& [dropId, drop] : m_activeDrops)
    {
        if (!request.RequestAll)
        {
            if (request.HasCellFilter && drop.CellId != request.CellId)
                continue;
            if (request.HasWorldSpaceFilter && drop.WorldSpaceId != request.WorldSpaceId)
                continue;
        }

        NotifyDroppedItems::Entry entry{};
        entry.DropId = dropId;
        entry.ServerId = drop.ServerId;
        entry.ActorFormId = drop.ActorFormId;
        entry.Item = drop.DropEntry;
        entry.HasLocation = drop.HasLocation;
        if (entry.HasLocation)
            entry.Location = drop.Location;
        entry.HasRotation = drop.HasRotation;
        if (entry.HasRotation)
            entry.Rotation = drop.Rotation;
        entry.CellId = drop.CellId;
        entry.WorldSpaceId = drop.WorldSpaceId;

        notify.Entries.push_back(std::move(entry));
    }

    acMessage.pPlayer->Send(notify);
    spdlog::info("DropService: sent {} drops in response to request {}", notify.Entries.size(), request.RequestId);
}

bool DropService::InitializeDatabase() noexcept
{
    m_databasePath = ResolveItemsDatabasePath();
    const auto dataDirectory = m_databasePath.parent_path();

    std::error_code ec;
    if (!dataDirectory.empty() && !std::filesystem::exists(dataDirectory, ec))
    {
        std::filesystem::create_directories(dataDirectory, ec);
        if (ec)
        {
            spdlog::error("DropService: failed to create data directory '{}': {}", dataDirectory.string(), ec.message());
            return false;
        }
    }

    spdlog::info("DropService: using drop database at '{}'", m_databasePath.string());
    if (sqlite3_open(m_databasePath.string().c_str(), &m_pDatabase) != SQLITE_OK)
    {
        spdlog::error("DropService: unable to open drop database at '{}': {}", m_databasePath.string(), sqlite3_errmsg(m_pDatabase));
        return false;
    }

    char* pError = nullptr;
    const int execResult = sqlite3_exec(m_pDatabase, kCreateDropsTableSql, nullptr, nullptr, &pError);
    if (execResult != SQLITE_OK)
    {
        spdlog::error("DropService: failed to initialize drop schema: {}", pError ? pError : "unknown");
        sqlite3_free(pError);
        return false;
    }

    return true;
}

void DropService::ShutdownDatabase() noexcept
{
    if (m_pDatabase)
    {
        sqlite3_close(m_pDatabase);
        m_pDatabase = nullptr;
    }
}

void DropService::LoadPersistedDrops() noexcept
{
    if (!m_pDatabase)
        return;

    constexpr const char* cSelectSql =
        "SELECT drop_id, server_id, actor_form_id, cell_mod_id, cell_base_id, world_mod_id, world_base_id, has_location, pos_x, pos_y, pos_z, has_rotation, rot_x, rot_y, rot_z, item_blob "
        "FROM dropped_items;";

    sqlite3_stmt* pStatement = nullptr;
    if (sqlite3_prepare_v2(m_pDatabase, cSelectSql, -1, &pStatement, nullptr) != SQLITE_OK)
    {
        spdlog::error("DropService: failed to prepare drop select statement: {}", sqlite3_errmsg(m_pDatabase));
        return;
    }

    uint32_t restoredCount = 0;
    while (sqlite3_step(pStatement) == SQLITE_ROW)
    {
        ActiveDrop drop{};
        drop.DropId = static_cast<uint64_t>(sqlite3_column_int64(pStatement, 0));
        drop.ServerId = static_cast<uint32_t>(sqlite3_column_int64(pStatement, 1));
        drop.ActorFormId = static_cast<uint32_t>(sqlite3_column_int64(pStatement, 2));
        drop.CellId.ModId = static_cast<uint32_t>(sqlite3_column_int64(pStatement, 3));
        drop.CellId.BaseId = static_cast<uint32_t>(sqlite3_column_int64(pStatement, 4));
        drop.WorldSpaceId.ModId = static_cast<uint32_t>(sqlite3_column_int64(pStatement, 5));
        drop.WorldSpaceId.BaseId = static_cast<uint32_t>(sqlite3_column_int64(pStatement, 6));

        drop.HasLocation = sqlite3_column_int(pStatement, 7) != 0;
        if (drop.HasLocation)
        {
            drop.Location.x = static_cast<float>(sqlite3_column_double(pStatement, 8));
            drop.Location.y = static_cast<float>(sqlite3_column_double(pStatement, 9));
            drop.Location.z = static_cast<float>(sqlite3_column_double(pStatement, 10));
        }

        drop.HasRotation = sqlite3_column_int(pStatement, 11) != 0;
        if (drop.HasRotation)
        {
            drop.Rotation.x = static_cast<float>(sqlite3_column_double(pStatement, 12));
            drop.Rotation.y = static_cast<float>(sqlite3_column_double(pStatement, 13));
            drop.Rotation.z = static_cast<float>(sqlite3_column_double(pStatement, 14));
        }

        const void* pBlob = sqlite3_column_blob(pStatement, 15);
        const int blobSize = sqlite3_column_bytes(pStatement, 15);
        if (!pBlob || blobSize <= 0)
        {
            spdlog::warn("DropService: missing item data for drop {}", drop.DropId);
            continue;
        }

        auto* pData = reinterpret_cast<const uint8_t*>(pBlob);
        TiltedPhoques::ViewBuffer view(const_cast<uint8_t*>(pData), static_cast<size_t>(blobSize));
        TiltedPhoques::Buffer::Reader reader(&view);
        drop.DropEntry.Deserialize(reader);
        drop.PickupEntry = drop.DropEntry;
        if (drop.PickupEntry.Count < 0)
            drop.PickupEntry.Count = -drop.PickupEntry.Count;

        m_activeDrops[drop.DropId] = drop;
        if (drop.DropId >= m_nextDropId)
            m_nextDropId = drop.DropId + 1;

        ++restoredCount;
    }

    sqlite3_finalize(pStatement);
    spdlog::info("DropService: restored {} persisted drops", restoredCount);
}

void DropService::PersistDrop(const ActiveDrop& acDrop) noexcept
{
    if (!m_pDatabase)
        return;

    constexpr const char* cInsertSql =
        "INSERT OR REPLACE INTO dropped_items (drop_id, server_id, actor_form_id, cell_mod_id, cell_base_id, world_mod_id, world_base_id, has_location, pos_x, pos_y, pos_z, has_rotation, rot_x, rot_y, rot_z, "
        "item_blob) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16);";

    sqlite3_stmt* pStatement = nullptr;
    if (sqlite3_prepare_v2(m_pDatabase, cInsertSql, -1, &pStatement, nullptr) != SQLITE_OK)
    {
        spdlog::error("DropService: failed to prepare drop insert statement: {}", sqlite3_errmsg(m_pDatabase));
        return;
    }

    sqlite3_bind_int64(pStatement, 1, static_cast<sqlite3_int64>(acDrop.DropId));
    sqlite3_bind_int64(pStatement, 2, static_cast<sqlite3_int64>(acDrop.ServerId));
    sqlite3_bind_int64(pStatement, 3, static_cast<sqlite3_int64>(acDrop.ActorFormId));
    sqlite3_bind_int64(pStatement, 4, static_cast<sqlite3_int64>(acDrop.CellId.ModId));
    sqlite3_bind_int64(pStatement, 5, static_cast<sqlite3_int64>(acDrop.CellId.BaseId));
    sqlite3_bind_int64(pStatement, 6, static_cast<sqlite3_int64>(acDrop.WorldSpaceId.ModId));
    sqlite3_bind_int64(pStatement, 7, static_cast<sqlite3_int64>(acDrop.WorldSpaceId.BaseId));

    sqlite3_bind_int(pStatement, 8, acDrop.HasLocation ? 1 : 0);
    if (acDrop.HasLocation)
    {
        sqlite3_bind_double(pStatement, 9, acDrop.Location.x);
        sqlite3_bind_double(pStatement, 10, acDrop.Location.y);
        sqlite3_bind_double(pStatement, 11, acDrop.Location.z);
    }
    else
    {
        sqlite3_bind_null(pStatement, 9);
        sqlite3_bind_null(pStatement, 10);
        sqlite3_bind_null(pStatement, 11);
    }

    sqlite3_bind_int(pStatement, 12, acDrop.HasRotation ? 1 : 0);
    if (acDrop.HasRotation)
    {
        sqlite3_bind_double(pStatement, 13, acDrop.Rotation.x);
        sqlite3_bind_double(pStatement, 14, acDrop.Rotation.y);
        sqlite3_bind_double(pStatement, 15, acDrop.Rotation.z);
    }
    else
    {
        sqlite3_bind_null(pStatement, 13);
        sqlite3_bind_null(pStatement, 14);
        sqlite3_bind_null(pStatement, 15);
    }

    TiltedPhoques::Buffer buffer(1 << 12);
    TiltedPhoques::Buffer::Writer writer(&buffer);
    acDrop.DropEntry.Serialize(writer);
    sqlite3_bind_blob(pStatement, 16, buffer.GetWriteData(), static_cast<int>(writer.Size()), SQLITE_TRANSIENT);

    const int stepResult = sqlite3_step(pStatement);
    if (stepResult != SQLITE_DONE)
        spdlog::error("DropService: failed to persist drop {}: {}", acDrop.DropId, sqlite3_errmsg(m_pDatabase));

    sqlite3_finalize(pStatement);
}

void DropService::RemovePersistedDrop(uint64_t aDropId) noexcept
{
    if (!m_pDatabase)
        return;

    constexpr const char* cDeleteSql = "DELETE FROM dropped_items WHERE drop_id = ?1;";

    sqlite3_stmt* pStatement = nullptr;
    if (sqlite3_prepare_v2(m_pDatabase, cDeleteSql, -1, &pStatement, nullptr) != SQLITE_OK)
    {
        spdlog::error("DropService: failed to prepare drop delete statement: {}", sqlite3_errmsg(m_pDatabase));
        return;
    }

    sqlite3_bind_int64(pStatement, 1, static_cast<sqlite3_int64>(aDropId));

    const int stepResult = sqlite3_step(pStatement);
    if (stepResult != SQLITE_DONE)
        spdlog::error("DropService: failed to remove persisted drop {}: {}", aDropId, sqlite3_errmsg(m_pDatabase));

    sqlite3_finalize(pStatement);
}
