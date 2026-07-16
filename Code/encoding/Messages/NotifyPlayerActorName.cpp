#include <Messages/NotifyPlayerActorName.h>

void NotifyPlayerActorName::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, PlayerId);
    Serialization::WriteString(aWriter, ActorName);
}

void NotifyPlayerActorName::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    PlayerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    ActorName = Serialization::ReadString(aReader);
}
