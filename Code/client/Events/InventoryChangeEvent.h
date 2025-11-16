#pragma once

#include <ExtraData/ExtraDataList.h>
#include <Structs/Inventory.h>
#include <Games/Primitives.h>

#include <optional>

/**
 * @brief Dispatched when the contents of an object or actor inventory changes locally.
 *
 * The event has a Drop member variable, since dropped items need to be handled differently.
 */
struct InventoryChangeEvent
{
    InventoryChangeEvent(const uint32_t aFormId, Inventory::Entry arItem)
        : FormId(aFormId)
        , Item(std::move(arItem))
    {
    }

    InventoryChangeEvent(const uint32_t aFormId, Inventory::Entry arItem, bool aDrop)
        : FormId(aFormId)
        , Item(std::move(arItem))
        , Drop(aDrop)
    {
    }

    InventoryChangeEvent(const uint32_t aFormId, Inventory::Entry arItem, bool aDrop, bool aUpdateClients, std::optional<NiPoint3> aDropLocation = std::nullopt, std::optional<NiPoint3> aDropRotation = std::nullopt, std::optional<uint32_t> aDropInstanceId = std::nullopt)
        : FormId(aFormId)
        , Item(std::move(arItem))
        , Drop(aDrop)
        , UpdateClients(aUpdateClients)
        , DropLocation(std::move(aDropLocation))
        , DropRotation(std::move(aDropRotation))
        , DropInstanceId(std::move(aDropInstanceId))
    {
    }

    uint32_t FormId{};
    Inventory::Entry Item{};
    bool Drop = false;
    bool UpdateClients = true;
    std::optional<NiPoint3> DropLocation{};
    std::optional<NiPoint3> DropRotation{};
    std::optional<uint32_t> DropInstanceId{};
};
