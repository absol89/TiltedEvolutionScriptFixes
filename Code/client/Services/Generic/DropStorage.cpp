#include "DropStorage.h"

#include <TiltedCore/Buffer.hpp>

#include <TESObjectREFR.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <fstream>
#include <vector>

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
constexpr uint32_t kDropStorageMagic = 'D' | ('R' << 8) | ('O' << 16) | ('P' << 24);
constexpr uint32_t kDropStorageVersion = 2;

std::filesystem::path ResolveClientDataDirectory() noexcept
{
    namespace fs = std::filesystem;

#if defined(_WIN32)
    if (char* localAppData = nullptr; _dupenv_s(&localAppData, nullptr, "LOCALAPPDATA") == 0 && localAppData)
    {
        fs::path path(localAppData);
        free(localAppData);
        path /= "SkyrimTogether";
        path /= "Client";
        return path;
    }
#else
    if (const char* xdgDataHome = std::getenv("XDG_DATA_HOME"); xdgDataHome && xdgDataHome[0] != '\0')
        return fs::path(xdgDataHome) / "skyrimtogether" / "client";

    if (const char* home = std::getenv("HOME"); home && home[0] != '\0')
        return fs::path(home) / ".local" / "share" / "skyrimtogether" / "client";
#endif

    std::error_code ec;
    const auto current = fs::current_path(ec);
    if (!ec)
        return current / "client_data";

    return fs::path("client_data");
}
} // namespace

DropStorage::~DropStorage()
{
    Shutdown();
}

void DropStorage::SetActiveUser(std::string aUsername) noexcept
{
    const auto sanitized = SanitizeUser(aUsername);
    if (sanitized == m_activeUser)
        return;

    Shutdown();
    m_activeUser = sanitized.empty() ? "default" : sanitized;
}

bool DropStorage::EnsureInitialized() noexcept
{
    if (m_activeUser.empty())
        m_activeUser = "default";

    if (m_initialized)
        return true;

    m_databasePath = ResolveDatabasePath();
    EnsureDirectories(m_databasePath.parent_path());
    Load();
    m_initialized = true;
    return true;
}

void DropStorage::Shutdown() noexcept
{
    if (!m_cachedDrops.empty())
        Flush();

    m_cachedDrops.clear();
    m_databasePath.clear();
    m_initialized = false;
}

std::vector<DropStorage::CachedDrop> DropStorage::GetDropsForCell(const GameId& aCellId, const GameId& aWorldId) const noexcept
{
    std::vector<CachedDrop> drops;
    drops.reserve(m_cachedDrops.size());

    for (const auto& [dropId, drop] : m_cachedDrops)
    {
        if (drop.CellId == aCellId && drop.WorldSpaceId == aWorldId)
            drops.push_back(drop);
    }

    return drops;
}

std::vector<DropStorage::CachedDrop> DropStorage::GetAllDrops() const noexcept
{
    std::vector<CachedDrop> drops;
    drops.reserve(m_cachedDrops.size());
    for (const auto& [_, drop] : m_cachedDrops)
        drops.push_back(drop);
    return drops;
}

void DropStorage::RemoveCachedDrop(uint64_t aDropId) noexcept
{
    if (m_cachedDrops.erase(aDropId) > 0)
        Flush();
}

void DropStorage::OnServerDropTracked(uint64_t aDropId, const DropManager::ServerDropData& acData) noexcept
{
    if (!EnsureInitialized())
        return;

    CachedDrop drop{};
    drop.DropId = aDropId;
    drop.ServerId = acData.ServerId;
    drop.CellId = acData.CellId;
    drop.WorldSpaceId = acData.WorldSpaceId;
    drop.ReferenceId = acData.ReferenceId;
    drop.Item = acData.Item;
    drop.Location = acData.Location;

    if (auto it = m_cachedDrops.find(aDropId); it != m_cachedDrops.end())
        drop.RefFormId = it->second.RefFormId;
    else
        drop.RefFormId = 0;

    m_cachedDrops[aDropId] = drop;
    spdlog::info("DropStorage: cached drop {} for cell {:X}:{:X}", aDropId, drop.CellId.ModId, drop.CellId.BaseId);
    Flush();
}

void DropStorage::OnDropHandleBound(uint64_t aDropId, uint32_t aHandleBits) noexcept
{
    if (!EnsureInitialized())
        return;

    auto it = m_cachedDrops.find(aDropId);
    if (it == m_cachedDrops.end())
        return;

    auto& drop = it.value();
    drop.RefFormId = ResolveRefFormId(aHandleBits);
    Flush();
}

void DropStorage::OnServerDropRemoved(uint64_t aDropId) noexcept
{
    RemoveCachedDrop(aDropId);
    spdlog::info("DropStorage: removed cached drop {}", aDropId);
}

std::string DropStorage::SanitizeUser(const std::string& aUsername)
{
    std::string result;
    result.reserve(aUsername.size());
    for (const char ch : aUsername)
    {
        if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_')
            result.push_back(ch);
        else
            result.push_back('_');
    }
    return result;
}

std::filesystem::path DropStorage::ResolveDatabasePath() const
{
    return ResolveClientDataDirectory() / fmt::format("{}-items.db", m_activeUser.empty() ? "default" : m_activeUser);
}

void DropStorage::EnsureDirectories(const std::filesystem::path& aPath) const noexcept
{
    if (aPath.empty())
        return;

    std::error_code ec;
    if (!std::filesystem::exists(aPath, ec))
        std::filesystem::create_directories(aPath, ec);

    if (ec)
        spdlog::warn("DropStorage: failed to ensure directory '{}': {}", aPath.string(), ec.message());
}

void DropStorage::Load() noexcept
{
    m_cachedDrops.clear();

    std::ifstream input(m_databasePath, std::ios::binary);
    if (!input.is_open())
        return;

    uint32_t magic = 0;
    uint32_t version = 0;

    input.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    input.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (!input || magic != kDropStorageMagic || version != kDropStorageVersion)
    {
        spdlog::warn("DropStorage: invalid header for '{}', ignoring cache", m_databasePath.string());
        return;
    }

    uint32_t count = 0;
    input.read(reinterpret_cast<char*>(&count), sizeof(count));
    if (!input)
        return;

    for (uint32_t i = 0; i < count; ++i)
    {
        CachedDrop drop{};
        input.read(reinterpret_cast<char*>(&drop.DropId), sizeof(drop.DropId));
        if (version >= 2)
            input.read(reinterpret_cast<char*>(&drop.ServerId), sizeof(drop.ServerId));
        else
            drop.ServerId = 0;

        input.read(reinterpret_cast<char*>(&drop.CellId.ModId), sizeof(drop.CellId.ModId));
        input.read(reinterpret_cast<char*>(&drop.CellId.BaseId), sizeof(drop.CellId.BaseId));
        input.read(reinterpret_cast<char*>(&drop.WorldSpaceId.ModId), sizeof(drop.WorldSpaceId.ModId));
        input.read(reinterpret_cast<char*>(&drop.WorldSpaceId.BaseId), sizeof(drop.WorldSpaceId.BaseId));

        if (version >= 2)
        {
            input.read(reinterpret_cast<char*>(&drop.ReferenceId.ModId), sizeof(drop.ReferenceId.ModId));
            input.read(reinterpret_cast<char*>(&drop.ReferenceId.BaseId), sizeof(drop.ReferenceId.BaseId));
        }
        else
        {
            drop.ReferenceId = {};
        }

        input.read(reinterpret_cast<char*>(&drop.Location), sizeof(drop.Location));
        input.read(reinterpret_cast<char*>(&drop.RefFormId), sizeof(drop.RefFormId));

        uint32_t itemSize = 0;
        input.read(reinterpret_cast<char*>(&itemSize), sizeof(itemSize));
        if (!input || itemSize == 0)
            continue;

        std::vector<uint8_t> itemBuffer(itemSize);
        input.read(reinterpret_cast<char*>(itemBuffer.data()), itemSize);
        if (!input)
            continue;

        TiltedPhoques::ViewBuffer view(itemBuffer.data(), itemBuffer.size());
        TiltedPhoques::Buffer::Reader reader(&view);
        drop.Item.Deserialize(reader);

        m_cachedDrops[drop.DropId] = drop;
    }
}

void DropStorage::Flush() noexcept
{
    if (!EnsureInitialized())
        return;

    std::ofstream output(m_databasePath, std::ios::binary | std::ios::trunc);
    if (!output.is_open())
    {
        spdlog::error("DropStorage: failed to save cache '{}'", m_databasePath.string());
        return;
    }

    const uint32_t magic = kDropStorageMagic;
    const uint32_t version = kDropStorageVersion;
    const uint32_t count = static_cast<uint32_t>(m_cachedDrops.size());

    output.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    output.write(reinterpret_cast<const char*>(&version), sizeof(version));
    output.write(reinterpret_cast<const char*>(&count), sizeof(count));

    for (const auto& [dropId, drop] : m_cachedDrops)
    {
        output.write(reinterpret_cast<const char*>(&drop.DropId), sizeof(drop.DropId));
        output.write(reinterpret_cast<const char*>(&drop.ServerId), sizeof(drop.ServerId));
        output.write(reinterpret_cast<const char*>(&drop.CellId.ModId), sizeof(drop.CellId.ModId));
        output.write(reinterpret_cast<const char*>(&drop.CellId.BaseId), sizeof(drop.CellId.BaseId));
        output.write(reinterpret_cast<const char*>(&drop.WorldSpaceId.ModId), sizeof(drop.WorldSpaceId.ModId));
        output.write(reinterpret_cast<const char*>(&drop.WorldSpaceId.BaseId), sizeof(drop.WorldSpaceId.BaseId));
        output.write(reinterpret_cast<const char*>(&drop.ReferenceId.ModId), sizeof(drop.ReferenceId.ModId));
        output.write(reinterpret_cast<const char*>(&drop.ReferenceId.BaseId), sizeof(drop.ReferenceId.BaseId));
        output.write(reinterpret_cast<const char*>(&drop.Location), sizeof(drop.Location));
        output.write(reinterpret_cast<const char*>(&drop.RefFormId), sizeof(drop.RefFormId));

        TiltedPhoques::Buffer buffer(1 << 12);
        TiltedPhoques::Buffer::Writer writer(&buffer);
        drop.Item.Serialize(writer);
        const uint32_t itemSize = static_cast<uint32_t>(writer.Size());
        output.write(reinterpret_cast<const char*>(&itemSize), sizeof(itemSize));
        output.write(reinterpret_cast<const char*>(buffer.GetWriteData()), itemSize);
    }
}

uint32_t DropStorage::ResolveRefFormId(uint32_t aHandleBits) const noexcept
{
    if (!aHandleBits)
        return 0;

    if (auto* pRef = TESObjectREFR::GetByHandle(aHandleBits))
        return pRef->formID;

    return 0;
}
