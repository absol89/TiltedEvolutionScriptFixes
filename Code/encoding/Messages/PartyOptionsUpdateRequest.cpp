#include <Messages/PartyOptionsUpdateRequest.h>

void PartyOptionsUpdateRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Options.Serialize(aWriter);
}

void PartyOptionsUpdateRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    Options.Deserialize(aReader);
}
