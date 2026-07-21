#include "AlertedHostileFactions.h"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

#include <Forms/TESFaction.h>
#include <Forms/TESNPC.h>
#include <Games/Primitives.h>

namespace
{
constexpr char kConfigPath[] = "Data/SkyrimTogetherReborn/config/AlertedHostileFactions.ini";

std::string_view Trim(std::string_view aValue)
{
    const auto first = aValue.find_first_not_of(" \t\r");
    if (first == std::string_view::npos)
        return {};

    const auto last = aValue.find_last_not_of(" \t\r");
    return aValue.substr(first, last - first + 1);
}

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
                defaults << R"(; ; Humanoid base-NPC faction form IDs (hex) whose Aggression=1 actors wake into combat search early.
; Actors with Aggression >= 2 wake regardless of faction. Use humanoid factions only; not creatures.
; One faction per line, format: 0x<formID>=
;   0x1BCC0= ; BanditFaction
;   0x1E2A3= ; BanditMages
;   0x43599= ; ForswornFaction
;   0x26724= ; WarlockFaction
;   0x34B74= ; NecromancerFaction
;   0x28849= ; StormCloaks
;   0x2BF9A= ; ImperialLegion
;   0x39F26= ; Thalmor
;   0xB3292= ; VigilantsOfStendarr
; Delete all entries below to disable Aggression=1 faction waking. Friendly factions are testable.
; Adding friendly factions to this list will make them suspicious, like when entering their towns.
[AlertedHostileFactions]
0x1BCC0=
0x1E2A3=
0x43599=
0x26724=
0x34B74=
)";
            }
        }

        file.clear();
        file.open(kConfigPath);
        if (!file.is_open())
        {
            spdlog::warn("Alerted hostile factions: failed to open {}", kConfigPath);
            return result;
        }
    }

    std::string line;
    while (std::getline(file, line))
    {
        const auto trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#')
            continue;

        const auto formIdText = Trim(trimmed.substr(0, trimmed.find('=')));
        if (formIdText.starts_with('[') || formIdText.starts_with("0x") == false)
            continue;

        uint32_t formId = 0;
        const auto* pBegin = formIdText.data() + 2;
        const auto* pEnd = formIdText.data() + formIdText.size();
        const auto [pParsedEnd, error] = std::from_chars(pBegin, pEnd, formId, 16);
        if (error == std::errc{} && pParsedEnd == pEnd)
            result.push_back(static_cast<uint32_t>(formId));
    }

    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    spdlog::info("Alerted hostile factions: loaded {} entries from {}", result.size(), kConfigPath);
    return result;
}

const std::vector<uint32_t>& GetAlertedHostileFactionIds() noexcept
{
    static const std::vector<uint32_t> s_ids = LoadFormIds();
    return s_ids;
}
}

namespace AlertedHostileFactions
{
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
