#include "CoSaveStorage.h"

#include <TiltedCore/Buffer.hpp>

#include <spdlog/spdlog.h>

#include <cstring>
#include <fstream>
#include <vector>

namespace
{
constexpr uint32_t kCoSaveMagic = 'T' | ('S' << 8) | ('C' << 16) | ('O' << 24);
constexpr uint32_t kCoSaveVersion = 2;
} // namespace

namespace CoSaveStorage
{
bool Load(const std::filesystem::path& aPath, TiltedPhoques::Map<uint64_t, Entry>& aOut) noexcept
{
    aOut.clear();

    std::ifstream input(aPath, std::ios::binary);
    if (!input.is_open())
        return true;

    uint32_t magic = 0;
    uint32_t version = 0;
    uint32_t buildId = 0;

    input.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    input.read(reinterpret_cast<char*>(&version), sizeof(version));
    input.read(reinterpret_cast<char*>(&buildId), sizeof(buildId));
    if (!input || magic != kCoSaveMagic || version == 0)
    {
        spdlog::warn("CoSaveStorage: invalid header for '{}', ignoring cache", aPath.string());
        return false;
    }

    uint32_t count = 0;
    input.read(reinterpret_cast<char*>(&count), sizeof(count));
    if (!input)
        return false;

    if (version == 1)
    {
        for (uint32_t i = 0; i < count; ++i)
        {
            Entry entry{};
            input.read(reinterpret_cast<char*>(&entry.DropId), sizeof(entry.DropId));
            input.read(reinterpret_cast<char*>(&entry.ServerId), sizeof(entry.ServerId));

            input.read(reinterpret_cast<char*>(&entry.CellId.ModId), sizeof(entry.CellId.ModId));
            input.read(reinterpret_cast<char*>(&entry.CellId.BaseId), sizeof(entry.CellId.BaseId));
            input.read(reinterpret_cast<char*>(&entry.WorldSpaceId.ModId), sizeof(entry.WorldSpaceId.ModId));
            input.read(reinterpret_cast<char*>(&entry.WorldSpaceId.BaseId), sizeof(entry.WorldSpaceId.BaseId));

            input.read(reinterpret_cast<char*>(&entry.ReferenceId.ModId), sizeof(entry.ReferenceId.ModId));
            input.read(reinterpret_cast<char*>(&entry.ReferenceId.BaseId), sizeof(entry.ReferenceId.BaseId));

            input.read(reinterpret_cast<char*>(&entry.Location), sizeof(entry.Location));
            input.read(reinterpret_cast<char*>(&entry.Rotation), sizeof(entry.Rotation));
            input.read(reinterpret_cast<char*>(&entry.RefFormId), sizeof(entry.RefFormId));
            input.read(reinterpret_cast<char*>(&entry.LastSeenTimestamp), sizeof(entry.LastSeenTimestamp));

            uint32_t itemSize = 0;
            input.read(reinterpret_cast<char*>(&itemSize), sizeof(itemSize));
            if (!input || itemSize == 0)
                continue;

            std::vector<uint8_t> itemBuffer(itemSize);
            input.read(reinterpret_cast<char*>(itemBuffer.data()), itemSize);
            if (!input)
                continue;

            TiltedPhoques::ViewBuffer view(itemBuffer.data(), itemBuffer.size());
            TiltedPhoques::Buffer::Reader reader(&view);
            entry.Item.Deserialize(reader);

            aOut[entry.DropId] = entry;
        }

        return true;
    }

    for (uint32_t i = 0; i < count; ++i)
    {
        uint32_t entrySize = 0;
        input.read(reinterpret_cast<char*>(&entrySize), sizeof(entrySize));
        if (!input || entrySize == 0)
            continue;

        std::vector<uint8_t> entryBuffer(entrySize);
        input.read(reinterpret_cast<char*>(entryBuffer.data()), entrySize);
        if (!input)
            continue;

        size_t offset = 0;
        auto readBytes = [&](void* dest, size_t size) -> bool {
            if (offset + size > entryBuffer.size())
                return false;
            std::memcpy(dest, entryBuffer.data() + offset, size);
            offset += size;
            return true;
        };

        Entry entry{};
        if (!readBytes(&entry.DropId, sizeof(entry.DropId)))
            continue;
        if (!readBytes(&entry.ServerId, sizeof(entry.ServerId)))
            continue;
        if (!readBytes(&entry.CellId.ModId, sizeof(entry.CellId.ModId)))
            continue;
        if (!readBytes(&entry.CellId.BaseId, sizeof(entry.CellId.BaseId)))
            continue;
        if (!readBytes(&entry.WorldSpaceId.ModId, sizeof(entry.WorldSpaceId.ModId)))
            continue;
        if (!readBytes(&entry.WorldSpaceId.BaseId, sizeof(entry.WorldSpaceId.BaseId)))
            continue;
        if (!readBytes(&entry.ReferenceId.ModId, sizeof(entry.ReferenceId.ModId)))
            continue;
        if (!readBytes(&entry.ReferenceId.BaseId, sizeof(entry.ReferenceId.BaseId)))
            continue;
        if (!readBytes(&entry.Location, sizeof(entry.Location)))
            continue;
        if (!readBytes(&entry.Rotation, sizeof(entry.Rotation)))
            continue;
        if (!readBytes(&entry.RefFormId, sizeof(entry.RefFormId)))
            continue;
        if (!readBytes(&entry.LastSeenTimestamp, sizeof(entry.LastSeenTimestamp)))
            continue;

        uint32_t itemSize = 0;
        if (!readBytes(&itemSize, sizeof(itemSize)))
            continue;
        if (itemSize == 0 || offset + itemSize > entryBuffer.size())
            continue;

        TiltedPhoques::ViewBuffer view(entryBuffer.data() + offset, itemSize);
        TiltedPhoques::Buffer::Reader reader(&view);
        entry.Item.Deserialize(reader);

        aOut[entry.DropId] = entry;
    }

    return true;
}

bool Save(const std::filesystem::path& aPath, const TiltedPhoques::Map<uint64_t, Entry>& aIn) noexcept
{
    std::ofstream output(aPath, std::ios::binary | std::ios::trunc);
    if (!output.is_open())
    {
        spdlog::error("CoSaveStorage: failed to save cache '{}'", aPath.string());
        return false;
    }

    const uint32_t magic = kCoSaveMagic;
    const uint32_t version = kCoSaveVersion;
    const uint32_t buildId = 0;
    const uint32_t count = static_cast<uint32_t>(aIn.size());

    output.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    output.write(reinterpret_cast<const char*>(&version), sizeof(version));
    output.write(reinterpret_cast<const char*>(&buildId), sizeof(buildId));
    output.write(reinterpret_cast<const char*>(&count), sizeof(count));

    for (const auto& [dropId, entry] : aIn)
    {
        TiltedPhoques::Buffer itemBuffer(1 << 12);
        TiltedPhoques::Buffer::Writer itemWriter(&itemBuffer);
        entry.Item.Serialize(itemWriter);
        const uint32_t itemSize = static_cast<uint32_t>(itemWriter.Size());

        std::vector<uint8_t> payload;
        payload.reserve(256 + itemSize);

        auto append = [&](const void* data, size_t size) {
            const auto* bytes = static_cast<const uint8_t*>(data);
            payload.insert(payload.end(), bytes, bytes + size);
        };

        append(&entry.DropId, sizeof(entry.DropId));
        append(&entry.ServerId, sizeof(entry.ServerId));
        append(&entry.CellId.ModId, sizeof(entry.CellId.ModId));
        append(&entry.CellId.BaseId, sizeof(entry.CellId.BaseId));
        append(&entry.WorldSpaceId.ModId, sizeof(entry.WorldSpaceId.ModId));
        append(&entry.WorldSpaceId.BaseId, sizeof(entry.WorldSpaceId.BaseId));
        append(&entry.ReferenceId.ModId, sizeof(entry.ReferenceId.ModId));
        append(&entry.ReferenceId.BaseId, sizeof(entry.ReferenceId.BaseId));
        append(&entry.Location, sizeof(entry.Location));
        append(&entry.Rotation, sizeof(entry.Rotation));
        append(&entry.RefFormId, sizeof(entry.RefFormId));
        append(&entry.LastSeenTimestamp, sizeof(entry.LastSeenTimestamp));
        append(&itemSize, sizeof(itemSize));
        append(itemBuffer.GetWriteData(), itemSize);

        const uint32_t entrySize = static_cast<uint32_t>(payload.size());
        output.write(reinterpret_cast<const char*>(&entrySize), sizeof(entrySize));
        output.write(reinterpret_cast<const char*>(payload.data()), payload.size());
    }

    if (!output)
        return false;

    return true;
}
} // namespace CoSaveStorage
