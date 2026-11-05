#include <RemotePerks.h>

#include <Actor.h>
#include <PlayerCharacter.h>
#include <World.h>

#include <Games/ActorExtension.h>

#include <algorithm>
#include <shared_mutex>

namespace
{
TP_THIS_FUNCTION(TVisitPerks, void, Actor, PerkRankVisitor* apVisitor);
TP_THIS_FUNCTION(THasPerkEntries, bool, Actor, uint8_t aEntryPoint);
TP_THIS_FUNCTION(TForEachPerkEntry, void, Actor, uint8_t aEntryPoint, PerkEntryVisitor* apVisitor);

TVisitPerks* RealVisitPerks = nullptr;
THasPerkEntries* RealHasPerkEntries = nullptr;
TForEachPerkEntry* RealForEachPerkEntry = nullptr;

// Hits and spells can resolve on engine worker threads, so readers take a reference to an immutable list.
std::shared_mutex s_perksLock;
RemotePerks::PerkMap s_perks;

thread_local uint32_t s_scopeDepth = 0;
thread_local uint32_t s_ignoreDepth = 0;

RemotePerks::PerkDataPtr GetRemotePlayerPerks(Actor* apActor) noexcept
{
    if (s_ignoreDepth > 0 || !apActor->GetExtension()->IsRemotePlayer())
        return nullptr;

    if (!World::Get().GetServerSettings().PvpEnabled)
        return nullptr;

    std::shared_lock lock(s_perksLock);

    const auto it = s_perks.find(apActor->formID);
    return it != s_perks.end() ? it->second : nullptr;
}

// Entry points on the attacker's side of a hit take the target as a filter argument. Everything else, such as the
// attacker's stamina costs or weapon speed, is simulated by the client that owns the attacker.
bool TargetsLocalPlayer(PerkEntryVisitor* apVisitor) noexcept
{
    POINTER_SKYRIMSE(uint8_t, s_handleEntryPointVisitorVtable, 195172);

    if (*reinterpret_cast<void**>(apVisitor) != s_handleEntryPointVisitorVtable.Get())
        return false;

    const auto* pVisitor = static_cast<const HandleEntryPointVisitor*>(apVisitor);
    const void* pPlayer = PlayerCharacter::Get();

    for (uint32_t i = 1; i < pVisitor->argumentCount; ++i)
    {
        if (pVisitor->ppArguments[i] == pPlayer)
            return true;
    }

    return false;
}

void TP_MAKE_THISCALL(HookVisitPerks, Actor, PerkRankVisitor* apVisitor)
{
    if (s_scopeDepth > 0)
    {
        if (const auto pData = GetRemotePlayerPerks(apThis))
        {
            for (const PerkRankData& cPerk : pData->Perks)
            {
                if (apVisitor->Visit(const_cast<PerkRankData*>(&cPerk)) != 1)
                    break;
            }
            return;
        }
    }

    TiltedPhoques::ThisCall(RealVisitPerks, apThis, apVisitor);
}

bool TP_MAKE_THISCALL(HookHasPerkEntries, Actor, uint8_t aEntryPoint)
{
    if (aEntryPoint < BGSEntryPointPerkEntry::kEntryPointCount)
    {
        if (const auto pData = GetRemotePlayerPerks(apThis))
            return !pData->Entries[aEntryPoint].empty();
    }

    return TiltedPhoques::ThisCall(RealHasPerkEntries, apThis, aEntryPoint);
}

void TP_MAKE_THISCALL(HookForEachPerkEntry, Actor, uint8_t aEntryPoint, PerkEntryVisitor* apVisitor)
{
    if (aEntryPoint < BGSEntryPointPerkEntry::kEntryPointCount)
    {
        if (const auto pData = GetRemotePlayerPerks(apThis))
        {
            if (TargetsLocalPlayer(apVisitor))
            {
                // Perk conditions can check the attacker's perks
                ScopedRemotePerks remotePerks;

                for (BGSPerkEntry* pEntry : pData->Entries[aEntryPoint])
                {
                    if (apVisitor->Visit(pEntry) != 1)
                        break;
                }
            }
            return;
        }
    }

    TiltedPhoques::ThisCall(RealForEachPerkEntry, apThis, aEntryPoint, apVisitor);
}
} // namespace

ScopedRemotePerks::ScopedRemotePerks() noexcept
{
    ++s_scopeDepth;
}

ScopedRemotePerks::~ScopedRemotePerks() noexcept
{
    --s_scopeDepth;
}

ScopedIgnoreRemotePerks::ScopedIgnoreRemotePerks() noexcept
{
    ++s_ignoreDepth;
}

ScopedIgnoreRemotePerks::~ScopedIgnoreRemotePerks() noexcept
{
    --s_ignoreDepth;
}

namespace RemotePerks
{
PerkDataPtr BuildPerkData(const TiltedPhoques::Vector<PlayerPerk>& acPerks) noexcept
{
    auto pData = std::make_shared<PerkData>();
    auto& modSystem = World::Get().GetModSystem();

    for (const PlayerPerk& cPerk : acPerks)
    {
        const uint32_t cPerkId = modSystem.GetGameId(cPerk.Id);
        BGSPerk* pPerk = cPerkId ? Cast<BGSPerk>(TESForm::GetById(cPerkId)) : nullptr;
        if (!pPerk)
        {
            spdlog::debug("{}: skipping unknown perk, GameId base: {:X}, mod: {:X}", __FUNCTION__, cPerk.Id.BaseId, cPerk.Id.ModId);
            continue;
        }

        pData->Perks.push_back(PerkRankData{pPerk, cPerk.Rank});

        for (BGSPerkEntry* pEntry : pPerk->perkEntries)
        {
            // BGSPerk::ApplyPerk only applies the entries of the rank the perk is at.
            if (!pEntry || pEntry->rank + 1 != cPerk.Rank || pEntry->GetType() != BGSPerkEntry::kEntryPoint)
                continue;

            const uint8_t cEntryPoint = static_cast<BGSEntryPointPerkEntry*>(pEntry)->entryPoint;
            if (cEntryPoint >= BGSEntryPointPerkEntry::kEntryPointCount)
                continue;

            // Same order as AIProcess::AddPerkEntry: highest priority first, then lowest rank.
            auto& entries = pData->Entries[cEntryPoint];
            const auto cPosition = std::upper_bound(entries.begin(), entries.end(), pEntry, [](const BGSPerkEntry* apLhs, const BGSPerkEntry* apRhs) {
                if (apLhs->priority != apRhs->priority)
                    return apLhs->priority > apRhs->priority;
                return apLhs->rank < apRhs->rank;
            });
            entries.insert(cPosition, pEntry);
        }
    }

    return pData;
}

void SetPerks(PerkMap&& aPerks) noexcept
{
    std::unique_lock lock(s_perksLock);
    s_perks.swap(aPerks);
}

void CollectLocalPlayerPerks(TiltedPhoques::Vector<PlayerPerk>& aPerks) noexcept
{
    struct Visitor final : PerkRankVisitor
    {
        explicit Visitor(TiltedPhoques::Vector<PlayerPerk>& aPerks) noexcept
            : Perks(aPerks)
        {
        }

        uint32_t Visit(PerkRankData* apPerkRank) override
        {
            PlayerPerk perk{};
            if (apPerkRank && apPerkRank->pPerk && apPerkRank->rank > 0 && World::Get().GetModSystem().GetServerModId(apPerkRank->pPerk->formID, perk.Id))
            {
                perk.Rank = apPerkRank->rank;
                Perks.push_back(perk);
            }
            return 1;
        }

        TiltedPhoques::Vector<PlayerPerk>& Perks;
    } visitor(aPerks);

    // PlayerCharacter::VisitPerks: the base form's perks, then the earned ones.
    POINTER_SKYRIMSE(TVisitPerks, s_visitPlayerPerks, 40680);
    TiltedPhoques::ThisCall(s_visitPlayerPerks.Get(), static_cast<Actor*>(PlayerCharacter::Get()), static_cast<PerkRankVisitor*>(&visitor));
}
} // namespace RemotePerks

static TiltedPhoques::Initializer s_remotePerkHooks([]() {
    POINTER_SKYRIMSE(TVisitPerks, visitPerks, 37693);
    POINTER_SKYRIMSE(THasPerkEntries, hasPerkEntries, 37701);
    POINTER_SKYRIMSE(TForEachPerkEntry, forEachPerkEntry, 37702);

    RealVisitPerks = visitPerks.Get();
    RealHasPerkEntries = hasPerkEntries.Get();
    RealForEachPerkEntry = forEachPerkEntry.Get();

    TP_HOOK(&RealVisitPerks, HookVisitPerks);
    TP_HOOK(&RealHasPerkEntries, HookHasPerkEntries);
    TP_HOOK(&RealForEachPerkEntry, HookForEachPerkEntry);
});
