#pragma once

#include <Forms/TESForm.h>
#include <Games/Primitives.h>

struct Actor;

struct BGSPerkEntry
{
    enum Type : uint32_t
    {
        kQuest = 0,
        kAbility = 1,
        kEntryPoint = 2,
    };

    virtual bool CheckConditionFilters(uint32_t aArgCount, void* apArgs);
    virtual uint32_t GetFunction();
    virtual void* GetFunctionData();
    virtual void sub_3();
    virtual Type GetType();

    uint8_t rank;
    uint8_t priority;
};
static_assert(offsetof(BGSPerkEntry, rank) == 0x8);
static_assert(offsetof(BGSPerkEntry, priority) == 0x9);

struct BGSEntryPointPerkEntry : BGSPerkEntry
{
    static constexpr uint32_t kEntryPointCount = 0x5C;

    uint8_t entryPoint;
};
static_assert(offsetof(BGSEntryPointPerkEntry, entryPoint) == 0x10);

struct BGSPerk : TESForm
{
    uint8_t pad20[0x60 - sizeof(TESForm)];
    GameArray<BGSPerkEntry*> perkEntries;
};
static_assert(offsetof(BGSPerk, perkEntries) == 0x60);

// Element of TESNPC::perkRanks. PlayerCharacter stores pointers to these for the perks a player earned.
struct PerkRankData
{
    BGSPerk* pPerk;
    uint8_t rank;
};
static_assert(sizeof(PerkRankData) == 0x10);

// The engine's visitors for Actor::VisitPerks (vtable slot 0xFA) and Actor::ForEachPerkEntry (slot 0x100).
// Returning 1 means continue.
struct PerkRankVisitor
{
    virtual uint32_t Visit(PerkRankData* apPerkRank) = 0;
};

struct PerkEntryVisitor
{
    virtual uint32_t Visit(BGSPerkEntry* apEntry) = 0;
};

// The visitor that BGSEntryPoint::HandleEntryPoint passes to Actor::ForEachPerkEntry.
struct HandleEntryPointVisitor : PerkEntryVisitor
{
    uint32_t function;
    void** ppArguments; // The perk owner, followed by the entry point's filter arguments
    void* pResults;
    Actor* pPerkOwner;
    uint8_t argumentCount;
    uint8_t resultCount;
};
static_assert(offsetof(HandleEntryPointVisitor, ppArguments) == 0x10);
static_assert(offsetof(HandleEntryPointVisitor, argumentCount) == 0x28);
