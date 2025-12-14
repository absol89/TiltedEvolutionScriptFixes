#include <Messages/PartyFastTravelMarkersRequest.h>

void PartyFastTravelMarkersRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(static_cast<uint64_t>(Markers.size()), 16);
    for (const auto& marker : Markers)
        marker.Serialize(aWriter);
}

void PartyFastTravelMarkersRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    uint64_t count = 0;
    aReader.ReadBits(count, 16);

    Markers.clear();
    Markers.reserve(count);
    for (uint64_t i = 0; i < count; ++i)
    {
        GameId marker{};
        marker.Deserialize(aReader);
        Markers.push_back(marker);
    }
}

