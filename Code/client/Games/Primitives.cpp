#include <TiltedOnlinePCH.h>

#include <Games/Primitives.h>

void NiRefObject::IncRef()
{
    InterlockedIncrement(&refCount);
}

void NiRefObject::DecRef()
{
    if (Release())
        Destroy();
}

bool NiRefObject::Release()
{
    return InterlockedDecrement(&refCount) == 0;
}

void BSRecursiveLock::Lock()
{
    const auto tid = GetCurrentThreadId();
    _mm_lfence();

    if (m_tid == tid)
    {
        InterlockedIncrement(&m_counter);
    }
    else
    {
        if (InterlockedCompareExchange(&m_counter, 1, 0))
        {
            while (true)
            {
                _mm_pause();
                if (!InterlockedCompareExchange(&m_counter, 1, 0))
                {
                    break;
                }
                std::this_thread::yield();
            }

            _mm_lfence();
        }

        m_tid = tid;
        _mm_sfence();
    }
}

void BSRecursiveLock::Unlock()
{
    _mm_lfence();
    if (m_tid == GetCurrentThreadId())
    {
        if (m_counter == 1)
        {
            m_tid = 0;
            _mm_mfence();
            InterlockedCompareExchange(&m_counter, 0, 1);
        }
        else
        {
            InterlockedDecrement(&m_counter);
        }
    }
}

void BSReadWriteLock::LockForRead()
{
    TP_THIS_FUNCTION(TLockForRead, void, BSReadWriteLock);
    POINTER_SKYRIMSE(TLockForRead, lockForRead, 68233);
    TiltedPhoques::ThisCall(lockForRead, this);
}

void BSReadWriteLock::UnlockForRead()
{
    TP_THIS_FUNCTION(TUnlockForRead, void, BSReadWriteLock);
    POINTER_SKYRIMSE(TUnlockForRead, unlockForRead, 68239);
    TiltedPhoques::ThisCall(unlockForRead, this);
}

void BSReadWriteLock::LockForWrite()
{
    TP_THIS_FUNCTION(TLockForWrite, void, BSReadWriteLock);
    POINTER_SKYRIMSE(TLockForWrite, lockForWrite, 68234);
    TiltedPhoques::ThisCall(lockForWrite, this);
}

void BSReadWriteLock::UnlockForWrite()
{
    TP_THIS_FUNCTION(TUnlockForWrite, void, BSReadWriteLock);
    POINTER_SKYRIMSE(TUnlockForWrite, unlockForWrite, 68240);
    TiltedPhoques::ThisCall(unlockForWrite, this);
}
