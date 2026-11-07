#pragma once

#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif

#include <Structs/Inventory.h>

struct RemoteComponent
{
    RemoteComponent(uint32_t aId, uint32_t aRefId, uint32_t aOwnershipEpoch) noexcept
        : Id(aId)
        , CachedRefId(aRefId)
        , OwnershipEpoch(aOwnershipEpoch)
    {
    }

    uint32_t Id;
    uint32_t CachedRefId;
    uint32_t OwnershipEpoch;
    // Canonical death state survives loading and prevents living animation replay on corpses.
    bool IsDead{false};
    // Retries of a death the game did not take, see CharacterService::RunRemoteUpdates.
    uint8_t KillAttempts{0};
    double NextKillAttempt{0.0};
};
