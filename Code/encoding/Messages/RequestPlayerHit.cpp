#include <Messages/RequestPlayerHit.h>

void RequestPlayerHit::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, HitteeId);
    Serialization::WriteVarInt(aWriter, AttackerId);
    WeaponId.Serialize(aWriter);
    Serialization::WriteFloat(aWriter, Damage);
    Serialization::WriteVarInt(aWriter, Flags);
}

void RequestPlayerHit::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    HitteeId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    AttackerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    WeaponId.Deserialize(aReader);
    Damage = Serialization::ReadFloat(aReader);
    Flags = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
}
