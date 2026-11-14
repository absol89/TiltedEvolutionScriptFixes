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

// How long events are kept to compare with later ones.
constexpr auto kHistory = std::chrono::milliseconds(2000);
// The same hit twice closer than this counts as one hit applied twice.
constexpr auto kSameHitWindow = std::chrono::milliseconds(150);
// Hits below this are damage-over-time ticks, too many and too small to log.
constexpr float kMinLoggedDamage = 1.f;

struct Record
{
    uint64_t Id = 0;
    Clock::time_point Time{};
    uint32_t SourceId = 0;
    uint32_t TargetId = 0;
    float Damage = 0.f;
};

std::mutex s_lock;
uint64_t s_nextId = 1;
std::deque<Record> s_localHits;
std::deque<Record> s_receivedHits;
std::deque<Record> s_broadcasts;
std::deque<Record> s_receivedSpells;

long long Ms(Clock::duration aDuration) noexcept
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(aDuration).count();
}

void Prune(std::deque<Record>& aRecords, Clock::time_point aNow) noexcept
{
    while (!aRecords.empty() && aNow - aRecords.front().Time > kHistory)
        aRecords.pop_front();
}

bool Close(float aA, float aB) noexcept
{
    return std::abs(aA - aB) <= std::max(0.5f, std::max(std::abs(aA), std::abs(aB)) * 0.05f);
}

// Pushes the record and returns the earlier one it duplicates, if any.
std::optional<Record> Track(std::deque<Record>& aRecords, const Record& acRecord) noexcept
{
    Prune(aRecords, acRecord.Time);
    std::optional<Record> twin;
    for (auto it = aRecords.rbegin(); it != aRecords.rend(); ++it)
    {
        if (acRecord.Time - it->Time > kSameHitWindow)
            break;
        if (it->SourceId == acRecord.SourceId && it->TargetId == acRecord.TargetId && Close(it->Damage, acRecord.Damage))
        {
            twin = *it;
            break;
        }
    }
    aRecords.push_back(acRecord);
    return twin;
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
    return pExtension->IsRemote() ? "remote npc" : "local npc";
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

int32_t Difficulty() noexcept
{
    const auto* pPlayer = PlayerCharacter::Get();
    return pPlayer ? pPlayer->difficulty : -1;
}
} // namespace

uint64_t OnLocalHit(Actor* apHittee, Actor* apHitter, float aRawDamage, float aRealDamage, const char* acpPath) noexcept
{
    if (!apHittee || aRealDamage < kMinLoggedDamage || !(IsPlayer(apHittee) || IsPlayer(apHitter)))
        return 0;

    std::scoped_lock lock(s_lock);
    const Record cRecord{s_nextId++, Clock::now(), apHitter ? apHitter->formID : 0, apHittee->formID, aRawDamage};
    const auto cTwin = Track(s_localHits, cRecord);

    spdlog::info(
        "[DmgDiag] #{} HIT path={} hitter={} hittee={} raw={:.1f} real={:.1f} hp={:.1f}/{:.1f} diff={}", cRecord.Id, acpPath, Describe(apHitter), Describe(apHittee), aRawDamage,
        aRealDamage, apHittee->GetActorValue(ActorValueInfo::kHealth), apHittee->GetActorPermanentValue(ActorValueInfo::kHealth), Difficulty());
    if (cTwin)
        spdlog::warn("[DmgDiag] #{} FLAG DUP_LOCAL_HIT: same hitter, hittee and damage as #{} {} ms earlier", cRecord.Id, cTwin->Id, Ms(cRecord.Time - cTwin->Time));

    return cRecord.Id;
}

void OnDamageApplied(uint64_t aHitId, Actor* apHittee, float aHealthBefore) noexcept
{
    if (!apHittee || aHitId == 0)
        return;

    const float cHealthAfter = apHittee->GetActorValue(ActorValueInfo::kHealth);
    spdlog::info("[DmgDiag] #{} APPLIED hp {:.1f} -> {:.1f}", aHitId, aHealthBefore, cHealthAfter);
}

void OnPlayerHitSent(Actor* apHittee, Actor* apAttacker, float aPhysicalDamage) noexcept
{
    if (!apHittee || aPhysicalDamage < kMinLoggedDamage)
        return;

    std::scoped_lock lock(s_lock);
    const uint64_t cId = s_nextId++;
    spdlog::info(
        "[DmgDiag] #{} SENT hit attacker={} victim={} before-armor={:.1f}, the victim's client applies armor, block and difficulty", cId, Describe(apAttacker), Describe(apHittee),
        aPhysicalDamage);
}

void OnPlayerHitReceived(Actor* apAttacker, float aPhysicalDamage, float aAfterArmor, float aAfterPerks, float aHealthBefore, float aHealthAfter) noexcept
{
    if (aPhysicalDamage < kMinLoggedDamage)
        return;

    std::scoped_lock lock(s_lock);
    const Record cRecord{s_nextId++, Clock::now(), apAttacker ? apAttacker->formID : 0, 0x14, aPhysicalDamage};
    const auto cTwin = Track(s_receivedHits, cRecord);

    spdlog::info(
        "[DmgDiag] #{} RECV hit attacker={} before-armor={:.1f} after-armor-block={:.1f} after-perks={:.1f} hp {:.1f} -> {:.1f} diff={}", cRecord.Id, Describe(apAttacker),
        aPhysicalDamage, aAfterArmor, aAfterPerks, aHealthBefore, aHealthAfter, Difficulty());
    if (cTwin)
        spdlog::warn("[DmgDiag] #{} FLAG DUP_RECEIVED_HIT: same attacker and damage as #{} {} ms earlier", cRecord.Id, cTwin->Id, Ms(cRecord.Time - cTwin->Time));
}

void OnSpellHitSent(Actor* apHittee, Actor* apCaster, const char* acpSpellName, uint32_t aEffectId, float aMagnitude) noexcept
{
    if (!apHittee || aMagnitude < kMinLoggedDamage)
        return;

    std::scoped_lock lock(s_lock);
    const uint64_t cId = s_nextId++;
    spdlog::info(
        "[DmgDiag] #{} SENT spell caster={} victim={} spell={} effect={:X} magnitude={:.1f}, the victim's client applies resistances and difficulty", cId, Describe(apCaster),
        Describe(apHittee), acpSpellName && *acpSpellName ? acpSpellName : "?", aEffectId, aMagnitude);
}

void OnSpellHitReceived(Actor* apCaster, const char* acpSpellName, uint32_t aEffectId, float aMagnitude, bool aApplied, float aHealthBefore, float aHealthAfter) noexcept
{
    if (aMagnitude < kMinLoggedDamage)
        return;

    std::scoped_lock lock(s_lock);
    const Record cRecord{s_nextId++, Clock::now(), apCaster ? apCaster->formID : 0, aEffectId, aMagnitude};
    const auto cTwin = Track(s_receivedSpells, cRecord);

    // Health can drop a frame later, when the effect starts.
    spdlog::info(
        "[DmgDiag] #{} RECV spell caster={} spell={} effect={:X} magnitude={:.1f} applied={} hp {:.1f} -> {:.1f} diff={}", cRecord.Id, Describe(apCaster),
        acpSpellName && *acpSpellName ? acpSpellName : "?", aEffectId, aMagnitude, aApplied ? "yes" : "no", aHealthBefore, aHealthAfter, Difficulty());
    if (!aApplied)
        spdlog::warn("[DmgDiag] #{} FLAG SPELL_REFUSED: the game refused it, resisted in full, blocked by a condition or already active", cRecord.Id);
    if (cTwin)
        spdlog::warn("[DmgDiag] #{} FLAG DUP_RECEIVED_SPELL: same caster, effect and magnitude as #{} {} ms earlier", cRecord.Id, cTwin->Id, Ms(cRecord.Time - cTwin->Time));
}

void OnHealthBroadcastReceived(uint32_t aServerId, float aDelta) noexcept
{
    if (std::abs(aDelta) < kMinLoggedDamage)
        return;

    std::scoped_lock lock(s_lock);
    const Record cRecord{s_nextId++, Clock::now(), 0, aServerId, aDelta};
    if (const auto cTwin = Track(s_broadcasts, cRecord))
        spdlog::warn("[DmgDiag] FLAG DUP_BROADCAST: health delta {:.1f} for server id {:X} arrived twice within {} ms", aDelta, aServerId, Ms(cRecord.Time - cTwin->Time));
}
} // namespace DamageDiagnostics
