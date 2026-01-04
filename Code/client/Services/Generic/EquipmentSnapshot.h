#pragma once

#include <cstdint>

#include <Actor.h>
#include <DefaultObjectManager.h>
#include <EquipManager.h>
#include <Forms/TESForm.h>
#include <PlayerCharacter.h>

struct EquipmentSnapshot
{
    uint32_t LeftSpellId = 0;
    uint32_t RightSpellId = 0;
    uint32_t LeftWeaponId = 0;
    uint32_t RightWeaponId = 0;
    uint32_t TwoHandWeaponId = 0;
    uint32_t AmmoId = 0;
    uint32_t ShoutId = 0;
    bool WasWeaponDrawn = false;
};

inline EquipmentSnapshot CaptureEquipmentSnapshot(Actor* pActor) noexcept
{
    EquipmentSnapshot snapshot{};
    if (!pActor)
        return snapshot;

    snapshot.WasWeaponDrawn = pActor->actorState.IsWeaponDrawn();

    TESForm* pLeftSpell = pActor->magicItems[0];
    TESForm* pRightSpell = pActor->magicItems[1];

    if (pLeftSpell)
        snapshot.LeftSpellId = pLeftSpell->formID;
    if (pRightSpell)
        snapshot.RightSpellId = pRightSpell->formID;

    TESForm* pLeftWeapon = pLeftSpell ? nullptr : pActor->GetEquippedWeapon(0);
    TESForm* pRightWeapon = pRightSpell ? nullptr : pActor->GetEquippedWeapon(1);
    const bool isTwoHand = pLeftWeapon && pRightWeapon && pLeftWeapon == pRightWeapon;

    if (isTwoHand)
    {
        snapshot.TwoHandWeaponId = pLeftWeapon->formID;
    }
    else
    {
        if (pLeftWeapon)
            snapshot.LeftWeaponId = pLeftWeapon->formID;
        if (pRightWeapon)
            snapshot.RightWeaponId = pRightWeapon->formID;
    }

    if (auto* pAmmo = pActor->GetEquippedAmmo())
        snapshot.AmmoId = pAmmo->formID;

    if (auto* pShout = pActor->equippedShout)
        snapshot.ShoutId = pShout->formID;

    return snapshot;
}

inline void RestoreEquipmentSnapshot(PlayerCharacter* pPlayer, const EquipmentSnapshot& snapshot, bool restoreDrawState) noexcept
{
    if (!pPlayer)
        return;

    auto* pEquipManager = EquipManager::Get();
    if (!pEquipManager)
        return;

    auto& defaults = DefaultObjectManager::Get();

    pPlayer->SetWeaponDrawnEx(false);

    if (auto* pSpell = pPlayer->magicItems[0])
        pEquipManager->UnEquipSpell(pPlayer, pSpell, 0);
    if (auto* pSpell = pPlayer->magicItems[1])
        pEquipManager->UnEquipSpell(pPlayer, pSpell, 1);

    TESForm* pLeftWeapon = pPlayer->GetEquippedWeapon(0);
    TESForm* pRightWeapon = pPlayer->GetEquippedWeapon(1);
    TESForm* pTwoHand = (pLeftWeapon && pRightWeapon && pLeftWeapon == pRightWeapon) ? pLeftWeapon : nullptr;

    if (pTwoHand)
    {
        pEquipManager->UnEquip(pPlayer, pTwoHand, nullptr, 1, defaults.eitherEquipSlot, false, true, false, false, nullptr);
    }
    else
    {
        if (pLeftWeapon)
            pEquipManager->UnEquip(pPlayer, pLeftWeapon, nullptr, 1, defaults.leftEquipSlot, false, true, false, false, nullptr);
        if (pRightWeapon)
            pEquipManager->UnEquip(pPlayer, pRightWeapon, nullptr, 1, defaults.rightEquipSlot, false, true, false, false, nullptr);
    }

    if (auto* pAmmo = pPlayer->GetEquippedAmmo())
        pEquipManager->UnEquip(pPlayer, pAmmo, nullptr, 1, defaults.rightEquipSlot, false, true, false, false, nullptr);

    if (auto* pShout = pPlayer->equippedShout)
        pEquipManager->UnEquipShout(pPlayer, pShout);

    const bool useTwoHand = snapshot.TwoHandWeaponId != 0;

    if (!useTwoHand)
    {
        if (snapshot.LeftSpellId)
        {
            if (auto* pSpell = TESForm::GetById(snapshot.LeftSpellId))
                pEquipManager->EquipSpell(pPlayer, pSpell, 0);
        }
        else if (snapshot.LeftWeaponId)
        {
            if (auto* pItem = TESForm::GetById(snapshot.LeftWeaponId))
                pEquipManager->Equip(pPlayer, pItem, nullptr, 1, defaults.leftEquipSlot, false, true, false, false);
        }
    }

    if (snapshot.RightSpellId && !useTwoHand)
    {
        if (auto* pSpell = TESForm::GetById(snapshot.RightSpellId))
            pEquipManager->EquipSpell(pPlayer, pSpell, 1);
    }
    else if (useTwoHand)
    {
        if (auto* pItem = TESForm::GetById(snapshot.TwoHandWeaponId))
            pEquipManager->Equip(pPlayer, pItem, nullptr, 1, defaults.eitherEquipSlot, false, true, false, false);
    }
    else if (snapshot.RightWeaponId)
    {
        if (auto* pItem = TESForm::GetById(snapshot.RightWeaponId))
            pEquipManager->Equip(pPlayer, pItem, nullptr, 1, defaults.rightEquipSlot, false, true, false, false);
    }

    if (snapshot.AmmoId)
    {
        if (auto* pAmmo = TESForm::GetById(snapshot.AmmoId))
            pEquipManager->Equip(pPlayer, pAmmo, nullptr, 1, defaults.rightEquipSlot, false, true, false, false);
    }

    if (snapshot.ShoutId)
    {
        if (auto* pShout = TESForm::GetById(snapshot.ShoutId))
            pEquipManager->EquipShout(pPlayer, pShout);
    }

    if (restoreDrawState)
        pPlayer->SetWeaponDrawnEx(snapshot.WasWeaponDrawn);
}
