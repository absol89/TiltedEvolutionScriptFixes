#include <Messages/NotifyPartyOptions.h>

void NotifyPartyOptions::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Options.Serialize(aWriter);
}

void NotifyPartyOptions::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    Options.Deserialize(aReader);
}
