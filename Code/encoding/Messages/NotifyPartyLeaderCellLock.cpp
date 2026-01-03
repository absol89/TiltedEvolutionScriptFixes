#include <Messages/NotifyPartyLeaderCellLock.h>

#include <TiltedCore/Serialization.hpp>

void NotifyPartyLeaderCellLock::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    WorldSpaceId.Serialize(aWriter);
    CellId.Serialize(aWriter);
    Position.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, CountdownSeconds);
    Serialization::WriteBool(aWriter, Cancelled);
}

void NotifyPartyLeaderCellLock::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    WorldSpaceId.Deserialize(aReader);
    CellId.Deserialize(aReader);
    Position.Deserialize(aReader);
    CountdownSeconds = static_cast<uint16_t>(Serialization::ReadVarInt(aReader) & 0xFFFF);
    Cancelled = Serialization::ReadBool(aReader);
}
