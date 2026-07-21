#pragma once

#include <cstdint>

struct LocalizedActorState
{
    uint64_t LocalizedTick = 0;
    uint8_t BroadcastedResetActions = 0;
    static constexpr uint64_t kAnimReconcileGraceTicks = 60;
};
