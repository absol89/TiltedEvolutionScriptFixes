

#include "ESLoader.h"
#include <filesystem>
#include <fstream>
#include <cstdlib>

#include <Records/CLMT.h>
#include <Records/NPC.h>
#include <Records/REFR.h>

namespace ESLoader
{
String ReadZString(Buffer::Reader& aReader) noexcept
{
    String zstring = String(reinterpret_cast<const char*>(aReader.GetDataAtPosition()));
    aReader.Advance(zstring.size() + 1);
    return zstring;
}

String ReadWString(Buffer::Reader& aReader) noexcept
{
    uint16_t stringLength = 0;
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&stringLength), 2);
    String wstring = String(reinterpret_cast<const char*>(aReader.GetDataAtPosition()), stringLength);
    aReader.Advance(stringLength);
    return wstring;
}

ESLoader::ESLoader()
{
    m_directory = fs::current_path() / "Data"; //< Keep upper case to match Skyrim's file system
}

UniquePtr<RecordCollection> ESLoader::BuildRecordCollection() noexcept
{
    if (!fs::is_directory(m_directory))
    {
        spdlog::warn("Data directory not found at {} (MO2 VFS may still provide files)", m_directory.string());
        // Continue; we will attempt to open files directly via VFS paths.
    }

    if (!LoadLoadOrder())
    {
        spdlog::warn("No load order found; will fallback to base ESM set if available.");
    }

    // Load all plugins from discovered list and index records
    auto recordCollection = LoadFiles();
    if (!recordCollection)
        return nullptr;

    // Optional: build cross-record references if needed elsewhere
    // recordCollection->BuildReferences();

    return std::move(recordCollection);
}

static bool LoadPluginsTxt(Vector<PluginData>& outOrder)
{
    // Try to read plugins.txt from LOCALAPPDATA (Windows)
    #if defined(_WIN32)
    char* localAppData = std::getenv("LOCALAPPDATA");
    if (localAppData)
    {
        fs::path pluginsPath = fs::path(localAppData) / "Skyrim Special Edition" / "plugins.txt";
        std::ifstream pluginsFile(pluginsPath.c_str());
        if (pluginsFile.is_open())
        {
            uint8_t standardId = 0x0;
            uint16_t liteId = 0x0;
            while (!pluginsFile.eof())
            {
                String line;
                std::getline(pluginsFile, line);
                if (line.empty() || line[0] == '#')
                    continue;
                // Some tools prefix active plugins with '*'
                if (!line.empty() && line[0] == '*')
                    line.erase(line.begin());
                // Trim CR just in case
                line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
                if (line.empty())
                    continue;

                PluginData plugin;
                plugin.m_filename = line;
                char extensionType = line.back();
                switch (extensionType)
                {
                case 'm':
                case 'p':
                {
                    PluginData p{};
                    p.m_filename = line;
                    p.m_standardId = standardId++;
                    p.m_isLite = false;
                    outOrder.push_back(p);
                    break;
                }
                case 'l':
                {
                    PluginData p{};
                    p.m_filename = line;
                    p.m_liteId = liteId++;
                    p.m_isLite = true;
                    outOrder.push_back(p);
                    break;
                }
                default:
                    break;
                }
            }
            return !outOrder.empty();
        }
    }
    #endif
    return false;
}

bool ESLoader::LoadLoadOrder()
{
    std::ifstream loadOrderFile;
    auto loadOrderPath = m_directory / "loadorder.txt";
    loadOrderFile.open(loadOrderPath.c_str());
    if (loadOrderFile.fail())
    {
        // Try plugins.txt fallback (MO2 / vanilla)
        if (LoadPluginsTxt(m_loadOrder))
        {
            // Populate master mapping for both ESM/ESP (non-lite)
            for (const auto& p : m_loadOrder)
            {
                if (!p.m_isLite)
                    m_masterFiles[p.m_filename] = p.m_standardId;
            }
            return true;
        }
        // Fallback to base ESMs minimal set
        spdlog::warn("Failed to open loadorder.txt; falling back to base ESM set");
        Vector<String> base = {"Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm"};
        uint8_t standardId = 0x0;
        for (auto& name : base)
        {
            PluginData plugin;
            plugin.m_filename = name;
            plugin.m_standardId = standardId;
            plugin.m_isLite = false;
            m_loadOrder.push_back(plugin);
            m_masterFiles[name] = standardId;
            standardId += 1;
        }
        return true;
    }

    uint8_t standardId = 0x0;
    uint16_t liteId = 0x0;

    while (!loadOrderFile.eof())
    {
        String line;
        std::getline(loadOrderFile, line);
        if (line.empty() || line[0] == '#')
            continue;

        // Trim CR (Linux/Windows)
        line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
        if (line.empty())
            continue;

        PluginData plugin;
        plugin.m_filename = line;

        char extensionType = line.back();

        switch (extensionType)
        {
        case 'm':
        case 'p':
            plugin.m_standardId = standardId;
            plugin.m_isLite = false;
            m_loadOrder.push_back(plugin);
            m_masterFiles[line] = standardId;
            standardId += 0x01;
            break;
        case 'l':
            plugin.m_liteId = liteId;
            plugin.m_isLite = true;
            m_loadOrder.push_back(plugin);
            liteId += 0x0001;
            break;
        default: spdlog::error("Extension in loadorder.txt not recognized: {}", line);
        }
    }

    return true;
}

UniquePtr<RecordCollection> ESLoader::LoadFiles()
{
    auto recordCollection = MakeUnique<RecordCollection>();

    for (PluginData& plugin : m_loadOrder)
    {
        fs::path pluginPath = GetPath(plugin.m_filename);
        if (pluginPath.empty())
        {
            spdlog::warn("Path to plugin file not found: {}", plugin.m_filename);
            continue;
        }

        TESFile pluginFile(m_masterFiles);
        if (plugin.IsLite())
            pluginFile.Setup(plugin.m_liteId);
        else
            pluginFile.Setup(plugin.m_standardId);

        bool loadResult = pluginFile.LoadFile(pluginPath);

        if (!loadResult)
            continue;

        pluginFile.IndexRecords(*recordCollection);
    }

    return recordCollection;
}

fs::path ESLoader::GetPath(String& aFilename)
{
    // Prefer direct path; MO2 VFS will usually resolve this without enumerating the directory
    fs::path direct = m_directory / aFilename;
    return direct;
}

} // namespace ESLoader
