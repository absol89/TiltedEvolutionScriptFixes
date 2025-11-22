#include <Messages/NotifyActorDrop.h>
#include <TiltedCore/Serialization.hpp>

void NotifyActorDrop::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, ServerId);
    Item.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, DropId);
    Serialization::WriteBool(aWriter, HasClientDropId);
    if (HasClientDropId)
        Serialization::WriteVarInt(aWriter, ClientDropId);
    Serialization::WriteBool(aWriter, HasLocation);
    if (HasLocation)
        Location.Serialize(aWriter);
    Serialization::WriteBool(aWriter, HasRotation);
    if (HasRotation)
        Rotation.Serialize(aWriter);
}

void NotifyActorDrop::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    ServerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Item.Deserialize(aReader);
    DropId = Serialization::ReadVarInt(aReader);
    HasClientDropId = Serialization::ReadBool(aReader);
    if (HasClientDropId)
        ClientDropId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    HasLocation = Serialization::ReadBool(aReader);
    if (HasLocation)
        Location.Deserialize(aReader);
    HasRotation = Serialization::ReadBool(aReader);
    if (HasRotation)
        Rotation.Deserialize(aReader);
}
