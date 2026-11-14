#include <TiltedOnlinePCH.h>

#include <Systems/DamageDiagnostics.h>

#include <World.h>
#include <Actor.h>
#include <PlayerCharacter.h>
#include <Games/ActorExtension.h>
#include <Forms/ActorValueInfo.h>

#include <algorithm>
#include <cmath>
#include <deque>
#include <mutex>

namespace DamageDiagnostics
{
namespace
{
using Clock = std::chrono::steady_clock;

// How long a hit is kept to match it with later events.
constexpr auto kHistory = std::chrono::milliseconds(3000);
// Two applications of the same hit closer than this are counted as one hit applied twice.
constexpr auto kSameHitWindow = std::chrono::milliseconds(150);
// A delta from another client this soon after a hit on the same actor is matched with that hit.
constexpr auto kMatchWindow = std::chrono::milliseconds(2000);
// Snapshots for the same player closer than this are not logged unless they drift.
constexpr auto kSnapshotLogInterval = std::chrono::milliseconds(5000);

struct HitRecord
{
    uint64_t Id = 0;
    Clock::time_point Time{};
    uint32_t HitterId = 0;
    uint32_t HitteeId = 0;
    float RawDamage = 0.f;
    float RealDamage = 0.f;
    bool Applied = false;
    bool Sent = false;
    bool InvolvesPlayer = false;
};

struct BroadcastRecord
{
    Clock::time_point Time{};
    uint32_t ServerId = 0;
    float Delta = 0.f;
};

std::mutex s_lock;
uint64_t s_nextHitId = 1;
std::deque<HitRecord> s_hits;
std::deque<BroadcastRecord> s_broadcasts;
std::unordered_map<uint32_t, Clock::time_point> s_lastSnapshotLog;

long long Ms(Clock::duration aDuration) noexcept
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(aDuration).count();
}

void Prune(Clock::time_point aNow) noexcept
{
    while (!s_hits.empty() && aNow - s_hits.front().Time > kHistory)
        s_hits.pop_front();
    while (!s_broadcasts.empty() && aNow - s_broadcasts.front().Time > kHistory)
        s_broadcasts.pop_front();
}

const char* Role(Actor* apActor) noexcept
{
    if (!apActor)
        return "none";
    ActorExtension* pExtension = apActor->GetExtension();
    if (!pExtension)
        return "no-ext";
    if (pExtension->IsLocalPlayer())
        return "local player";
    if (pExtension->IsRemotePlayer())
        return "remote player";
    if (pExtension->IsLocal())
        return "local npc";
    if (pExtension->IsRemote())
        return "remote npc";
    return "unsynced";
}

bool IsPlayer(Actor* apActor) noexcept
{
    return apActor && apActor->GetExtension() && apActor->GetExtension()->IsPlayer();
}

std::string Describe(Actor* apActor)
{
    if (!apActor)
        return "none";
    const char* cpName = apActor->baseForm ? apActor->baseForm->GetName() : nullptr;
    return fmt::format("{}({:X}, {})", cpName && *cpName ? cpName : "?", apActor->formID, Role(apActor));
}

std::string Context()
{
    const auto* pPlayer = PlayerCharacter::Get();
    const auto& party = World::Get().GetPartyService();
    const char* cpParty = !party.IsInParty() ? "none" : (party.IsLeader() ? "leader" : "member");
    return fmt::format("diff={} party={}", pPlayer ? pPlayer->difficulty : -1, cpParty);
}

HitRecord* FindHit(uint64_t aId) noexcept
{
    for (auto& hit : s_hits)
        if (hit.Id == aId)
            return &hit;
    return nullptr;
}

bool Close(float aA, float aB, float aTolerance) noexcept
{
    const float cScale = std::max(std::abs(aA), std::abs(aB));
    return std::abs(aA - aB) <= std::max(1.f, cScale * aTolerance);
}
} // namespace

uint64_t OnDamageHook(Actor* apHittee, Actor* apHitter, float aRawDamage, float aRealDamage, bool aKillMove, const char* acpPath, bool aApplied) noexcept
{
    if (!apHittee)
        return 0;

    std::scoped_lock lock(s_lock);
    const auto cNow = Clock::now();
    Prune(cNow);

    HitRecord record;
    record.Id = s_nextHitId++;
    record.Time = cNow;
    record.HitterId = apHitter ? apHitter->formID : 0;
    record.HitteeId = apHittee->formID;
    record.RawDamage = aRawDamage;
    record.RealDamage = aRealDamage;
    record.Applied = aApplied;

    // The same hit applied twice on this client within a few frames.
    const HitRecord* pTwin = nullptr;
    if (aApplied)
    {
        for (auto it = s_hits.rbegin(); it != s_hits.rend(); ++it)
        {
            if (cNow - it->Time > kSameHitWindow)
                break;
            if (it->Applied && it->HitteeId == record.HitteeId && it->HitterId == record.HitterId && Close(it->RawDamage, aRawDamage, 0.05f))
            {
                pTwin = &*it;
                break;
            }
        }
    }

    const bool cInvolvesPlayer = IsPlayer(apHittee) || IsPlayer(apHitter);
    record.InvolvesPlayer = cInvolvesPlayer;
    if (cInvolvesPlayer || pTwin)
    {
        const float cMultiplier = aRawDamage != 0.f ? aRealDamage / aRawDamage : 0.f;
        spdlog::info(
            "[DmgDiag] #{} HIT {} path={} hitter={} hittee={} raw={:.1f} real={:.1f} mult={:.2f} killMove={} hp={:.1f}/{:.1f} armor(DR)={:.0f} {}", record.Id,
            aApplied ? "applied" : "left-to-owner", acpPath, Describe(apHitter), Describe(apHittee), aRawDamage, aRealDamage, cMultiplier, aKillMove,
            apHittee->GetActorValue(ActorValueInfo::kHealth), apHittee->GetActorPermanentValue(ActorValueInfo::kHealth), apHittee->GetActorValue(ActorValueInfo::kDamageResist),
            Context());
    }

    if (pTwin)
        spdlog::warn("[DmgDiag] #{} FLAG DUP_LOCAL_HIT: same hitter, hittee and damage as #{} applied {} ms earlier", record.Id, pTwin->Id, Ms(cNow - pTwin->Time));

    s_hits.push_back(record);
    return record.Id;
}

void OnDamageApplied(uint64_t aHitId, Actor* apHittee, float aHealthBefore) noexcept
{
    if (!apHittee || aHitId == 0)
        return;

    std::scoped_lock lock(s_lock);
    const HitRecord* pHit = FindHit(aHitId);
    if (!pHit)
        return;

    const float cHealthAfter = apHittee->GetActorValue(ActorValueInfo::kHealth);
    const float cDrop = aHealthBefore - cHealthAfter;
    const bool cInvolvesPlayer = pHit->InvolvesPlayer;

    // What this client lost and what it tells the others should match, otherwise every other client sees a different number.
    const bool cMismatch = pHit->RealDamage >= 1.f && cHealthAfter > 0.f && !Close(cDrop, pHit->RealDamage, 0.25f);

    if (cInvolvesPlayer || cMismatch)
        spdlog::info("[DmgDiag] #{} APPLIED hp {:.1f} -> {:.1f} (game took {:.1f}, delta sent to others {:.1f})", aHitId, aHealthBefore, cHealthAfter, cDrop, -pHit->RealDamage);

    if (cMismatch)
        spdlog::warn(
            "[DmgDiag] #{} FLAG SENT_VS_APPLIED: game took {:.1f} but {:.1f} is sent to the other clients (ratio {:.2f})", aHitId, cDrop, pHit->RealDamage,
            cDrop != 0.f ? pHit->RealDamage / cDrop : 0.f);
}

void OnHealthChangeSent(uint32_t aServerId, uint32_t aFormId, float aDelta, bool aBatched) noexcept
{
    std::scoped_lock lock(s_lock);
    const auto cNow = Clock::now();

    // Match the newest applied hit on this actor that has not been sent yet.
    HitRecord* pHit = nullptr;
    for (auto it = s_hits.rbegin(); it != s_hits.rend(); ++it)
    {
        if (it->Applied && !it->Sent && it->HitteeId == aFormId && Close(-it->RealDamage, aDelta, 0.01f))
        {
            pHit = &*it;
            break;
        }
    }
    if (pHit)
        pHit->Sent = true;

    Actor* pActor = Cast<Actor>(TESForm::GetById(aFormId));
    if (!IsPlayer(pActor) && !(pHit && pHit->InvolvesPlayer))
        return;

    if (pHit)
        spdlog::info(
            "[DmgDiag] #{} SENT delta {:.1f} for {} server {:X}{} ({} ms after the hit)", pHit->Id, aDelta, Describe(pActor), aServerId, aBatched ? " batched" : "",
            Ms(cNow - pHit->Time));
    else if (IsPlayer(pActor))
        spdlog::info("[DmgDiag] SENT delta {:.1f} for {} server {:X}{} (not from a hit: heal, regen or batch)", aDelta, Describe(pActor), aServerId, aBatched ? " batched" : "");
}

void OnHealthBroadcastReceived(uint32_t aServerId, Actor* apActor, float aDelta, float aHealthBefore, float aHealthAfter) noexcept
{
    if (!apActor)
        return;

    std::scoped_lock lock(s_lock);
    const auto cNow = Clock::now();
    Prune(cNow);

    ActorExtension* pExtension = apActor->GetExtension();
    const bool cIsLocal = pExtension && pExtension->IsLocal();
    const bool cIsPlayer = IsPlayer(apActor);

    // The same delta for the same actor twice in a row.
    const BroadcastRecord* pTwin = nullptr;
    for (auto it = s_broadcasts.rbegin(); it != s_broadcasts.rend(); ++it)
    {
        if (cNow - it->Time > kSameHitWindow)
            break;
        if (it->ServerId == aServerId && Close(it->Delta, aDelta, 0.01f))
        {
            pTwin = &*it;
            break;
        }
    }
    s_broadcasts.push_back({cNow, aServerId, aDelta});

    // The newest hit this client saw on the same actor.
    const HitRecord* pHit = nullptr;
    for (auto it = s_hits.rbegin(); it != s_hits.rend(); ++it)
    {
        if (cNow - it->Time > kMatchWindow)
            break;
        if (it->HitteeId == apActor->formID)
        {
            pHit = &*it;
            break;
        }
    }

    if (cIsPlayer || cIsLocal || pTwin)
    {
        spdlog::info(
            "[DmgDiag] RECV delta {:.1f} for {} server {:X}, hp {:.1f} -> {:.1f}{} {}", aDelta, Describe(apActor), aServerId, aHealthBefore, aHealthAfter,
            pHit ? fmt::format(
                       ", last hit seen here #{} {} ms ago (real {:.1f}, {})", pHit->Id, Ms(cNow - pHit->Time), pHit->RealDamage, pHit->Applied ? "applied here" : "left to owner")
                 : std::string(),
            Context());
    }

    if (pTwin)
        spdlog::warn("[DmgDiag] FLAG DUP_BROADCAST: delta {:.1f} for server {:X} arrived twice within {} ms", aDelta, aServerId, Ms(cNow - pTwin->Time));

    if (cIsLocal && aDelta < 0.f)
    {
        // This client owns the actor and applies its hits itself, so another client's damage lands on top of its own.
        if (pHit && pHit->Applied && Close(pHit->RealDamage, -aDelta, 0.15f))
            spdlog::warn(
                "[DmgDiag] FLAG LIKELY_DOUBLE_DAMAGE: {} is owned here, hit #{} was already applied here for {:.1f} and another client sent {:.1f}", Describe(apActor), pHit->Id,
                pHit->RealDamage, aDelta);
        else
            spdlog::warn("[DmgDiag] FLAG DAMAGE_ON_OWNED_ACTOR: {} is owned here but another client's damage {:.1f} was applied to it", Describe(apActor), aDelta);
    }
    else if (pExtension && pExtension->IsRemotePlayer() && aDelta < 0.f && pHit && !pHit->Applied && pHit->RealDamage >= 1.f)
    {
        // This client estimated the hit and left it to the player's own client, compare what that client reported.
        const float cRatio = -aDelta / pHit->RealDamage;
        if (cRatio < 0.5f || cRatio > 2.f)
            spdlog::warn(
                "[DmgDiag] FLAG REMOTE_DAMAGE_DIFFERS: {} reported {:.1f} for hit #{}, this client estimated {:.1f} (ratio {:.2f}, armor differs per client)", Describe(apActor),
                -aDelta, pHit->Id, pHit->RealDamage, cRatio);
    }
}

void OnHealthSnapshot(uint32_t aServerId, Actor* apActor, float aReportedHealth) noexcept
{
    if (!apActor || !IsPlayer(apActor))
        return;

    std::scoped_lock lock(s_lock);
    const auto cNow = Clock::now();

    const float cLocal = apActor->GetActorValue(ActorValueInfo::kHealth);
    const float cMax = apActor->GetActorPermanentValue(ActorValueInfo::kHealth);
    const float cDrift = cLocal - aReportedHealth;
    const bool cDrifting = std::abs(cDrift) >= std::max(10.f, cMax * 0.1f);

    auto& last = s_lastSnapshotLog[aServerId];
    if (!cDrifting && cNow - last < kSnapshotLogInterval)
        return;
    last = cNow;

    spdlog::info(
        "[DmgDiag] SNAPSHOT {} owner says hp {:.1f}, local copy has {:.1f} (drift {:+.1f}, max {:.1f}) {}", Describe(apActor), aReportedHealth, cLocal, cDrift, cMax, Context());
    if (cDrifting)
        spdlog::warn("[DmgDiag] FLAG HEALTH_DRIFT: local copy of {} is {:+.1f} off the owner's health, the synced deltas don't add up", Describe(apActor), cDrift);
}
} // namespace DamageDiagnostics
