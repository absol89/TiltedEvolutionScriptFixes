#pragma once

#include <Sync/DropManager.h>
#include <Structs/GameId.h>
#include <Games/Primitives.h>

#include <TiltedCore/Stl.hpp>

#include <filesystem>
#include <string>
#include <vector>

class DropStorage final : public DropManager::StorageListener
{
public:
    struct CachedDrop
    {
        uint64_t DropId{};
        GameId CellId{};
        GameId WorldSpaceId{};
        Inventory::Entry Item{};
        NiPoint3 Location{};
        uint32_t RefFormId{};
    };

    DropStorage() = default;
    ~DropStorage() override;

    void SetActiveUser(std::string aUsername) noexcept;
    bool EnsureInitialized() noexcept;
    void Shutdown() noexcept;
    std::vector<CachedDrop> GetDropsForCell(const GameId& aCellId, const GameId& aWorldId) const noexcept;
    void RemoveCachedDrop(uint64_t aDropId) noexcept;

    void OnServerDropTracked(uint64_t aDropId, const DropManager::ServerDropData& acData) noexcept override;
    void OnDropHandleBound(uint64_t aDropId, uint32_t aHandleBits) noexcept override;
    void OnServerDropRemoved(uint64_t aDropId) noexcept override;

private:
    static std::string SanitizeUser(const std::string& aUsername);
    std::filesystem::path ResolveDatabasePath() const;
    bool InitializeDatabase() noexcept;
    void EnsureDirectories(const std::filesystem::path& aPath) const noexcept;
    void Load() noexcept;
    void Flush() noexcept;
    uint32_t ResolveRefFormId(uint32_t aHandleBits) const noexcept;

    std::filesystem::path m_databasePath;
    std::string m_activeUser{};
    bool m_initialized{false};
    TiltedPhoques::Map<uint64_t, CachedDrop> m_cachedDrops;
};
