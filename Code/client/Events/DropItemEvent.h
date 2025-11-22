#pragma once

#include <Structs/Inventory.h>
#include <Games/Primitives.h>

struct DropItemEvent
{
    DropItemEvent(uint32_t aActorFormId, Inventory::Entry aItem, uint32_t aClientDropId, NiPoint3 aLocation, NiPoint3 aRotation, uint32_t aHandleBits)
        : ActorFormId(aActorFormId)
        , Item(std::move(aItem))
        , ClientDropId(aClientDropId)
        , Location(aLocation)
        , Rotation(aRotation)
        , HandleBits(aHandleBits)
    {
    }

    uint32_t ActorFormId{};
    Inventory::Entry Item{};
    uint32_t ClientDropId{};
    NiPoint3 Location{};
    NiPoint3 Rotation{};
    uint32_t HandleBits{};
};
