#include <Games/Skyrim/RespawnOverrides.h>

#include <base/simpleini/SimpleIni.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <filesystem>
#include <string_view>

#include <Games/Primitives.h>

namespace
{
constexpr char kConfigPathName[] = "Data/SkyrimTogetherReborn/config";
constexpr char kSettingsFileName[] = "RespawnOverrides.ini";
constexpr char kSectionName[] = "RespawnOverrides";
constexpr char kSettingsComment[] =
    "; Respawn override positions.\n"
    "; Add one entry per bad center-of-cell respawn: CellEditorId = x, y, z\n"
    "; Use the cell editor ID from sources like UESP, and get coordinates in-game with player.getpos x/y/z.\n"
    "; Round coordinates down to whole numbers.";

struct RespawnOverride
{
    std::string_view CellEditorId;
    NiPoint3 Position;
};

const std::array kDefaultOverrides{
    // Keep built-in defaults sorted by cell editor ID. Community additions belong in RespawnOverrides.ini.
    RespawnOverride{"HaltedStreamCamp01", glm::vec3(-515.f, -501.f, 603.f)},
    RespawnOverride{"Korvanjund01", glm::vec3(-287.f, -5619.f, 608.f)},
    RespawnOverride{"Korvanjund02", glm::vec3(1201.f, -636.f, -400.f)},
    RespawnOverride{"QASmoke", glm::vec3(363.f, 2035.f, 7152.f)},
    RespawnOverride{"Saarthal02", glm::vec3(888.f, -269.f, -4.f)},
    RespawnOverride{"ShimmermistCave02", glm::vec3(5009.f, 57.f, -1319.f)},
    RespawnOverride{"StillbornCave01", glm::vec3(1473.f, -1044.f, -45.f)},
    RespawnOverride{"SwindlersDen01", glm::vec3(1470.f, -1651.f, 1969.f)},
};

std::string_view Trim(std::string_view aValue) noexcept
{
    constexpr std::string_view whitespace{" \t\r\n"};

    const auto start = aValue.find_first_not_of(whitespace);
    if (start == std::string_view::npos)
        return {};

    const auto end = aValue.find_last_not_of(whitespace);
    return aValue.substr(start, end - start + 1);
}

bool ReadFloat(std::string_view aValue, float& aOutValue) noexcept
{
    aValue = Trim(aValue);
    if (aValue.empty())
        return false;

    const auto* pBegin = aValue.data();
    const auto* pEnd = pBegin + aValue.size();
    const auto result = std::from_chars(pBegin, pEnd, aOutValue);

    return result.ec == std::errc{} && result.ptr == pEnd;
}

bool ReadPosition(std::string_view aValue, NiPoint3& aOutPosition) noexcept
{
    const auto xEnd = aValue.find(',');
    if (xEnd == std::string_view::npos)
        return false;

    const auto yEnd = aValue.find(',', xEnd + 1);
    if (yEnd == std::string_view::npos)
        return false;

    float x{};
    float y{};
    float z{};
    if (!ReadFloat(aValue.substr(0, xEnd), x) ||
        !ReadFloat(aValue.substr(xEnd + 1, yEnd - xEnd - 1), y) ||
        !ReadFloat(aValue.substr(yEnd + 1), z))
        return false;

    aOutPosition = glm::vec3(x, y, z);
    return true;
}

std::filesystem::path GetSettingsPath()
{
    return std::filesystem::current_path() / kConfigPathName / kSettingsFileName;
}

void CreateDefaultSettings(const std::filesystem::path& acPath)
{
    std::error_code error{};
    std::filesystem::create_directories(acPath.parent_path(), error);
    if (error)
        return;

    CSimpleIni ini;
    ini.SetUnicode();
    ini.SetValue(kSectionName, nullptr, nullptr, kSettingsComment);

    for (const auto& entry : kDefaultOverrides)
    {
        char value[64]{};
        const auto result = std::to_chars(value, value + sizeof(value), entry.Position.x);
        if (result.ec != std::errc{})
            continue;

        auto* pCursor = result.ptr;
        *pCursor++ = ',';
        *pCursor++ = ' ';

        const auto yResult = std::to_chars(pCursor, value + sizeof(value), entry.Position.y);
        if (yResult.ec != std::errc{})
            continue;

        pCursor = yResult.ptr;
        *pCursor++ = ',';
        *pCursor++ = ' ';

        const auto zResult = std::to_chars(pCursor, value + sizeof(value), entry.Position.z);
        if (zResult.ec != std::errc{})
            continue;

        ini.SetValue(kSectionName, entry.CellEditorId.data(), value);
    }

    std::string buffer;
    if (ini.Save(buffer, true) == SI_Error::SI_OK)
        TiltedPhoques::SaveFile(acPath, TiltedPhoques::String(buffer));
}

Map<String, NiPoint3> LoadSettings() noexcept
{
    Map<String, NiPoint3> result;

    try
    {
        const auto path = GetSettingsPath();

        std::error_code error{};
        if (!std::filesystem::exists(path, error))
            CreateDefaultSettings(path);

        if (error || !std::filesystem::exists(path, error))
            return result;

        CSimpleIni ini;
        ini.SetUnicode();

        const auto buffer = TiltedPhoques::LoadFile(path);
        if (ini.LoadData(buffer.c_str()) != SI_Error::SI_OK)
            return result;

        const auto* pSection = ini.GetSection(kSectionName);
        if (!pSection)
            return result;

        for (const auto& entry : *pSection)
        {
            NiPoint3 position{};
            if (entry.first.pItem && entry.second && ReadPosition(entry.second, position))
                result[String(entry.first.pItem)] = position;
        }
    }
    catch (...)
    {
    }

    return result;
}

const Map<String, NiPoint3>& GetConfiguredOverrides() noexcept
{
    static const Map<String, NiPoint3> s_overrides = LoadSettings();
    return s_overrides;
}
} // namespace

bool RespawnOverrides::GetRespawnPos(const char* apCellEditorId, NiPoint3& aOutPos) noexcept
{
    if (!apCellEditorId || !*apCellEditorId)
        return false;

    const String cellEditorId(apCellEditorId);

    const auto& configuredOverrides = GetConfiguredOverrides();
    const auto overrideIt = configuredOverrides.find(cellEditorId);
    if (overrideIt != configuredOverrides.end())
    {
        aOutPos = overrideIt->second;
        return true;
    }

    const auto defaultIt = std::lower_bound(
        kDefaultOverrides.begin(), kDefaultOverrides.end(), std::string_view{apCellEditorId},
        [](const RespawnOverride& acEntry, std::string_view acCellEditorId) { return acEntry.CellEditorId < acCellEditorId; });

    if (defaultIt == kDefaultOverrides.end() || defaultIt->CellEditorId != apCellEditorId)
        return false;

    aOutPos = defaultIt->Position;
    return true;
}

bool RespawnOverrides::GetRespawnPos(TESObjectCELL* apCell, NiPoint3& aOutPos) noexcept
{
    if (!apCell)
        return false;

    return GetRespawnPos(apCell->GetFormEditorID(), aOutPos);
}
