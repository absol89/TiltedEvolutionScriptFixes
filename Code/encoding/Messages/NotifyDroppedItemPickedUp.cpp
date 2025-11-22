#include <Messages/NotifyDroppedItemPickedUp.h>
#include <TiltedCore/Serialization.hpp>

void NotifyDroppedItemPickedUp::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, ServerId);
    Item.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, DropId);
}

void NotifyDroppedItemPickedUp::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    ServerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Item.Deserialize(aReader);
    DropId = Serialization::ReadVarInt(aReader);
}
