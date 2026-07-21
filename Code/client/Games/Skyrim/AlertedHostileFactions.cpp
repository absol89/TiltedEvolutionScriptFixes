#include "AlertedHostileFactions.h"

#include <base/simpleini/SimpleIni.h>

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <string_view>
#include <vector>

#include <Games/Primitives.h>

namespace
{
constexpr char kConfigPathName[] = "Data/SkyrimTogetherReborn/config";
constexpr char kSettingsFileName[] = "AlertedHostileFactions.ini";
constexpr char kSectionName[] = "AlertedHostileFactions";

bool TryParseFormId(std::string_view aValue, uint32_t& aOutFormId) noexcept
{
    unsigned long formId = 0;
    const auto* pBegin = aValue.data();
    const auto* pEnd = pBegin + aValue.size();
    const auto result = std::from_chars(pBegin, pEnd, formId, 16);
    if (result.ec != std::errc{} || result.ptr != pEnd)
        return false;
    aOutFormId = static_cast<uint32_t>(formId);
    return true;
}

std::string_view Trim(std::string_view aValue) noexcept
{
    constexpr std::string_view whitespace{" \t\r\n"};
    const auto start = aValue.find_first_not_of(whitespace);
    if (start == std::string_view::npos)
        return {};
    const auto end = aValue.find_last_not_of(whitespace);
    return aValue.substr(start, end - start + 1);
}

std::vector<uint32_t> LoadFormIds() noexcept
{
    std::vector<uint32_t> result;

    try
    {
        const std::filesystem::path path =
            std::filesystem::current_path() / kConfigPathName / kSettingsFileName;

        std::error_code error{};
        if (!std::filesystem::exists(path, error))
        {
            CSimpleIni ini;
            ini.SetUnicode();
            ini.SetValue(kSectionName, nullptr, nullptr,
                         "; Faction form IDs (hex) treated as alerted hostile at aggression 1.\n"
                         "; Add one per line. 0x1BCC0 = BanditFaction. Delete section to disable.\n"
                         "0x1BCC0=\n");

            std::string buf;
            if (ini.Save(buf, true) == SI_Error::SI_OK)
                TiltedPhoques::SaveFile(path, TiltedPhoques::String(buf));
        }

        if (error || !std::filesystem::exists(path, error))
            return result;

        std::string content = TiltedPhoques::LoadFile(path);
        CSimpleIni ini;
        ini.SetUnicode();
        if (ini.LoadData(content.c_str()) != SI_Error::SI_OK)
            return result;

        const auto* pSection = ini.GetSection(kSectionName);
        if (!pSection)
            return result;

        for (const auto& entry : *pSection)
        {
            if (!entry.first.pItem || !entry.second)
                continue;

            uint32_t formId = 0;
            if (TryParseFormId(Trim(entry.second), formId))
                result.push_back(formId);
        }
    }
    catch (...)
    {
    }

    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}
}

namespace AlertedHostileFactions
{
const std::vector<uint32_t>& GetAlertedHostileFactionIds() noexcept
{
    static const std::vector<uint32_t> s_ids = LoadFormIds();
    return s_ids;
}

bool IsAlertedHostileFaction(const Actor* apActor) noexcept
{
    if (!apActor)
        return false;

    auto* pNpc = Cast<TESNPC>(apActor->baseForm);
    if (!pNpc)
        return false;

    const auto& factions = pNpc->actorData.factions;
    const auto& ids = GetAlertedHostileFactionIds();

    for (uint32_t i = 0; i < factions.length; ++i)
    {
        const auto* pFaction = factions[i].faction;
        if (!pFaction)
            continue;

        if (std::binary_search(ids.begin(), ids.end(), pFaction->formID))
            return true;
    }
    return false;
}
}
