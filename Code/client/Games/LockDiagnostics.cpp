#include <TiltedOnlinePCH.h>

#include <Games/Primitives.h>

// Review diagnostics for BSReadWriteLock stalls (the ExtraDataList lock fix). Not meant to ship: revert this commit
// before a release.
//
// Every read lock taken on an ExtraDataList through the game's functions is remembered on its thread until it is
// released (other locks are too frequent to follow). A thread that has waited kStallAfter for any write lock logs,
// once per lock, the lock's state and, for an ExtraDataList, every remembered reader of it. No remembered reader means
// the reader count was left behind by an unbalanced lock or unlock, not held by anyone.
//
// Only the caller's return address is recorded: walking the stack from these hooks (RtlCaptureStackBackTrace) can
// fault inside ntdll on frames without unwind data, which crashed the game past every crash handler.

#include <intrin.h>

namespace
{
using Clock = std::chrono::steady_clock;

constexpr auto kStallAfter = std::chrono::seconds(5);
// The lock sits right after the vtable, the entries and the presence bitfield (ExtraDataList::lock).
constexpr uintptr_t kExtraDataListLockOffset = 0x18;

void* s_pExtraDataListVtable = nullptr;

// No C++ objects here, so a lock at the very start of a page can be checked without crashing.
bool IsExtraDataListLock(const BSReadWriteLock* apLock)
{
    __try
    {
        return *reinterpret_cast<void* const*>(reinterpret_cast<uintptr_t>(apLock) - kExtraDataListLockOffset) == s_pExtraDataListVtable;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

struct ReadHold
{
    const BSReadWriteLock* pLock;
    Clock::time_point Since;
    void* pCaller;
};

struct ThreadHolds
{
    DWORD ThreadId;
    std::mutex Mutex;
    std::vector<ReadHold> Holds;
};

// Game threads live as long as the process, so a thread's entry is never removed.
std::mutex s_registryMutex;
std::vector<ThreadHolds*> s_registry;

std::mutex s_reportedMutex;
std::vector<const BSReadWriteLock*> s_reported;

// The bookkeeping can allocate, and an allocation may take a game lock again.
thread_local bool s_inDiagnostics = false;

ThreadHolds& GetThreadHolds()
{
    thread_local ThreadHolds* s_pHolds = []
    {
        auto* pHolds = new ThreadHolds();
        pHolds->ThreadId = GetCurrentThreadId();
        pHolds->Holds.reserve(64);

        std::scoped_lock _(s_registryMutex);
        s_registry.push_back(pHolds);
        return pHolds;
    }();

    return *s_pHolds;
}

std::string FormatAddress(void* apAddress)
{
    HMODULE module = nullptr;
    wchar_t path[MAX_PATH]{};
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCWSTR>(apAddress), &module) &&
        GetModuleFileNameW(module, path, MAX_PATH))
    {
        const auto name = std::filesystem::path(path).filename().string();
        return fmt::format("{}+{:X} ({})", name, reinterpret_cast<uintptr_t>(apAddress) - reinterpret_cast<uintptr_t>(module), apAddress);
    }

    return fmt::format("{}", apAddress);
}

void ReportStall(const BSReadWriteLock* apLock, Clock::time_point aStart, void* apCaller)
{
    {
        std::scoped_lock _(s_reportedMutex);
        if (std::find(s_reported.begin(), s_reported.end(), apLock) != s_reported.end())
            return;
        s_reported.push_back(apLock);
    }

    const bool cIsExtraDataList = IsExtraDataListLock(apLock);

    spdlog::critical(
        "Lock diagnostics: thread {} has waited {} ms to write-lock {}{} (writer thread {}, lock count {:#x}), from {}", GetCurrentThreadId(),
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - aStart).count(), static_cast<const void*>(apLock),
        cIsExtraDataList ? " (an ExtraDataList)" : "", apLock->uiWriterThread, apLock->uiLock, FormatAddress(apCaller));

    if (!cIsExtraDataList)
    {
        spdlog::critical("Lock diagnostics:   readers are only followed on ExtraDataList locks");
        return;
    }

    size_t readers = 0;
    const auto now = Clock::now();

    std::scoped_lock _(s_registryMutex);
    for (auto* pThread : s_registry)
    {
        std::scoped_lock __(pThread->Mutex);
        for (const auto& hold : pThread->Holds)
        {
            if (hold.pLock != apLock)
                continue;

            ++readers;
            spdlog::critical(
                "Lock diagnostics:   read-locked by thread {} for {} ms, from {}", pThread->ThreadId,
                std::chrono::duration_cast<std::chrono::milliseconds>(now - hold.Since).count(), FormatAddress(hold.pCaller));
        }
    }

    if (readers == 0)
        spdlog::critical("Lock diagnostics:   no thread holds a read lock on it: the count was left behind, not held");
}

TP_THIS_FUNCTION(TLockFunction, void, BSReadWriteLock);
TLockFunction* RealLockForRead = nullptr;
TLockFunction* RealUnlockForRead = nullptr;
TLockFunction* RealLockForWrite = nullptr;

void TP_MAKE_THISCALL(HookLockForRead, BSReadWriteLock)
{
    TiltedPhoques::ThisCall(RealLockForRead, apThis);

    if (s_inDiagnostics || !IsExtraDataListLock(apThis))
        return;
    s_inDiagnostics = true;

    // The hook is jumped to from the game's function, so this is the code that asked for the lock.
    const ReadHold hold{apThis, Clock::now(), _ReturnAddress()};

    auto& thread = GetThreadHolds();
    {
        std::scoped_lock _(thread.Mutex);
        thread.Holds.push_back(hold);
    }

    s_inDiagnostics = false;
}

bool ForgetReadHold(ThreadHolds& aThread, const BSReadWriteLock* apLock)
{
    std::scoped_lock _(aThread.Mutex);
    for (auto it = aThread.Holds.rbegin(); it != aThread.Holds.rend(); ++it)
    {
        if (it->pLock == apLock)
        {
            aThread.Holds.erase(std::next(it).base());
            return true;
        }
    }
    return false;
}

void TP_MAKE_THISCALL(HookUnlockForRead, BSReadWriteLock)
{
    if (!s_inDiagnostics && IsExtraDataListLock(apThis))
    {
        s_inDiagnostics = true;

        // A read lock is normally released on the thread that took it; look elsewhere only if not.
        if (!ForgetReadHold(GetThreadHolds(), apThis))
        {
            std::scoped_lock _(s_registryMutex);
            for (auto* pThread : s_registry)
            {
                if (ForgetReadHold(*pThread, apThis))
                    break;
            }
        }

        s_inDiagnostics = false;
    }

    TiltedPhoques::ThisCall(RealUnlockForRead, apThis);
}

void TP_MAKE_THISCALL(HookLockForWrite, BSReadWriteLock)
{
    // Wait like the game does, so a write lock that never comes free can be reported, then let the game take it.
    if (!s_inDiagnostics && apThis->uiWriterThread != GetCurrentThreadId())
    {
        const auto start = Clock::now();
        for (uint32_t spins = 0; apThis->uiLock != 0; ++spins)
        {
            if (Clock::now() - start >= kStallAfter)
            {
                s_inDiagnostics = true;
                ReportStall(apThis, start, _ReturnAddress());
                s_inDiagnostics = false;
                break;
            }

            Sleep(spins < 10000 ? 0 : 1);
        }
    }

    TiltedPhoques::ThisCall(RealLockForWrite, apThis);
}
} // namespace

static TiltedPhoques::Initializer s_lockDiagnosticsHooks(
    []()
    {
        POINTER_SKYRIMSE(TLockFunction, lockForRead, 68233);
        POINTER_SKYRIMSE(TLockFunction, unlockForRead, 68239);
        POINTER_SKYRIMSE(TLockFunction, lockForWrite, 68234);
        // BaseExtraList's vtable, found through its RTTI (.?AVBaseExtraList@@) on 1.6.1170 and 1.7.104.
        POINTER_SKYRIMSE(void*, extraDataListVtable, 422045);

        s_pExtraDataListVtable = extraDataListVtable.Get();
        RealLockForRead = lockForRead.Get();
        RealUnlockForRead = unlockForRead.Get();
        RealLockForWrite = lockForWrite.Get();

        TP_HOOK(&RealLockForRead, HookLockForRead);
        TP_HOOK(&RealUnlockForRead, HookUnlockForRead);
        TP_HOOK(&RealLockForWrite, HookLockForWrite);

        spdlog::info("Lock diagnostics: following ExtraDataList readers, reporting write locks stalled for {} s", kStallAfter.count());
    });
