#include <Messages/NotifyPlayerPerks.h>

#include <TiltedCore/Serialization.hpp>

void NotifyPlayerPerks::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, PlayerId);
    Serialization::WriteVarInt(aWriter, Perks.size());

    for (const auto& perk : Perks)
        perk.Serialize(aWriter);
}

void NotifyPlayerPerks::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    PlayerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Perks.resize(Serialization::ReadVarInt(aReader));

    for (auto& perk : Perks)
        perk.Deserialize(aReader);
}
