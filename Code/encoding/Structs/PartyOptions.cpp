#include <Structs/PartyOptions.h>
#include <TiltedCore/Serialization.hpp>

void PartyOptions::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    TiltedPhoques::Serialization::WriteVarInt(aWriter, FlagsMask);
}

void PartyOptions::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    FlagsMask = static_cast<uint32_t>(TiltedPhoques::Serialization::ReadVarInt(aReader));
}

void PartyOptions::SetFlag(uint32_t aFlag, bool aEnabled) noexcept
{
    if (aEnabled)
        FlagsMask |= aFlag;
    else
        FlagsMask &= ~aFlag;
}

void PartyOptions::SetSyncFastTravelMarkers(bool aEnabled) noexcept
{
    SetFlag(kSyncFastTravelMarkers, aEnabled);
}

void PartyOptions::SetShowPartyMemberMarkers(bool aEnabled) noexcept
{
    SetFlag(kShowPartyMemberMarkers, aEnabled);
}
