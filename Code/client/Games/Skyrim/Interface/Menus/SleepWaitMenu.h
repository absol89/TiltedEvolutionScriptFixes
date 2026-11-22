#pragma once

#include <Interface/IMenu.h>

struct SleepWaitMenu final : IMenu
{
    static bool IsOpen() noexcept;

    // Authorizes the current menu request; its next main-thread UI update starts the sleep.
    static void Confirm(float aHours, uint64_t aRequestId) noexcept;
    static bool IsRequestPending(uint64_t aRequestId) noexcept;
    static bool IsConfirming() noexcept;
    static void CancelPendingConfirm(uint64_t aRequestId = 0) noexcept;
};
