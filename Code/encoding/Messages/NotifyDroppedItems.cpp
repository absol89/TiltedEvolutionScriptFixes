#include <Messages/NotifyDroppedItems.h>
#include <TiltedCore/Serialization.hpp>

void NotifyDroppedItems::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, RequestId);
    Serialization::WriteVarInt(aWriter, Entries.size());
    for (const auto& entry : Entries)
    {
        Serialization::WriteVarInt(aWriter, entry.DropId);
        Serialization::WriteVarInt(aWriter, entry.ServerId);
        Serialization::WriteVarInt(aWriter, entry.ActorFormId);
        Serialization::WriteVarInt(aWriter, entry.SpawnEpoch);
        entry.Item.Serialize(aWriter);

        Serialization::WriteBool(aWriter, entry.HasLocation);
        if (entry.HasLocation)
            entry.Location.Serialize(aWriter);

        Serialization::WriteBool(aWriter, entry.HasRotation);
        if (entry.HasRotation)
            entry.Rotation.Serialize(aWriter);

        entry.CellId.Serialize(aWriter);
        entry.WorldSpaceId.Serialize(aWriter);
        entry.ReferenceId.Serialize(aWriter);
    }
}

void NotifyDroppedItems::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    RequestId = Serialization::ReadVarInt(aReader);
    const auto count = Serialization::ReadVarInt(aReader);
    Entries.clear();
    Entries.reserve(count);

    for (size_t i = 0; i < count; ++i)
    {
        Entry entry{};
        entry.DropId = Serialization::ReadVarInt(aReader);
        entry.ServerId = Serialization::ReadVarInt(aReader);
        entry.ActorFormId = Serialization::ReadVarInt(aReader);
        entry.SpawnEpoch = Serialization::ReadVarInt(aReader);
        entry.Item.Deserialize(aReader);

        entry.HasLocation = Serialization::ReadBool(aReader);
        if (entry.HasLocation)
            entry.Location.Deserialize(aReader);

        entry.HasRotation = Serialization::ReadBool(aReader);
        if (entry.HasRotation)
            entry.Rotation.Deserialize(aReader);

        entry.CellId.Deserialize(aReader);
        entry.WorldSpaceId.Deserialize(aReader);
        entry.ReferenceId.Deserialize(aReader);

        Entries.push_back(std::move(entry));
    }
}
