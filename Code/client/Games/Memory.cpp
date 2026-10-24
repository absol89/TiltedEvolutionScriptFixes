#include <TiltedOnlinePCH.h>

#include <Games/Memory.h>
#include <Games/References.h>

#include <TiltedCore/MimallocAllocator.hpp>
#include <mimalloc.h>

#pragma optimize("", off)

struct GameHeap
{
    static GameHeap* Get()
    {
        POINTER_SKYRIMSE(GameHeap, s_gameHeap, 400188);

        return s_gameHeap.Get();
    }
};

TP_THIS_FUNCTION(TFormAllocate, void*, GameHeap, size_t aSize, size_t aAlignment, bool aAligned);
TP_THIS_FUNCTION(TFormFree, void, GameHeap, void* apPtr, bool aAligned);

TFormAllocate* RealFormAllocate = nullptr;
TFormFree* RealFormFree = nullptr;

static TiltedPhoques::MimallocAllocator s_allocator;

void* TP_MAKE_THISCALL(HookFormAllocate, GameHeap, size_t aSize, size_t aAlignment, bool aAligned)
{
    switch (aSize)
    {
    case sizeof(Actor): aSize = sizeof(ExActor); break;
    case sizeof(PlayerCharacter): aSize = sizeof(ExPlayerCharacter); break;
    default: break;
    }

    auto* pPointer = TiltedPhoques::ThisCall(RealFormAllocate, apThis, aSize, aAlignment, aAligned);

    if (!pPointer)
        return nullptr;

    ActorExtension* pExtension = nullptr;

    switch (aSize)
    {
    case sizeof(ExActor): pExtension = static_cast<ActorExtension*>(static_cast<ExActor*>(pPointer)); break;
    case sizeof(ExPlayerCharacter): pExtension = static_cast<ActorExtension*>(static_cast<ExPlayerCharacter*>(pPointer)); break;
    default: break;
    }

    if (pExtension)
    {
        new (pExtension) ActorExtension;
    }

    return pPointer;
}

void* Memory::Allocate(const size_t aSize) noexcept
{
    return TiltedPhoques::ThisCall(HookFormAllocate, GameHeap::Get(), aSize, 0, false);
}

void Memory::Free(void* apData) noexcept
{
    TiltedPhoques::ThisCall(RealFormFree, GameHeap::Get(), apData, false);
}

static bool IsFormAllocateReplacedByEF(TFormAllocate** appOutEngineFixesAlloc) noexcept
{
    POINTER_SKYRIMSE(TFormAllocate, s_formAllocate, 68115);
    TFormAllocate* pFormAllocate = s_formAllocate.Get();

    auto opcodeBytes = reinterpret_cast<uint16_t*>(*pFormAllocate);
    uint8_t shift = 0;

    if (*opcodeBytes == 0x25FF) // 'jmp' opcode 'FF 25' and the 4-byte displacement bytes come before the virtual address we're after
        shift = 6;
    else if (*opcodeBytes == 0xB848) // 'mov' opcode '48 B8' comes before the virtual address we're after
        shift = 2;

    auto possibleEfAllocAddress = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uint8_t*>(*pFormAllocate) + shift);

    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery((void*)possibleEfAllocAddress, &mbi, sizeof(mbi)) != 0)
    {
        if (mbi.AllocationBase == GetModuleHandleW(L"EngineFixes.dll"))
        {
            *appOutEngineFixesAlloc = reinterpret_cast<TFormAllocate*>(possibleEfAllocAddress);
            return true;
        }
    }
    return false;
}

static void RehookFormAllocate(TFormAllocate* apEngineFixesAllocate) noexcept
{
    RealFormAllocate = apEngineFixesAllocate;
    TP_HOOK_IMMEDIATE(&RealFormAllocate, HookFormAllocate);
}

size_t Hook_msize(void* apData)
{
    return mi_malloc_size(apData);
}

void Hookfree(void* apData)
{
    mi_free(apData);
}

void* Hookcalloc(size_t aCount, size_t aSize)
{
    return mi_calloc(aCount, aSize);
}

void* Hookmalloc(size_t aSize)
{
    return mi_malloc(aSize);
}

void Hook_aligned_free(void* apData)
{
    mi_free(apData);
}

void* Hook_aligned_malloc(size_t aSize, size_t aAlignment)
{
    return mi_malloc_aligned(aSize, aAlignment);
}

// Playtest builds, not meant for upstream. mimalloc's secure mode reports a double free (EAGAIN), a corrupted free list
// (EFAULT) or an invalid pointer (EINVAL) where it is detected; the handler then crashes on the spot, so CrashLogger
// records the stack of the code that freed the block instead of a hang somewhere later. Messages go to
// logs\mimalloc_errors.log through the Windows file API, as the mimalloc heap cannot be trusted at that point.
static constexpr DWORD kMimallocErrorException = 0xE04D494D; // "MIM"

static void WriteMimallocLog(const char* acpText) noexcept
{
    const HANDLE cFile = CreateFileA("Data\\SkyrimTogetherReborn\\logs\\mimalloc_errors.log", FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (cFile == INVALID_HANDLE_VALUE)
        return;

    DWORD written = 0;
    WriteFile(cFile, acpText, static_cast<DWORD>(strlen(acpText)), &written, nullptr);
    CloseHandle(cFile);
}

static void OnMimallocOutput(const char* acpMessage, void*) noexcept
{
    OutputDebugStringA(acpMessage);
    WriteMimallocLog(acpMessage);
}

static void OnMimallocError(int aError, void*) noexcept
{
    char line[96];
    sprintf_s(line, "mimalloc error %d on thread %lu\r\n", aError, GetCurrentThreadId());
    WriteMimallocLog(line);

    // An invalid pointer is a free of memory mimalloc never handed out. No playtest session logged one by mistake, so it
    // crashes too.
    if (aError == EAGAIN || aError == EFAULT || aError == EINVAL)
        RaiseException(kMimallocErrorException, EXCEPTION_NONCONTINUABLE, 0, nullptr);
}

static TiltedPhoques::Initializer s_memoryHooks(
    []()
    {
        mi_register_output(&OnMimallocOutput, nullptr);
        mi_register_error(&OnMimallocError, nullptr);
        mi_option_enable(mi_option_show_errors);

        POINTER_SKYRIMSE(TFormAllocate, s_formAllocate, 68115);

        POINTER_SKYRIMSE(TFormFree, s_formFree, 68117);

        RealFormAllocate = s_formAllocate.Get();
        RealFormFree = s_formFree.Get();

        using T_msize = decltype(&Hook_msize);
        using Tfree = decltype(&Hookfree);
        using Tcalloc = decltype(&Hookcalloc);
        using Tmalloc = decltype(&Hookmalloc);
        using T_aligned_malloc = decltype(&Hook_aligned_malloc);
        using T_aligned_free = decltype(&Hook_aligned_free);
        T_msize Real_msize = nullptr;
        Tfree Realfree = nullptr;
        Tcalloc Realcalloc = nullptr;
        Tmalloc Realmalloc = nullptr;
        T_aligned_malloc Real_aligned_malloc = nullptr;
        T_aligned_free Real_aligned_free = nullptr;

        const char* cModuleName = "api-ms-win-crt-heap-l1-1-0.dll";

        TP_HOOK_IAT(_msize, cModuleName);
        TP_HOOK_IAT(free, cModuleName);
        TP_HOOK_IAT(calloc, cModuleName);
        TP_HOOK_IAT(malloc, cModuleName);
        TP_HOOK_IAT(_aligned_malloc, cModuleName);
        TP_HOOK_IAT(_aligned_free, cModuleName);

        TP_HOOK(&RealFormAllocate, HookFormAllocate);
    });

using T_initterm_e = decltype(&_initterm_e);
T_initterm_e Real_initterm_e = nullptr;

// If EngineFixes loaded, and it changed our FormAllocate hook, 
// reset it. Our hook works just fine chaining to theirs.
int __cdecl Hook_initterm_e(_PIFV* apFirst, _PIFV* apLast)
{
    // We want to run last, so pre-chain.
    auto retval = Real_initterm_e(apFirst, apLast); 
    
    // Check if EngineFixes messed with STR's modified alloc hook; if it did, treat EF as truth and rehook
    TFormAllocate* pEngineFixesAllocate = nullptr;
    if (GetModuleHandleW(L"EngineFixes.dll") && IsFormAllocateReplacedByEF(&pEngineFixesAllocate))
        RehookFormAllocate(pEngineFixesAllocate);

    return retval;
}

void HookFormAllocateSentinelInit()
{
    TP_HOOK_IAT(_initterm_e, "api-ms-win-crt-runtime-l1-1-0.dll");
}
#pragma optimize("", on)
