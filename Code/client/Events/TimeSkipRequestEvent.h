#pragma once

/**
 * @brief Dispatched when the local player confirms the sleep/wait menu while connected.
 */
struct TimeSkipRequestEvent
{
    float Hours;
    // Local menu request identity; not a network field.
    uint64_t RequestId;
};
