#include <Interface/Menus/SleepWaitMenu.h>

#include <Events/TimeSkipRequestEvent.h>
#include <Interface/UI.h>
#include <World.h>

#include <atomic>
#include <mutex>

namespace
{
// Partial Main layout: SleepWaitMenu::ProcessMessage itself checks this field before handling input.
// Verified in the 1.6.1170 executable (ID 52486); the singleton is Address Library ID 403449.
struct MainThreadState
{
    uint8_t pad0[0x28];
    uint32_t threadId;
};
static_assert(offsetof(MainThreadState, threadId) == 0x28);

struct PendingConfirm
{
    IMenu* pMenu = nullptr; // Identity only; never dereferenced by the network/VM thread.
    uint64_t RequestId = 0;
    float Hours = 0.f;
};

std::mutex s_confirmMutex;
PendingConfirm s_pendingConfirm;
uint64_t s_nextRequestId = 0;
std::atomic_bool s_confirming{false};

bool IsMainThread() noexcept
{
    POINTER_SKYRIMSE(MainThreadState*, s_main, 403449);
    const MainThreadState* pMain = *s_main.Get();
    return pMain && pMain->threadId == GetCurrentThreadId();
}

const BSFixedString& GetMenuName() noexcept
{
    static const BSFixedString* s_name = new BSFixedString("Sleep/Wait Menu");
    return *s_name;
}

FxDelegateHandler::CallbackFn* RealConfirm = nullptr;
using TProcessMessage = UI_MESSAGE_RESULTS(IMenu*, UIMessage&);
TProcessMessage* RealProcessMessage = nullptr;

void HookConfirm(const FxDelegateArgs* apArgs)
{
    if (!World::Get().GetTransport().IsConnected())
        return RealConfirm(apArgs);

    if (!apArgs || !apArgs->pHandler || !apArgs->pArgs || apArgs->numArgs != 1)
        return;

    const float cHours = static_cast<float>((*apArgs)[0].value.number);
    if (!(cHours >= 1.f && cHours <= 24.f))
        return;

    uint64_t requestId;
    {
        std::scoped_lock lock(s_confirmMutex);
        // Repeated clicks must not create a second vote/confirmation for the same open menu.
        if (s_pendingConfirm.RequestId || s_confirming.load())
            return;
        requestId = ++s_nextRequestId;
        s_pendingConfirm = {static_cast<IMenu*>(apArgs->pHandler), requestId, 0.f};
    }
    World::Get().GetRunner().Trigger(TimeSkipRequestEvent{cHours, requestId});
}

UI_MESSAGE_RESULTS HookProcessMessage(IMenu* apMenu, UIMessage& aMessage)
{
    if (aMessage.eType == UIMessage::kShow || aMessage.eType == UIMessage::kHide || aMessage.eType == UIMessage::kForceHide)
        SleepWaitMenu::CancelPendingConfirm();

    // UI::ProcessMessages dispatches this update from Main::Update, outside the Papyrus job.
    // Other UI pumps (including loading screens/mods) must never consume it on a worker thread.
    if (aMessage.eType == UIMessage::kUpdate && IsMainThread() && !s_confirming.load())
    {
        float hours = 0.f;
        {
            std::scoped_lock lock(s_confirmMutex);
            if (s_pendingConfirm.pMenu == apMenu && s_pendingConfirm.Hours > 0.f)
            {
                hours = s_pendingConfirm.Hours;
                s_pendingConfirm = {};
                s_confirming.store(true);
            }
        }

        if (hours > 0.f)
        {
            // No handoff lock may be held across native code: autosaving can re-enter the VM.
            GFxValue value{};
            value.type = GFxValue::kNumber;
            value.value.number = hours;

            FxDelegateArgs args{};
            args.pHandler = apMenu;
            args.pMovieView = apMenu->uiMovie;
            args.pArgs = &value;
            args.numArgs = 1;
            spdlog::info("[TimeSkipDiag] starting {} hours from the sleep/wait menu update on thread {}", hours, GetCurrentThreadId());
            RealConfirm(&args);
            s_confirming.store(false);
        }
    }

    return RealProcessMessage(apMenu, aMessage);
}
} // namespace

bool SleepWaitMenu::IsOpen() noexcept
{
    return UI::Get()->GetMenuOpen(GetMenuName());
}

void SleepWaitMenu::Confirm(float aHours, uint64_t aRequestId) noexcept
{
    if (!(aHours >= 1.f && aHours <= 24.f))
        return;

    std::scoped_lock lock(s_confirmMutex);
    if (aRequestId && s_pendingConfirm.RequestId == aRequestId)
        s_pendingConfirm.Hours = aHours;
}

bool SleepWaitMenu::IsRequestPending(uint64_t aRequestId) noexcept
{
    std::scoped_lock lock(s_confirmMutex);
    return aRequestId && s_pendingConfirm.RequestId == aRequestId;
}

bool SleepWaitMenu::IsConfirming() noexcept
{
    return s_confirming.load();
}

void SleepWaitMenu::CancelPendingConfirm(uint64_t aRequestId) noexcept
{
    std::scoped_lock lock(s_confirmMutex);
    if (!aRequestId || s_pendingConfirm.RequestId == aRequestId)
        s_pendingConfirm = {};
}

static TiltedPhoques::Initializer s_sleepWaitMenuInit(
    []()
    {
// SleepWaitMenu's "OK" callback, the one SleepWaitMenu::Accept registers first. 1.7.104 gave it a new ID.
#ifdef TP_SKYRIM_1_6_1170
        POINTER_SKYRIMSE(FxDelegateHandler::CallbackFn, s_confirm, 52488);
#else
        POINTER_SKYRIMSE(FxDelegateHandler::CallbackFn, s_confirm, 524025);
#endif

        RealConfirm = s_confirm.Get();

        TP_HOOK(&RealConfirm, HookConfirm);

        POINTER_SKYRIMSE(TProcessMessage, s_processMessage, 52486);
        RealProcessMessage = s_processMessage.Get();
        TP_HOOK(&RealProcessMessage, HookProcessMessage);
    });
