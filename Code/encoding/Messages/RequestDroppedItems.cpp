#include <Messages/RequestDroppedItems.h>
#include <TiltedCore/Serialization.hpp>

void RequestDroppedItems::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, RequestId);
    Serialization::WriteBool(aWriter, RequestAll);
    Serialization::WriteBool(aWriter, HasCellFilter);
    if (HasCellFilter)
        CellId.Serialize(aWriter);

    Serialization::WriteBool(aWriter, HasWorldSpaceFilter);
    if (HasWorldSpaceFilter)
        WorldSpaceId.Serialize(aWriter);
}

void RequestDroppedItems::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    RequestId = Serialization::ReadVarInt(aReader);
    RequestAll = Serialization::ReadBool(aReader);
    HasCellFilter = Serialization::ReadBool(aReader);
    if (HasCellFilter)
        CellId.Deserialize(aReader);

    HasWorldSpaceFilter = Serialization::ReadBool(aReader);
    if (HasWorldSpaceFilter)
        WorldSpaceId.Deserialize(aReader);
}
