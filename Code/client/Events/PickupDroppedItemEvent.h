#pragma once

struct PickupDroppedItemEvent
{
    PickupDroppedItemEvent(uint32_t aActorFormId, uint64_t aDropId)
        : ActorFormId(aActorFormId)
        , DropId(aDropId)
    {
    }

    uint32_t ActorFormId{};
    uint64_t DropId{};
};
