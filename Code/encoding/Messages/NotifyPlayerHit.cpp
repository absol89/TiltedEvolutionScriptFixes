#include <Messages/NotifyPlayerHit.h>

void NotifyPlayerHit::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, HitteeId);
    Serialization::WriteVarInt(aWriter, AttackerId);
    WeaponId.Serialize(aWriter);
    Serialization::WriteFloat(aWriter, Damage);
    Serialization::WriteVarInt(aWriter, Flags);
}

void NotifyPlayerHit::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    HitteeId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    AttackerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    WeaponId.Deserialize(aReader);
    Damage = Serialization::ReadFloat(aReader);
    Flags = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
}
