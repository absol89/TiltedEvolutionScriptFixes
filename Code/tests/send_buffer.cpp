#include "../client/Services/SendBuffer.h"
#include <TiltedCore/ScratchAllocator.hpp>
#include <Messages/AuthenticationRequest.h>
#include <catch2/catch.hpp>
#include <vector>

namespace
{
struct PayloadMessage final : ClientMessage
{
    explicit PayloadMessage(size_t aSize) : ClientMessage(kAuthenticationRequest), Data(aSize, 0x5A) {}
    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override
    {
        aWriter.WriteBytes(Data.data(), Data.size());
        aWriter.WriteBits(0xAB, 8); // A successful write after a failed large write must not hide the failure.
    }
    std::vector<uint8_t> Data;
};
}

TEST_CASE("Client send buffer rejects partial messages and accepts exact fits", "[encoding.send]")
{
    auto& allocator = *TiltedPhoques::Allocator::GetDefault();
    constexpr size_t kCapacity = 512 * 1024;
    const PayloadMessage empty(0);
    const SendBuffer::PacketBuffer overhead(empty, allocator, 64);
    REQUIRE(overhead.IsComplete());

    const PayloadMessage exact(kCapacity - overhead.Size());
    const SendBuffer::PacketBuffer full(exact, allocator, kCapacity);
    REQUIRE(full.IsComplete());
    REQUIRE(full.Size() == kCapacity);
    REQUIRE(full.GetData()[kCapacity - 1] == 0xAB);

    const PayloadMessage oversized(kCapacity);
    const SendBuffer::PacketBuffer rejected(oversized, allocator, kCapacity);
    REQUIRE_FALSE(rejected.IsComplete());
    REQUIRE(rejected.Size() < kCapacity);
}

TEST_CASE("Large authentication retries after the scratch buffer overflows", "[encoding.send]")
{
    AuthenticationRequest request;
    for (uint16_t i = 0; i < 2000; ++i)
        request.UserMods.ModList.push_back({TiltedPhoques::String(40, 'x') + ".esp", i, true});
    TiltedPhoques::ScratchAllocator scratch(1 << 18);
    const SendBuffer::PacketBuffer small(request, scratch, 1 << 16);
    REQUIRE_FALSE(small.IsComplete());

    const SendBuffer::PacketBuffer large(request, *TiltedPhoques::Allocator::GetDefault(), 512 * 1024);
    REQUIRE(large.IsComplete());
    TiltedPhoques::Buffer copy(large.GetData(), large.Size());
    TiltedPhoques::Buffer::Reader reader(&copy);
    uint64_t header = 1;
    REQUIRE(reader.ReadBits(header, 8));
    REQUIRE(header == 0);
    uint64_t opcode = 0;
    REQUIRE(reader.ReadBits(opcode, 8));
    REQUIRE(opcode == kAuthenticationRequest);
    AuthenticationRequest decoded;
    decoded.DeserializeRaw(reader);
    REQUIRE(decoded == request);
}
