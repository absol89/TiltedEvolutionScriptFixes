#include <Messages/PlayerActorNameUpdateRequest.h>

#include <TiltedCore/Serialization.hpp>

void PlayerActorNameUpdateRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteString(aWriter, ActorName);
}

void PlayerActorNameUpdateRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);
    ActorName = Serialization::ReadString(aReader);
}
