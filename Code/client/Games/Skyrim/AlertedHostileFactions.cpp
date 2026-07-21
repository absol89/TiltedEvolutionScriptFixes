#include "AlertedHostileFactions.h"

#include <spdlog/spdlog.h>
#include <fstream>

#include <Forms/TESActorBase.h>
#include <Forms/TESFaction.h>

TP_THIS_FUNCTION(static bool, IsKnownHostileHumanoidFaction, TESNPC*, TESFaction*);

static TIsKnownHostileHumanoidFaction* RealIsKnownHostileHumanoidFaction = nullptr;

namespace
{
void EnsureDefaultIniExists()
{
    const std::filesystem::path iniPath = TiltedPhoques::GetAppDataPath() / "AlertedHostileFactions.ini";
    if (!std::filesystem::exists(iniPath))
    {
        std::ofstream file(iniPath);
        if (file.is_open())
        {
            file << R"([AlertedHostileFactions]
; Faction form IDs (hex) that should be treated as awakened at aggression 1.
; Add one form ID per line. Empty/deleted entries leave those bandits untouched.
0x1BCC0=
)";
            spdlog::info("AlertedHostileFactions.ini created at {}", iniPath.string());
        }
    }
}

std::vector<TESFaction*> LoadAlertedFactions()
{
    EnsureDefaultIniExists();

    std::vector<TESFaction*> factions;
    CSimpleIniTempl<char, SI_NoCase<char>, SI_ConvertA<char>> ini;
    ini.SetUnicode();
    ini.LoadFile((TiltedPhoques::GetAppDataPath() / "AlertedHostileFactions.ini").string().c_str());

    const char* pszSection = "AlertedHostileFactions";
    CSimpleIniTempl<char, SI_NoCase<char>, SI_ConvertA<char>>::TNamesDepend sectionKeys;
    ini.GetAllValues(pszSection, nullptr, sectionKeys);

    for (auto& key : sectionKeys)
    {
        const uint32_t formId = std::strtoul(key.pItem, nullptr, 16);
        if (auto* pFaction = Cast<TESFaction>(TESForm::GetById(formId)))
            factions.push_back(pFaction);
    }

    spdlog::info("AlertedHostileFactions loaded {} factions", factions.size());
    return factions;
}
}

namespace AlertedHostileFactions
{
std::vector<TESFaction*> GetAlertedFactions()
{
    static std::vector<TESFaction*> factions = LoadAlertedFactions();
    return factions;
}

bool IsAlertedHostileFaction(TESActorBase* apActorBase)
{
    if (!apActorBase)
        return false;

    for (auto* pFaction : GetAlertedFactions())
    {
        if (apActorBase->IsInFaction(pFaction))
            return true;
    }
    return false;
}

bool IsAlertedHostileFaction(TESForm* apForm)
{
    if (auto* pActorBase = Cast<TESActorBase>(apForm))
        return IsAlertedHostileFaction(pActorBase);
    return false;
}
}
