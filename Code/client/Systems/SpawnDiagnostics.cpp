#include <TiltedOnlinePCH.h>

#include <Systems/SpawnDiagnostics.h>

#include <World.h>
#include <Components.h>
#include <Actor.h>
#include <PlayerCharacter.h>
#include <Games/ActorExtension.h>
#include <Forms/TESNPC.h>
#include <Forms/BGSOutfit.h>
#include <Forms/TESRace.h>
#include <Forms/TESObjectCELL.h>
#include <Structs/Inventory.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace SpawnDiagnostics
{
namespace
{
using Clock = std::chrono::steady_clock;

constexpr auto kScanInterval = std::chrono::seconds(5);
constexpr auto kPuppetInterval = std::chrono::seconds(2);
// Lookalikes closer than this to each other are reported.
constexpr float kLookalikeDistance = 1500.f;
// How far around a new copy to look for lookalikes when it spawns.
constexpr float kSpawnLookDistance = 4096.f;

struct SpawnInfo
{
    uint32_t ServerId = 0;
    bool FromCustomForm = false;
};

struct PuppetInfo
{
    uint32_t FormId = 0;
    uint32_t BaseId = 0;
    uint32_t RaceId = 0;
    uint32_t OutfitId = 0;
    bool Reported = false;
};

// Copies this client created for other clients' NPCs, by form id.
std::unordered_map<uint32_t, SpawnInfo> s_spawned;
// Remote player puppets as they were when first seen, by server id.
std::unordered_map<uint32_t, PuppetInfo> s_puppets;
// Lookalike groups and shared references already reported, so each is logged once.
std::unordered_set<uint64_t> s_reported;

uint32_t FormIdOf(const TESForm* apForm) noexcept
{
    return apForm ? apForm->formID : 0;
}

const char* NameOf(const TESForm* apForm) noexcept
{
    const char* cpName = apForm ? apForm->GetName() : nullptr;
    return cpName && *cpName ? cpName : "?";
}

TESNPC* BaseOf(const Actor* apActor) noexcept
{
    return apActor && apActor->baseForm && apActor->baseForm->formType == FormType::Npc ? static_cast<TESNPC*>(apActor->baseForm) : nullptr;
}

// What kind of NPC this is: its leveled pick, or the plugin base behind its (temporary) base. 0 for players and NPCs
// built from scratch from another client's save data, which have nothing to compare.
uint32_t IdentityOf(const Actor* apActor) noexcept
{
    if (const TESNPC* pPick = apActor->GetLeveledPick())
        return pPick->formID;

    const TESNPC* pBase = BaseOf(apActor);
    if (!pBase)
        return 0;

    if (!pBase->IsTemporary())
        return pBase->formID;

    const TESNPC* pTemplate = pBase->GetTemplateBase();
    return pTemplate ? pTemplate->formID : 0;
}

float Distance(const Actor* apA, const Actor* apB) noexcept
{
    const float cX = apA->position.x - apB->position.x;
    const float cY = apA->position.y - apB->position.y;
    const float cZ = apA->position.z - apB->position.z;
    return std::sqrt(cX * cX + cY * cY + cZ * cZ);
}

struct Tracked
{
    entt::entity Entity{};
    Actor* pActor = nullptr;
};

std::string Describe(World& aWorld, const Tracked& acTracked)
{
    Actor* pActor = acTracked.pActor;
    ActorExtension* pExtension = pActor->GetExtension();

    std::string owner = "untracked";
    if (const auto* pLocal = aWorld.try_get<LocalComponent>(acTracked.Entity))
        owner = fmt::format("local, server id {:X}", pLocal->Id);
    else if (const auto* pRemote = aWorld.try_get<RemoteComponent>(acTracked.Entity))
        owner = fmt::format("remote, server id {:X}", pRemote->Id);

    std::string spawned;
    if (const auto it = s_spawned.find(pActor->formID); it != s_spawned.end())
        spawned = fmt::format(", spawned here for server id {:X}{}", it->second.ServerId, it->second.FromCustomForm ? " as a new actor" : "");

    const TESNPC* pBase = BaseOf(pActor);
    const TESObjectCELL* pCell = pActor->GetParentCellEx();
    return fmt::format("{}({:X}{}) base {:X}{} pick {:X} [{}{}{}{}] cell {:X} at ({:.0f}, {:.0f}, {:.0f}){}", NameOf(pBase), pActor->formID,
                       pActor->IsTemporary() ? ", runtime ref" : "", FormIdOf(pBase), pBase && pBase->IsTemporary() ? " (runtime)" : "",
                       FormIdOf(pActor->GetLeveledPick()), owner, pExtension && pExtension->IsPlayer() ? ", player" : "", pActor->IsDead() ? ", dead" : "",
                       pActor->IsDisabled() ? ", disabled" : "", FormIdOf(pCell), pActor->position.x, pActor->position.y, pActor->position.z, spawned);
}

uint64_t Hash(Vector<uint32_t> aIds) noexcept
{
    std::sort(aIds.begin(), aIds.end());
    uint64_t hash = 1469598103934665603ull;
    for (const uint32_t cId : aIds)
    {
        hash ^= cId;
        hash *= 1099511628211ull;
    }
    return hash;
}

Vector<Tracked> CollectActors(World& aWorld) noexcept
{
    Vector<Tracked> actors;
    auto view = aWorld.view<FormIdComponent>();
    for (const auto entity : view)
    {
        Actor* pActor = Cast<Actor>(TESForm::GetById(view.get<FormIdComponent>(entity).Id));
        if (pActor && pActor->GetExtension())
            actors.push_back({entity, pActor});
    }
    return actors;
}

void CheckSharedReferences(World& aWorld, const Vector<Tracked>& acActors) noexcept
{
    std::unordered_map<uint32_t, const Tracked*> byForm;
    for (const auto& tracked : acActors)
    {
        const auto [it, inserted] = byForm.emplace(tracked.pActor->formID, &tracked);
        if (inserted)
            continue;

        const uint64_t cKey = Hash({tracked.pActor->formID, 0xFFFFFFFFu});
        if (!s_reported.insert(cKey).second)
            continue;

        spdlog::warn("[SpawnDiag] FLAG two entities share one reference: {} | {}", Describe(aWorld, *it->second), Describe(aWorld, tracked));
    }
}

void CheckLookalikes(World& aWorld, const Vector<Tracked>& acActors) noexcept
{
    std::unordered_map<uint32_t, Vector<const Tracked*>> byIdentity;
    for (const auto& tracked : acActors)
    {
        Actor* pActor = tracked.pActor;
        if (pActor->GetExtension()->IsPlayer() || pActor->IsDisabled() || !pActor->GetNiNode())
            continue;

        if (const uint32_t cIdentity = IdentityOf(pActor))
            byIdentity[cIdentity].push_back(&tracked);
    }

    for (const auto& [cIdentity, members] : byIdentity)
    {
        if (members.size() < 2)
            continue;

        // Plugin references of the same kind (a town's guards) are expected. A runtime reference among them is not
        // always a duplicate either (two bandits from one leveled list), so the log gives both sides to compare.
        Vector<const Tracked*> group;
        for (size_t i = 0; i < members.size(); ++i)
        {
            for (size_t j = i + 1; j < members.size(); ++j)
            {
                if (!members[i]->pActor->IsTemporary() && !members[j]->pActor->IsTemporary())
                    continue;
                if (Distance(members[i]->pActor, members[j]->pActor) > kLookalikeDistance)
                    continue;

                if (std::find(group.begin(), group.end(), members[i]) == group.end())
                    group.push_back(members[i]);
                if (std::find(group.begin(), group.end(), members[j]) == group.end())
                    group.push_back(members[j]);
            }
        }

        if (group.size() < 2)
            continue;

        Vector<uint32_t> ids;
        for (const auto* pTracked : group)
            ids.push_back(pTracked->pActor->formID);
        if (!s_reported.insert(Hash(ids)).second)
            continue;

        const TESForm* pIdentity = TESForm::GetById(cIdentity);
        const bool cHasRemote = std::any_of(group.begin(), group.end(), [&aWorld](const Tracked* apTracked) { return aWorld.all_of<RemoteComponent>(apTracked->Entity); });
        const bool cHasLocal = std::any_of(group.begin(), group.end(), [&aWorld](const Tracked* apTracked) { return aWorld.all_of<LocalComponent>(apTracked->Entity); });

        spdlog::warn("[SpawnDiag] {}{} {} lookalikes of {}({:X}) within {:.0f} units{}", cHasRemote && cHasLocal ? "FLAG " : "", group.size(),
                     cHasRemote && cHasLocal ? "local and remote" : "", NameOf(pIdentity), cIdentity, kLookalikeDistance,
                     cHasRemote && cHasLocal ? ", this client runs one and another client runs the other" : "");
        for (const auto* pTracked : group)
            spdlog::warn("[SpawnDiag]   {}", Describe(aWorld, *pTracked));
    }
}

void CheckPuppets(World& aWorld) noexcept
{
    auto view = aWorld.view<PlayerComponent, RemoteComponent, FormIdComponent>();
    for (const auto entity : view)
    {
        const uint32_t cServerId = view.get<RemoteComponent>(entity).Id;
        const uint32_t cPlayerId = view.get<PlayerComponent>(entity).Id;
        Actor* pActor = Cast<Actor>(TESForm::GetById(view.get<FormIdComponent>(entity).Id));
        const TESNPC* pBase = BaseOf(pActor);
        if (!pActor || !pBase)
            continue;

        PuppetInfo now{pActor->formID, pBase->formID, FormIdOf(pBase->raceForm.race), FormIdOf(pBase->defaultOutfit)};
        auto [it, inserted] = s_puppets.emplace(cServerId, now);
        PuppetInfo& seen = it->second;

        if (inserted)
        {
            spdlog::info("[SpawnDiag] remote player {} puppet {:X}, base {:X}, race {}({:X}), outfit {:X}, server id {:X}", cPlayerId, now.FormId, now.BaseId,
                         NameOf(pBase->raceForm.race), now.RaceId, now.OutfitId, cServerId);
            if (!pActor->IsTemporary() || !pBase->IsTemporary())
            {
                spdlog::warn("[SpawnDiag] FLAG remote player {} uses a game NPC as its puppet: {}", cPlayerId, Describe(aWorld, {entity, pActor}));
                seen.Reported = true;
            }
            continue;
        }

        if (now.FormId == seen.FormId && now.BaseId == seen.BaseId && now.RaceId == seen.RaceId && now.OutfitId == seen.OutfitId)
            continue;

        spdlog::warn("[SpawnDiag] FLAG remote player {} puppet changed: form {:X} -> {:X}, base {:X} -> {:X}, race {:X} -> {:X}, outfit {:X} -> {:X}, now {}",
                     cPlayerId, seen.FormId, now.FormId, seen.BaseId, now.BaseId, seen.RaceId, now.RaceId, seen.OutfitId, now.OutfitId,
                     Describe(aWorld, {entity, pActor}));
        seen = now;
    }
}

Vector<uint32_t> WornArmor(const Inventory& acInventory) noexcept
{
    Vector<uint32_t> armor;
    auto& modSystem = World::Get().GetModSystem();
    for (const auto& entry : acInventory.Entries)
    {
        if (!entry.ExtraWorn && !entry.ExtraWornLeft)
            continue;

        const uint32_t cId = modSystem.GetGameId(entry.BaseId);
        const TESForm* pForm = cId ? TESForm::GetById(cId) : nullptr;
        if (pForm && pForm->formType == FormType::Armor)
            armor.push_back(cId);
    }
    std::sort(armor.begin(), armor.end());
    return armor;
}

std::string ListForms(const Vector<uint32_t>& acIds)
{
    std::string list;
    for (const uint32_t cId : acIds)
        list += fmt::format("{}{}({:X})", list.empty() ? "" : ", ", NameOf(TESForm::GetById(cId)), cId);
    return list.empty() ? "nothing" : list;
}
} // namespace

void Update() noexcept
{
    static Clock::time_point s_lastScan{};
    static Clock::time_point s_lastPuppets{};
    const auto cNow = Clock::now();

    World& world = World::Get();

    if (cNow - s_lastPuppets >= kPuppetInterval)
    {
        s_lastPuppets = cNow;
        CheckPuppets(world);
    }

    if (cNow - s_lastScan < kScanInterval)
        return;
    s_lastScan = cNow;

    const auto cActors = CollectActors(world);
    CheckSharedReferences(world, cActors);
    CheckLookalikes(world, cActors);

    // Forget copies that are gone, so a recycled form id isn't labelled with an old spawn.
    for (auto it = s_spawned.begin(); it != s_spawned.end();)
    {
        if (!Cast<Actor>(TESForm::GetById(it->first)))
            it = s_spawned.erase(it);
        else
            ++it;
    }
}

void OnRemoteSpawned(Actor* apActor, uint32_t aServerId, bool aFromCustomForm, bool aIsPlayer) noexcept
{
    if (!apActor)
        return;

    s_spawned[apActor->formID] = {aServerId, aFromCustomForm};
    if (aIsPlayer)
        return;

    World& world = World::Get();
    const uint32_t cIdentity = IdentityOf(apActor);
    const TESNPC* pBase = BaseOf(apActor);
    spdlog::info("[SpawnDiag] spawned copy {:X} of server id {:X}{}, {}({:X}), base {:X}{}, pick {:X}", apActor->formID, aServerId,
                 aFromCustomForm ? " as a new actor (the owner's NPC is a runtime reference)" : "", NameOf(pBase), cIdentity, FormIdOf(pBase),
                 pBase && pBase->IsTemporary() ? " (runtime)" : "", FormIdOf(apActor->GetLeveledPick()));

    if (!cIdentity)
        return;

    for (const auto& tracked : CollectActors(world))
    {
        Actor* pOther = tracked.pActor;
        if (pOther == apActor || pOther->GetExtension()->IsPlayer() || IdentityOf(pOther) != cIdentity || pOther->IsDead())
            continue;

        const float cDistance = Distance(apActor, pOther);
        if (cDistance > kSpawnLookDistance)
            continue;

        spdlog::warn("[SpawnDiag] {}new copy {:X} of server id {:X} looks like {} already here, {:.0f} units away",
                     world.all_of<LocalComponent>(tracked.Entity) && pOther->IsTemporary() ? "FLAG " : "", apActor->formID, aServerId,
                     Describe(world, tracked), cDistance);
    }
}

BaseSnapshot SnapshotBase(const TESNPC* apNpc) noexcept
{
    if (!apNpc)
        return {};
    return {FormIdOf(apNpc->raceForm.race), FormIdOf(apNpc->defaultOutfit), FormIdOf(apNpc->faceNPC)};
}

void OnBaseAppearanceLoaded(const TESNPC* apNpc, const BaseSnapshot& acBefore, uint32_t aServerId, uint32_t aChangeFlags) noexcept
{
    if (!apNpc)
        return;

    const BaseSnapshot cAfter = SnapshotBase(apNpc);

    // Every actor here that uses this base gets the other client's appearance too.
    Vector<std::string> sharers;
    World& world = World::Get();
    for (const auto& tracked : CollectActors(world))
    {
        if (tracked.pActor->baseForm == apNpc)
            sharers.push_back(fmt::format("{:X}", tracked.pActor->formID));
    }

    const bool cChanged = cAfter.RaceId != acBefore.RaceId || cAfter.OutfitId != acBefore.OutfitId || cAfter.FaceNpcId != acBefore.FaceNpcId;
    const bool cShared = !apNpc->IsTemporary();

    std::string sharerList;
    for (const auto& id : sharers)
        sharerList += (sharerList.empty() ? "" : ", ") + id;

    const auto cLevel = cShared ? spdlog::level::warn : spdlog::level::info;
    spdlog::log(cLevel, "[SpawnDiag] {}server id {:X} loaded another client's appearance onto base {}({:X}){}, change flags {:X}, race {:X} -> {:X}, outfit {:X} -> {:X}, face npc {:X} -> {:X}, actors using it here: {}",
                cShared && (cChanged || !sharers.empty()) ? "FLAG " : "", aServerId, NameOf(apNpc), apNpc->formID, cShared ? " (a plugin base other NPCs share)" : "",
                aChangeFlags, acBefore.RaceId, cAfter.RaceId, acBefore.OutfitId, cAfter.OutfitId, acBefore.FaceNpcId, cAfter.FaceNpcId,
                sharerList.empty() ? "none" : sharerList);
}

void OnInventoryApplied(Actor* apActor, const Inventory& acCurrent, const Inventory& acNew) noexcept
{
    if (!apActor || !apActor->GetExtension() || apActor->GetExtension()->IsPlayer())
        return;

    const auto cBefore = WornArmor(acCurrent);
    const auto cAfter = WornArmor(acNew);
    if (cBefore == cAfter || cBefore.empty())
        return;

    const TESNPC* pBase = BaseOf(apActor);
    spdlog::warn("[SpawnDiag] {} {}({:X}) base {:X} pick {:X}: worn armor {} -> {}", apActor->GetExtension()->IsRemote() ? "remote NPC" : "local NPC",
                 NameOf(pBase), apActor->formID, FormIdOf(pBase), FormIdOf(apActor->GetLeveledPick()), ListForms(cBefore), ListForms(cAfter));
}
} // namespace SpawnDiagnostics
