#include <Messages/PlayerPerksRequest.h>

#include <TiltedCore/Serialization.hpp>

void PlayerPerksRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Perks.size());

    for (const auto& perk : Perks)
        perk.Serialize(aWriter);
}

void PlayerPerksRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    Perks.resize(Serialization::ReadVarInt(aReader));

    for (auto& perk : Perks)
        perk.Deserialize(aReader);
}
