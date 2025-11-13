#include <TiltedOnlinePCH.h>

#include <Games/Skyrim/WorldMapProjector.h>
#include <Forms/TESWorldSpace.h>
#include <Games/Skyrim/Forms/TESForm.h>

#include <components/es_loader/ESLoader.h>
#include <components/es_loader/RecordCollection.h>
#include <components/es_loader/Records/WRLD.h>

namespace {
struct MapLink {
    uint32_t parentId{0};
    float xOff{0.f};
    float yOff{0.f};
    float zOff{0.f};
};

static bool g_loaded = false;
static TiltedPhoques::Map<uint32_t, MapLink> g_links; // worldId -> mapping to its parent

static void EnsureLoaded() noexcept {
    if (g_loaded)
        return;
    g_loaded = true;

    try {
        ESLoader::ESLoader loader;
        auto records = loader.BuildRecordCollection();
        if (!records || !records->HasAnyRecords())
            return;

        for (const auto& [fid, wrld] : records->GetWorlds())
        {
            MapLink link{};
            if (wrld.m_parentId)
                link.parentId = *wrld.m_parentId;
            if (wrld.m_onam)
            {
                link.xOff = wrld.m_onam->m_cellOffsetX4096;
                link.yOff = wrld.m_onam->m_cellOffsetY4096;
                link.zOff = wrld.m_onam->m_cellOffsetZ4096;
            }
            if (link.parentId != 0)
                g_links[fid] = link;
        }
    } catch (...) {
        // Swallow any file IO exceptions; mapping will simply be unavailable.
    }
}

// Walk up from child world to target parent, accumulating offsets.
static bool ToAncestor(uint32_t fromId, uint32_t targetAncestor, glm::vec3 srcPos, glm::vec3& outPos) noexcept {
    EnsureLoaded();
    if (fromId == 0 || targetAncestor == 0)
        return false;
    if (fromId == targetAncestor)
    {
        outPos = srcPos;
        return true;
    }

    glm::vec3 p = srcPos;
    uint32_t cur = fromId;
    // Guard against cycles / very deep chains
    for (int i = 0; i < 16; ++i)
    {
        auto it = g_links.find(cur);
        if (it == g_links.end())
            return false;
        const auto& link = it->second;
        // Apply offset to go into parent coordinates
        p.x += link.xOff;
        p.y += link.yOff;
        p.z += link.zOff;
        if (link.parentId == targetAncestor)
        {
            outPos = p;
            return true;
        }
        cur = link.parentId;
    }
    return false;
}

static uint32_t RootAncestorId(uint32_t wsId) noexcept
{
    EnsureLoaded();
    uint32_t cur = wsId;
    for (int i = 0; i < 16; ++i)
    {
        auto it = g_links.find(cur);
        if (it == g_links.end() || it->second.parentId == 0)
            return cur;
        cur = it->second.parentId;
    }
    return cur;
}
} // namespace

bool WorldMapProjector::Convert(TESWorldSpace* apFromWs,
                                const glm::vec3& aFromPos,
                                TESWorldSpace* apToWs,
                                glm::vec3& aOutToPos) noexcept
{
    if (!apFromWs || !apToWs)
        return false;

    const uint32_t fromId = apFromWs->formID;
    const uint32_t toId = apToWs->formID;

    if (fromId == toId)
    {
        aOutToPos = aFromPos;
        return true;
    }

    // Only support mapping up the parent chain (child -> parent -> ...)
    if (ToAncestor(fromId, toId, aFromPos, aOutToPos))
        return true;

    return false;
}

TESWorldSpace* WorldMapProjector::GetDisplayWorld(TESWorldSpace* apWs) noexcept
{
    if (!apWs)
        return nullptr;
    const uint32_t rootId = RootAncestorId(apWs->formID);
    if (rootId == apWs->formID)
        return apWs;
    auto* pForm = TESForm::GetById(rootId);
    return pForm ? static_cast<TESWorldSpace*>(pForm) : apWs;
}

