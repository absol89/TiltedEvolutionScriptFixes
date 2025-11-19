#include <Messages/NotifyInventoryChanges.h>
#include <TiltedCore/Serialization.hpp>

void NotifyInventoryChanges::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, ServerId);
    Item.Serialize(aWriter);
    Serialization::WriteBool(aWriter, Drop);
    Serialization::WriteBool(aWriter, Silent);
    Serialization::WriteBool(aWriter, HasDropInstanceId);
    if (HasDropInstanceId)
        Serialization::WriteVarInt(aWriter, DropInstanceId);
    Serialization::WriteBool(aWriter, HasDropLocation);
    if (HasDropLocation)
        DropLocation.Serialize(aWriter);
    Serialization::WriteBool(aWriter, HasDropRotation);
    if (HasDropRotation)
        DropRotation.Serialize(aWriter);
}

void NotifyInventoryChanges::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    ServerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Item.Deserialize(aReader);
    Drop = Serialization::ReadBool(aReader);
    Silent = Serialization::ReadBool(aReader);
    HasDropInstanceId = Serialization::ReadBool(aReader);
    if (HasDropInstanceId)
        DropInstanceId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    HasDropLocation = Serialization::ReadBool(aReader);
    if (HasDropLocation)
        DropLocation.Deserialize(aReader);
    HasDropRotation = Serialization::ReadBool(aReader);
    if (HasDropRotation)
        DropRotation.Deserialize(aReader);
}
