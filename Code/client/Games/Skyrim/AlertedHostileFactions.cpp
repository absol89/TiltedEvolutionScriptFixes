#include "AlertedHostileFactions.h"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <string_view>

#include <Forms/TESFaction.h>
#include <Forms/TESNPC.h>
#include <Games/Primitives.h>

namespace
{
constexpr char kConfigPath[] = "Data/SkyrimTogetherReborn/config/AlertedHostileFactions.ini";

std::vector<uint32_t> LoadFormIds()
{
    std::vector<uint32_t> result;

    std::ifstream file(kConfigPath);
    if (!file.is_open())
    {
        std::error_code error;
        std::filesystem::create_directories(std::filesystem::path(kConfigPath).parent_path(), error);
        if (!error)
        {
            {
                std::ofstream defaults(kConfigPath);
                defaults << "; Aggression-1 factions to wake after localization.\n0x1BCC0\n";
            }
        }

        file.clear();
        file.open(kConfigPath);
        if (!file.is_open())
            return result;
    }

    std::string line;
    while (std::getline(file, line))
    {
        const auto first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos)
            continue;

        const auto last = line.find_last_not_of(" \t\r");
        const auto trimmed = std::string_view(line.data() + first, last - first + 1);
        if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#')
            continue;

        if (trimmed.starts_with('[') || trimmed.starts_with("0x") == false)
            continue;

        uint32_t formId = 0;
        const auto* pBegin = trimmed.data() + 2;
        const auto* pEnd = trimmed.data() + trimmed.size();
        const auto [pParsedEnd, error] = std::from_chars(pBegin, pEnd, formId, 16);
        if (error == std::errc{} && pParsedEnd == pEnd)
            result.push_back(static_cast<uint32_t>(formId));
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
