#pragma once

#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif

#include <Structs/Inventory.h>
#include <Messages/NotifyInventoryChanges.h>
#include <Messages/NotifyEquipmentChanges.h>
#include <variant>

struct PendingInventoryComponent
{
    PendingInventoryComponent(const Inventory& acInventory, uint32_t aOwnershipEpoch)
        : Content(acInventory), OwnershipEpoch(aOwnershipEpoch)
    {
    }

    Inventory Content;
    uint32_t OwnershipEpoch;
    Vector<std::variant<NotifyInventoryChanges, NotifyEquipmentChanges>> Updates;
};
