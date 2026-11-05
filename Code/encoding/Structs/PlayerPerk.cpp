#include <Structs/PlayerPerk.h>

#include <TiltedCore/Serialization.hpp>

using TiltedPhoques::Serialization;

void PlayerPerk::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Id.Serialize(aWriter);
    aWriter.WriteBits(Rank, 8);
}

void PlayerPerk::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    Id.Deserialize(aReader);

    uint64_t rank = 0;
    aReader.ReadBits(rank, 8);
    Rank = rank & 0xFF;
}
