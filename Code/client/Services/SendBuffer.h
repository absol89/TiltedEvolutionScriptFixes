#pragma once

#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>
#include <TiltedCore/Stl.hpp>
#include <Messages/Message.h>

namespace SendBuffer
{
// The writer does not retain failed-write state. Remember every rejected growth
// attempt, including a large write followed by smaller writes that still fit.
class LimitedAllocator final : public TiltedPhoques::Allocator
{
public:
    LimitedAllocator(TiltedPhoques::Allocator& aAllocator, size_t aCapacity) noexcept
        : m_allocator(aAllocator), m_capacity(aCapacity)
    {
    }

    void* Allocate(size_t aSize) noexcept override
    {
        if (aSize > m_capacity)
        {
            m_failed = true;
            return nullptr;
        }
        void* pData = m_allocator.Allocate(aSize);
        m_failed |= pData == nullptr;
        return pData;
    }
    void Free(void* apData) noexcept override { m_allocator.Free(apData); }
    size_t Size(void* apData) noexcept override { return m_allocator.Size(apData); }
    bool Failed() const noexcept { return m_failed; }

private:
    TiltedPhoques::Allocator& m_allocator;
    size_t m_capacity;
    bool m_failed = false;
};

class PacketBuffer
{
public:
    PacketBuffer(const ClientMessage& acMessage, TiltedPhoques::Allocator& aAllocator, size_t aCapacity) noexcept
        : m_allocator(aAllocator, aCapacity), m_writer(&m_buffer)
    {
        m_buffer.SetAllocator(&m_allocator);
        if (!m_buffer.Resize(aCapacity))
            return;
        m_writer.WriteBits(0, 8); // Packet layer header.
        acMessage.Serialize(m_writer);
    }

    TP_NOCOPYMOVE(PacketBuffer);
    bool IsComplete() const noexcept { return !m_allocator.Failed(); }
    uint8_t* GetData() const noexcept { return m_buffer.GetWriteData(); }
    size_t Size() const noexcept { return m_writer.Size(); }

private:
    LimitedAllocator m_allocator;
    TiltedPhoques::Buffer m_buffer;
    TiltedPhoques::Buffer::Writer m_writer;
};
}
