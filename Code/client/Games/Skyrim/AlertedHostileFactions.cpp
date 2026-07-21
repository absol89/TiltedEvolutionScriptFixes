#include "AlertedHostileFactions.h"

#include <algorithm>
#include <fstream>
#include <string_view>

#include <Forms/TESFaction.h>
#include <Forms/TESNPC.h>
#include <Games/Primitives.h>

namespace
{
constexpr char kConfigPath[] = "Data/SkyrimTogetherReborn/config/AlertedHostileFactions.ini";
constexpr uint32_t kDefaultFactionId = 0x1BCC0u;

std::vector<uint32_t> LoadFormIds()
{
    std::vector<uint32_t> result;

    std::ifstream file(kConfigPath);
    if (!file.is_open())
        return result;

    std::string line;
    while (std::getline(file, line))
    {
        const auto trimmed = std::string_view(line.data(), line.size());
        if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#')
        {
            continue;
        }

        if (trimmed.starts_with('[') || trimmed.starts_with("0x") == false)
        {
            continue;
        }

        unsigned long formId = 0;
        const auto* pBegin = trimmed.data() + 2;
        const auto* pEnd = trimmed.data() + trimmed.size();
        if (std::from_chars(pBegin, pEnd, formId, 16).ec == std::errc{})
        {
            result.push_back(static_cast<uint32_t>(formId));
        }
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
