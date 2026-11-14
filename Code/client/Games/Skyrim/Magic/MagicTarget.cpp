#include "MagicTarget.h"

#include <Actor.h>
#include <World.h>
#include "EffectItem.h"

#include <Structs/Skyrim/AnimationGraphDescriptor_WerewolfBehavior.h>
#include <Structs/Skyrim/AnimationGraphDescriptor_VampireLordBehavior.h>

#include <Events/AddTargetEvent.h>
#include <Events/HitEvent.h>

#include <Games/ActorExtension.h>
#include <Games/Overrides.h>
#include <PlayerCharacter.h>
#include <RemotePerks.h>

#include <Effects/ActiveEffect.h>
#include <Forms/SpellItem.h>

TP_THIS_FUNCTION(TAddTarget, bool, MagicTarget, MagicTarget::AddTargetData& arData);
TP_THIS_FUNCTION(TCheckAddEffectTargetData, bool, MagicTarget::AddTargetData, void* arArgs, float afResistance);
TP_THIS_FUNCTION(TFindTargets, bool, MagicCaster, float afEffectivenessMult, int32_t* aruiTargetCount,
                 TESBoundObject* apSource, char abLoadCast, char abAdjust);
TP_THIS_FUNCTION(TAdjustForPerks, void, ActiveEffect, Actor* apCaster, MagicTarget* apTarget);
TP_THIS_FUNCTION(THasPerk, bool, Actor, TESForm* apPerk, void* apUnk1, double* afReturnValue);
TP_THIS_FUNCTION(TGetPerkRank, uint8_t, Actor, TESForm* apPerk);
using TConditionFunction = bool(TESObjectREFR* apSubject, void* apParam1, void* apParam2, double* apResult);

static TAddTarget* RealAddTarget = nullptr;
static TCheckAddEffectTargetData* RealCheckAddEffectTargetData = nullptr;
static TFindTargets* RealFindTargets = nullptr;
static TAdjustForPerks* RealAdjustForPerks = nullptr;
static THasPerk* RealHasPerk = nullptr;
static TGetPerkRank* RealGetPerkRank = nullptr;
static TConditionFunction* RealConditionIsHostileToActor = nullptr;
static TConditionFunction* RealConditionGetIsID = nullptr;

static thread_local bool s_autoSucceedEffectCheck = false;
static thread_local bool s_applyHealPerkBonus = false;
static thread_local bool s_applyStaminaPerkBonus = false;
static thread_local bool s_isCheckingAttackFromPlayer = false;
static thread_local bool s_isCheckingParalysis = false;

void MagicTarget::DispelAllSpells(bool aNow) noexcept
{
    TP_THIS_FUNCTION(TDispelAllSpells, void, MagicTarget, bool aNow);
    POINTER_SKYRIMSE(TDispelAllSpells, dispelAllSpells, 34512);
    TiltedPhoques::ThisCall(dispelAllSpells, this, aNow);
}

bool MagicTarget::AddTarget(AddTargetData& arData, bool aApplyHealPerkBonus, bool aApplyStaminaPerkBonus) noexcept
{
    // Poisons come from the attacker's hit, so the victim's conditions still get to refuse them.
    s_autoSucceedEffectCheck = !arData.pSpell->IsPoison();
    s_applyHealPerkBonus = aApplyHealPerkBonus;
    s_applyStaminaPerkBonus = aApplyStaminaPerkBonus;

    bool result = false;
    if (arData.pSpell->IsHealingSpell() || arData.pSpell->IsBuffSpell())
    {
        // The caster's perks are already part of what they send for these, see ApplyHealPerkBonus.
        ScopedIgnoreRemotePerks ignoreRemotePerks;
        result = TiltedPhoques::ThisCall(RealAddTarget, this, arData);
    }
    else
    {
        ScopedRemotePerks remotePerks;
        result = TiltedPhoques::ThisCall(RealAddTarget, this, arData);
    }

    s_autoSucceedEffectCheck = false;
    s_applyHealPerkBonus = false;
    s_applyStaminaPerkBonus = false;

    return result;
}

bool MagicTarget::AddTargetData::ShouldSync()
{
    // Detect life is an area effect on every actor around the caster, syncing it would show them to other players too.
    if (pEffectItem->IsDetectLifeEffect())
        return false;

    // Constant effect slow time is an ability each client's scripts add and remove on their own player, so a synced
    // copy would stay on the other players for good. Fire and forget slow time (shouts) still syncs.
    if (pEffectItem->IsSlowEffect())
    {
        SpellItem* pSpellItem = Cast<SpellItem>(pSpell);
        if (pSpellItem && pSpellItem->eCastingType == MagicSystem::CastingType::CONSTANT_EFFECT)
            return false;
    }

    return !pEffectItem->IsSummonEffect() && !pSpell->IsInvisibilitySpell() && !pSpell->IsWardSpell();
}

// Some effects are player specific, and do not need to be synced.
bool MagicTarget::AddTargetData::IsForbiddenEffect(Actor* apTarget)
{
    if (apTarget != PlayerCharacter::Get())
        return false;

    return pEffectItem->IsNightVisionEffect();
}

// Hostile area spells that are cast on the caster (Mass Paralysis, for example) have no projectile, so nothing about
// them runs on any client but the caster's. Those are sent to the other clients once they find a player. A replayed
// concentration spell doesn't get to decide on the targets either, since it only finds them once.
static bool IsSelfCastAreaAttack(const MagicTarget::AddTargetData& acData) noexcept
{
    if (acData.pSpell->iHostileCount <= 0 || acData.pEffectItem->data.iArea <= 0 || !World::Get().GetServerSettings().PvpEnabled)
        return false;

    const SpellItem* pSpellItem = Cast<SpellItem>(acData.pSpell);
    return pSpellItem && pSpellItem->eDelivery == MagicSystem::Delivery::SELF && pSpellItem->eCastingType != MagicSystem::CastingType::CONSTANT_EFFECT;
}

// The ice form effect of Unbounded Freezing. Like the shout it comes from, it isn't meant to reach players.
static bool IsChanneledParalysis(const MagicTarget::AddTargetData& acData) noexcept
{
    if (acData.pEffectItem->pEffectSetting->eArchetype != EffectArchetypes::ArchetypeID::kParalysis)
        return false;

    const SpellItem* pSpellItem = Cast<SpellItem>(acData.pSpell);
    return pSpellItem && pSpellItem->eCastingType == MagicSystem::CastingType::CONCENTRATION;
}

// CloakEffect passes this callback along with each of the targets it finds around the wearer.
static bool IsCloakHit(const MagicTarget::AddTargetData& acData) noexcept
{
    POINTER_SKYRIMSE(uint8_t, s_cloakCallbackVtable, 205851);
    return acData.pCallback && *reinterpret_cast<void* const*>(acData.pCallback) == s_cloakCallbackVtable.Get();
}

Actor* MagicTarget::GetTargetAsActor()
{
    TP_THIS_FUNCTION(TGetTargetAsActor, Actor*, MagicTarget);
    POINTER_SKYRIMSE(TGetTargetAsActor, getTargetAsActor, 34529);
    return TiltedPhoques::ThisCall(getTargetAsActor, this);
}

bool TP_MAKE_THISCALL(HookAddTarget, MagicTarget, MagicTarget::AddTargetData& arData)
{
    Actor* pTargetActor = apThis->GetTargetAsActor();
    if (!pTargetActor || arData.IsForbiddenEffect(pTargetActor))
        return TiltedPhoques::ThisCall(RealAddTarget, apThis, arData);

    ActorExtension* pTargetActorEx = pTargetActor->GetExtension();

    if (arData.pEffectItem->IsWerewolfEffect())
        pTargetActorEx->GraphDescriptorHash = AnimationGraphDescriptor_WerewolfBehavior::m_key;

    if (arData.pEffectItem->IsVampireLordEffect())
        pTargetActorEx->GraphDescriptorHash = AnimationGraphDescriptor_VampireLordBehavior::m_key;

    if (arData.pEffectItem->IsDetectLifeEffect() && arData.pCaster)
    {
        const ActorExtension* pCasterExtension = arData.pCaster->GetExtension();

        // The caster's copy repeats the cast on this client, but only the caster gets to see living targets.
        if (pCasterExtension->IsRemotePlayer())
            return false;

        if (pCasterExtension->IsLocalPlayer() && pTargetActorEx->IsRemotePlayer() && World::Get().GetServerSettings().PvpEnabled)
            return TiltedPhoques::ThisCall(RealAddTarget, apThis, arData);
    }

    // The caster's client sends these, see below.
    if (pTargetActorEx->IsLocalPlayer() && arData.pCaster && arData.pCaster->GetExtension()->IsRemotePlayer() && IsSelfCastAreaAttack(arData))
        return false;

    if (ScopedSpellCastOverride::IsOverriden())
        return TiltedPhoques::ThisCall(RealAddTarget, apThis, arData);

    AddTargetEvent addTargetEvent{};
    addTargetEvent.TargetID = pTargetActor->formID;
    addTargetEvent.CasterID = arData.pCaster ? arData.pCaster->formID : 0;
    addTargetEvent.SpellID = arData.pSpell->formID;
    addTargetEvent.EffectID = arData.pEffectItem->pEffectSetting->formID;
    addTargetEvent.Magnitude = arData.fMagnitude;
    addTargetEvent.IsDualCasting = arData.bDualCast;

    if (pTargetActorEx->IsRemotePlayer())
    {
        if (!arData.pCaster)
            return false;

        // The spell doesn't land here, but it does make the two players enemies.
        if (arData.pSpell->iHostileCount > 0 && arData.pCaster->GetExtension()->IsLocalPlayer())
            World::Get().GetRunner().Trigger(HitEvent(arData.pCaster->formID, pTargetActor->formID));

        // The charge of a poison belongs to the attacker's weapon, so this client decides that it lands and tells the
        // victim's client. It isn't applied to the copy here, its health comes from the victim's client anyway.
        if (arData.pSpell->IsPoison() && World::Get().GetServerSettings().PvpEnabled && arData.pCaster->GetExtension()->IsLocalPlayer())
        {
            World::Get().GetRunner().Trigger(addTargetEvent);
            return false;
        }

        // The wearer's client is the one that knows how long the cloak lasts, a concentration cloak only lives on as
        // long as it's being cast there. So it decides on the hits, and the copy's cloak is ignored on the victim's client.
        if (IsCloakHit(arData) && arData.pCaster->GetExtension()->IsLocalPlayer() && World::Get().GetServerSettings().PvpEnabled)
        {
            addTargetEvent.IsPvpForwarded = true;
            World::Get().GetRunner().Trigger(addTargetEvent);
            return false;
        }

        // Perk disarms (Disarming Bash etc.) come from the attacker's hit and its perks, which the victim's client
        // doesn't run, so it gets told here. Shouts reach the victim's client through their own projectile.
        if (arData.pEffectItem->IsDisarmEffect() && arData.pCaster->GetExtension()->IsLocalPlayer() && World::Get().GetServerSettings().PvpEnabled)
        {
            const SpellItem* pSpellItem = Cast<SpellItem>(arData.pSpell);
            if (pSpellItem && pSpellItem->eDelivery == MagicSystem::Delivery::TOUCH)
                World::Get().GetRunner().Trigger(addTargetEvent);

            return false;
        }

        if (arData.pCaster->GetExtension()->IsLocalPlayer() && IsSelfCastAreaAttack(arData) && !IsChanneledParalysis(arData))
        {
            addTargetEvent.IsPvpForwarded = true;
            World::Get().GetRunner().Trigger(addTargetEvent);

            // A script (the lightning from Unbounded Storms, for example) only has to run on the victim's client.
            if (arData.pEffectItem->pEffectSetting->eArchetype == EffectArchetypes::ArchetypeID::kScript)
                return false;

            return TiltedPhoques::ThisCall(RealAddTarget, apThis, arData);
        }

        if (!arData.pSpell->IsHealingSpell() && !arData.pSpell->IsBuffSpell())
            return false;

        ActorExtension* pCasterExtension = arData.pCaster->GetExtension();
        if (!pCasterExtension->IsLocalPlayer())
            return false;

        if (arData.pSpell->IsHealingSpell())
        {
            addTargetEvent.ApplyHealPerkBonus = arData.pCaster->HasPerk(0x581f8);
            addTargetEvent.ApplyStaminaPerkBonus = arData.pCaster->HasPerk(0x581f9);
        }

        bool result = TiltedPhoques::ThisCall(RealAddTarget, apThis, arData);
        if (result)
            World::Get().GetRunner().Trigger(addTargetEvent);
        return result;
    }

    if (pTargetActorEx->IsLocalPlayer())
    {
        if (arData.pCaster)
        {
            ActorExtension* pCasterExtension = arData.pCaster->GetExtension();
            if (pCasterExtension->IsRemotePlayer())
            {
                if (!World::Get().GetServerSettings().PvpEnabled)
                    return false;

                // Heal and buff spells are already synced by the caster.
                if (arData.pSpell->IsHealingSpell() || arData.pSpell->IsBuffSpell())
                    return false;

                if (arData.pSpell->iHostileCount > 0)
                    World::Get().GetRunner().Trigger(HitEvent(arData.pCaster->formID, pTargetActor->formID));

                // Poisons are synced by the attacker. The copy's ExtraPoison and its charges can be out of date.
                if (arData.pSpell->IsPoison())
                    return false;

                // Cloak hits are synced by the wearer.
                if (IsCloakHit(arData))
                    return false;
            }
            else if (pCasterExtension->IsRemote() && arData.pCaster->IsPlayerSummon() && arData.pSpell->iHostileCount > 0)
            {
                if (!World::Get().GetServerSettings().PvpEnabled)
                    return false;
            }
        }

        ScopedRemotePerks remotePerks;
        bool result = TiltedPhoques::ThisCall(RealAddTarget, apThis, arData);
        if (result && arData.ShouldSync())
            World::Get().GetRunner().Trigger(addTargetEvent);
        return result;
    }

    if (arData.pCaster)
    {
        ActorExtension* pCasterExtension = arData.pCaster->GetExtension();
        if (pCasterExtension->IsLocalPlayer())
        {
            bool result = TiltedPhoques::ThisCall(RealAddTarget, apThis, arData);
            if (result && arData.ShouldSync())
                World::Get().GetRunner().Trigger(addTargetEvent);
            return result;
        }
        else if (pCasterExtension->IsRemotePlayer())
        {
            // Send out a HitEvent because TakeDamage is never triggered.
            World::Get().GetRunner().Trigger(HitEvent(arData.pCaster->formID, pTargetActor->formID));
            return false;
        }
    }

    if (pTargetActorEx->IsLocal())
    {
        bool result = TiltedPhoques::ThisCall(RealAddTarget, apThis, arData);
        if (result && arData.ShouldSync())
            World::Get().GetRunner().Trigger(addTargetEvent);
        return result;
    }
    else
    {
        return false;
    }
}

bool TP_MAKE_THISCALL(HookCheckAddEffectTargetData, MagicTarget::AddTargetData, void* arArgs, float afResistance)
{
    // The victim's resistances and the effect's conditions still get to refuse an attack from another player.
    // Heal and buff spells have already passed them on the caster's client.
    Actor* pTargetActor = (*static_cast<MagicTarget**>(arArgs))->GetTargetAsActor();
    const bool cIsAttackFromPlayer = pTargetActor && pTargetActor->GetExtension()->IsLocalPlayer() && apThis->pCaster && apThis->pCaster->GetExtension()->IsRemotePlayer()
        && !apThis->pSpell->IsHealingSpell() && !apThis->pSpell->IsBuffSpell() && World::Get().GetServerSettings().PvpEnabled;

    if (s_autoSucceedEffectCheck && !cIsAttackFromPlayer)
        return true;

    s_isCheckingAttackFromPlayer = cIsAttackFromPlayer;
    s_isCheckingParalysis = apThis->pEffectItem->pEffectSetting->eArchetype == EffectArchetypes::ArchetypeID::kParalysis;
    const bool cResult = TiltedPhoques::ThisCall(RealCheckAddEffectTargetData, apThis, arArgs, afResistance);
    s_isCheckingAttackFromPlayer = false;
    s_isCheckingParalysis = false;
    return cResult;
}

// Effects keep players out of their reach with conditions, which have to let an attack from another player through:
// cloak damage asks if the target is hostile to the caster or to the player, some area effects ask if the target isn't
// the player. These only run when an effect is added, not while it's active. Paralysis keeps its player check.
static bool HookConditionIsHostileToActor(TESObjectREFR* apSubject, void* apParam1, void* apParam2, double* apResult)
{
    const bool cResult = RealConditionIsHostileToActor(apSubject, apParam1, apParam2, apResult);
    if (s_isCheckingAttackFromPlayer && apSubject == PlayerCharacter::Get())
        *apResult = 1.0;
    return cResult;
}

static bool HookConditionGetIsID(TESObjectREFR* apSubject, void* apParam1, void* apParam2, double* apResult)
{
    const bool cResult = RealConditionGetIsID(apSubject, apParam1, apParam2, apResult);
    if (s_isCheckingAttackFromPlayer && !s_isCheckingParalysis && apSubject == PlayerCharacter::Get())
        *apResult = 0.0;
    return cResult;
}

bool TP_MAKE_THISCALL(HookFindTargets, MagicCaster, float afEffectivenessMult, int32_t* aruiTargetCount, TESBoundObject* apSource, char abLoadCast, char abAdjust)
{
    return TiltedPhoques::ThisCall(RealFindTargets, apThis, afEffectivenessMult, aruiTargetCount, apSource, abLoadCast, abAdjust);
}

void TP_MAKE_THISCALL(HookAdjustForPerks, ActiveEffect, Actor* apCaster, MagicTarget* apTarget)
{
    TiltedPhoques::ThisCall(RealAdjustForPerks, apThis, apCaster, apTarget);

    if (s_applyHealPerkBonus)
        apThis->fMagnitude *= 1.5f;
}

bool TP_MAKE_THISCALL(HookHasPerk, Actor, TESForm* apPerk, void* apUnk1, double* afReturnValue)
{
    ActorExtension* pExtension = apThis ? apThis->GetExtension() : nullptr;

    if (pExtension && pExtension->IsRemotePlayer())
    {
        if (apPerk && apPerk->formID == 0x581f9)
        {
            if (s_applyStaminaPerkBonus)
            {
                if (afReturnValue)
                    *afReturnValue = 1.f;
                return true;
            }
        }
    }

    return TiltedPhoques::ThisCall(RealHasPerk, apThis, apPerk, apUnk1, afReturnValue);
}

uint8_t TP_MAKE_THISCALL(HookGetPerkRank, Actor, TESForm* apPerk)
{
    return TiltedPhoques::ThisCall(RealGetPerkRank, apThis, apPerk);
}

static TiltedPhoques::Initializer s_magicTargetHooks([]() {
    POINTER_SKYRIMSE(TAddTarget, addTarget, 34526);
    POINTER_SKYRIMSE(TCheckAddEffectTargetData, checkAddEffectTargetData, 34525);
    POINTER_SKYRIMSE(TFindTargets, findTargets, 34410);
    POINTER_SKYRIMSE(TAdjustForPerks, adjustForPerks, 34053);
    POINTER_SKYRIMSE(THasPerk, hasPerk, 21622);
    POINTER_SKYRIMSE(TGetPerkRank, getPerkRank, 37698);
    POINTER_SKYRIMSE(TConditionFunction, conditionIsHostileToActor, 21472);
    POINTER_SKYRIMSE(TConditionFunction, conditionGetIsID, 21487);

    RealAddTarget = addTarget.Get();
    RealCheckAddEffectTargetData = checkAddEffectTargetData.Get();
    RealFindTargets = findTargets.Get();
    RealAdjustForPerks = adjustForPerks.Get();
    RealHasPerk = hasPerk.Get();
    RealGetPerkRank = getPerkRank.Get();
    RealConditionIsHostileToActor = conditionIsHostileToActor.Get();
    RealConditionGetIsID = conditionGetIsID.Get();

    TP_HOOK(&RealAddTarget, HookAddTarget);
    TP_HOOK(&RealCheckAddEffectTargetData, HookCheckAddEffectTargetData);
    TP_HOOK(&RealFindTargets, HookFindTargets);
    TP_HOOK(&RealAdjustForPerks, HookAdjustForPerks);
    TP_HOOK(&RealHasPerk, HookHasPerk);
    TP_HOOK(&RealConditionIsHostileToActor, HookConditionIsHostileToActor);
    TP_HOOK(&RealConditionGetIsID, HookConditionGetIsID);
    //TP_HOOK(&RealGetPerkRank, HookGetPerkRank);
});
