#include <Messages/RequestPickupDroppedItem.h>
#include <TiltedCore/Serialization.hpp>

void RequestPickupDroppedItem::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, ServerId);
    Serialization::WriteVarInt(aWriter, DropId);
}

void RequestPickupDroppedItem::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    ServerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    DropId = Serialization::ReadVarInt(aReader);
}
