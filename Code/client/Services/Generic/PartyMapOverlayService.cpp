#include <TiltedOnlinePCH.h>

#include <Events/UpdateEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/PartyJoinedEvent.h>
#include <Events/PartyLeftEvent.h>
#include <Events/SetWaypointEvent.h>

#include <Messages/NotifyPlayerCellChanged.h>
#include <Messages/PartyPositionUpdateRequest.h>

#include <Services/PartyMapOverlayService.h>
#include <Services/PartyService.h>
#include <Services/OverlayService.h>

#include <Components.h>
#include <World.h>

#include <algorithm>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <cmath>

#include <Games/Skyrim/Interface/UI.h>
#include <Games/Skyrim/Camera/PlayerCamera.h>
#include <Games/Skyrim/Renderer.h>
#include <Games/Skyrim/PlayerCharacter.h>
#include <Forms/TESWorldSpace.h>
#include <Forms/TESObjectCELL.h>
#include <Games/Skyrim/TESObjectREFR.h>
#include <Games/Skyrim/Interface/Menus/HUDMenuUtils.h>
#include <Games/Skyrim/WorldMapProjector.h>

PartyMapOverlayService::PartyMapOverlayService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
{
    // Update position cache each frame
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&PartyMapOverlayService::OnUpdate>(this);


    // Network-driven worldspace updates and lifecycle events
    m_cellChangedConnection = aDispatcher.sink<NotifyPlayerCellChanged>().connect<&PartyMapOverlayService::OnPlayerCellChanged>(this);
    m_positionsConnection = aDispatcher.sink<NotifyPartyPositions>().connect<&PartyMapOverlayService::OnPartyPositions>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&PartyMapOverlayService::OnDisconnected>(this);
    m_partyJoinedConnection = aDispatcher.sink<PartyJoinedEvent>().connect<&PartyMapOverlayService::OnPartyJoined>(this);
    m_partyLeftConnection = aDispatcher.sink<PartyLeftEvent>().connect<&PartyMapOverlayService::OnPartyLeft>(this);
}

void PartyMapOverlayService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    m_last.clear();
    m_worlds.clear();
    m_lastPerWorld.clear();
    m_lastScreen.clear();
}

void PartyMapOverlayService::OnPartyJoined(const PartyJoinedEvent&) noexcept
{
    PruneNonPartyEntries();
}

void PartyMapOverlayService::OnPartyLeft(const PartyLeftEvent&) noexcept
{
    m_last.clear();
    m_worlds.clear();
    m_lastPerWorld.clear();
    m_lastScreen.clear();
}

void PartyMapOverlayService::OnPlayerCellChanged(const NotifyPlayerCellChanged& aMsg) noexcept
{
    // Track worldspace per player when available
    auto& modSystem = m_world.GetModSystem();

    WorldspaceInfo info{};
    if (aMsg.WorldSpaceId)
    {
        info.WorldSpaceFormId = modSystem.GetGameId(aMsg.WorldSpaceId);
        info.HasWorld = info.WorldSpaceFormId != 0;
    }
    else
    {
        // Interior cells have no worldspace; skip for now (map UI is worldspace-only)
        info.WorldSpaceFormId = 0;
        info.HasWorld = false;
    }

    m_worlds[aMsg.PlayerId] = info;
}

void PartyMapOverlayService::OnPartyPositions(const NotifyPartyPositions& aMsg) noexcept
{
    // Update caches from server-sent party positions (covers far-away/unloaded players)
    const uint64_t tick = m_world.GetTick();
    auto& modSystem = m_world.GetModSystem();

    for (const auto& e : aMsg.Entries)
    {
        m_last[e.PlayerId] = LastInfo{glm::vec3{e.Position.x, e.Position.y, e.Position.z}, tick};

        WorldspaceInfo info{};
        if (e.WorldSpaceId)
        {
            info.WorldSpaceFormId = modSystem.GetGameId(e.WorldSpaceId);
            info.HasWorld = info.WorldSpaceFormId != 0;
        }
        else
        {
            info.WorldSpaceFormId = 0;
            info.HasWorld = false; // interior or unknown
        }
        m_worlds[e.PlayerId] = info;

        // Track last known position per worldspace for cross-world projection
        if (info.HasWorld)
        {
            m_lastPerWorld[e.PlayerId][info.WorldSpaceFormId] = glm::vec3{e.Position.x, e.Position.y, e.Position.z};
        }
    }
}


void PartyMapOverlayService::PruneNonPartyEntries() noexcept
{
    const auto& members = m_world.GetPartyService().GetPartyMembers();
    TiltedPhoques::Vector<uint32_t> toErase;
    toErase.reserve(std::max<size_t>(m_last.size(), m_worlds.size()));

    for (const auto& [pid, _] : m_last)
    {
        if (std::find(members.begin(), members.end(), pid) == members.end())
            toErase.push_back(pid);
    }
    for (auto id : toErase)
        m_last.erase(id);

    toErase.clear();
    for (const auto& [pid, _] : m_worlds)
    {
        if (std::find(members.begin(), members.end(), pid) == members.end())
            toErase.push_back(pid);
    }
    for (auto id : toErase)
        m_worlds.erase(id);

    // Prune per-world history for non-members
    toErase.clear();
    for (const auto& [pid, _] : m_lastPerWorld)
    {
        if (std::find(members.begin(), members.end(), pid) == members.end())
            toErase.push_back(pid);
    }
    for (auto id : toErase)
        m_lastPerWorld.erase(id);

    // Prune last-screen cache for non-members
    toErase.clear();
    for (const auto& [pid, _] : m_lastScreen)
    {
        if (std::find(members.begin(), members.end(), pid) == members.end())
            toErase.push_back(pid);
    }
    for (auto id : toErase)
        m_lastScreen.erase(id);
}

void PartyMapOverlayService::OnUpdate(const UpdateEvent&) noexcept
{
    const uint64_t tick = m_world.GetTick();

    // Cache last known positions for loaded remote players
    auto view = m_world.view<PlayerComponent, InterpolationComponent>();
    for (auto e : view)
    {
        const auto& pc = view.get<PlayerComponent>(e);
        const auto& interp = view.get<InterpolationComponent>(e);
        m_last[pc.Id] = LastInfo{interp.Position, tick};
    }

    // Periodically send our own position to the server for party tracking
    {
        static auto s_lastPosSend = std::chrono::steady_clock::time_point{};
        constexpr auto cPosInterval = std::chrono::milliseconds(250);
        const auto now = std::chrono::steady_clock::now();
        if (now - s_lastPosSend >= cPosInterval)

        {
            s_lastPosSend = now;
            if (auto* pPc = PlayerCharacter::Get())
            {
                PartyPositionUpdateRequest req{};
                req.Position.x = pPc->position.x;
                req.Position.y = pPc->position.y;
                req.Position.z = pPc->position.z;

                auto& modSystem = m_world.GetModSystem();
                if (auto* pWs = pPc->GetWorldSpace())
                    modSystem.GetServerModId(pWs->formID, req.WorldSpaceId);
                else
                    req.WorldSpaceId = {};

                if (pPc->parentCell)
                    modSystem.GetServerModId(pPc->parentCell->formID, req.CellId);
                else
                    req.CellId = {};

                m_world.GetTransport().Send(req);
            }
        }
    }

    // Prune very stale entries (>10 minutes)
    const uint64_t maxAge = 10ull * 60ull * 1000ull;
    for (auto it = m_last.begin(); it != m_last.end();)
    {
        if (tick - it->second.Tick > maxAge)
            it = m_last.erase(it);
        else
            ++it;
    }

    // Build and send pins to CEF overlay at a throttled rate
    static auto s_lastSend = std::chrono::steady_clock::time_point{};
    constexpr auto cDelayBetweenUpdates = std::chrono::milliseconds(33); // ~30 FPS for smoother pins
    const auto now = std::chrono::steady_clock::now();
    if (now - s_lastSend < cDelayBetweenUpdates)
        return;
    s_lastSend = now;

    auto& partyService = m_world.GetPartyService();
    UI* pUI = UI::Get();
    const bool mapOpen = pUI && pUI->GetMenuOpen(BSFixedString("MapMenu"));
    if (!mapOpen)
    {
        m_world.GetOverlayService().SetPartyPinsJson("[]");
        return;
    }

    // Determine current worldspace of the local player and the map's display world (root ancestor)
    TESWorldSpace* pMyWs = nullptr;
    if (auto* pPc = PlayerCharacter::Get())
        pMyWs = pPc->GetWorldSpace();
    TESWorldSpace* pDispWs = WorldMapProjector::GetDisplayWorld(pMyWs);
    const uint32_t dispWsId = pDispWs ? pDispWs->formID : 0u;

    // Screen size
    auto* pRenderer = BGSRenderer::Get();
    const float width = pRenderer ? static_cast<float>(pRenderer->windowWidth) : 0.f;
    const float height = pRenderer ? static_cast<float>(pRenderer->windowHeight) : 0.f;

    auto* pCam = PlayerCamera::Get();
    if (!pCam || width <= 0.f || height <= 0.f)
    {
        // If we can't project, don't draw anything
        m_world.GetOverlayService().SetPartyPinsJson("[]");
        return;
    }

    // If not in a party or only ourselves, don't draw anything
    const auto& members = partyService.GetPartyMembers();
    if (!partyService.IsInParty() || members.size() <= 1)
    {
        m_world.GetOverlayService().SetPartyPinsJson("[]");
        return;
    }

    const uint32_t localId = m_world.GetTransport().GetLocalPlayerId();

    std::ostringstream os;
    os.setf(std::ios::fixed); os << std::setprecision(1);
    os << "[";
    bool first = true;

    for (uint32_t pid : members)
    {
        if (pid == localId)
            continue; // don't display ourselves on the map

        const auto itPos = m_last.find(pid);
        const auto itWs = m_worlds.find(pid);
        if (itPos == m_last.end())
            continue; // no position at all yet

        const bool hasWorld = (itWs != m_worlds.end()) && itWs->second.HasWorld && itWs->second.WorldSpaceFormId != 0;
        float sx = 0.f, sy = 0.f;
        bool drew = false;

        // 1) If member already in display worldspace, project directly
        if (hasWorld && itWs->second.WorldSpaceFormId == dispWsId)
        {
            NiPoint3 wpos(itPos->second.Pos);
            NiPoint3 spos{};
            if (HUDMenuUtils::WorldPtToScreenPt3(wpos, spos))
            {
                sx = std::clamp(spos.x, 0.0f, 1.0f) * width;
                sy = (1.f - std::clamp(spos.y, 0.0f, 1.0f)) * height;
                drew = true;
            }
        }

        // 2) Otherwise, convert into display world via WRLD ONAM (child->ancestor only)
        if (!drew && hasWorld && dispWsId != 0 && itWs->second.WorldSpaceFormId != dispWsId && pDispWs)
        {
            if (auto* pFromWs = static_cast<TESWorldSpace*>(TESForm::GetById(itWs->second.WorldSpaceFormId)))
            {
                glm::vec3 dstPos{};
                if (WorldMapProjector::Convert(pFromWs, itPos->second.Pos, pDispWs, dstPos))
                {
                    NiPoint3 wpos(dstPos);
                    NiPoint3 spos{};
                    if (HUDMenuUtils::WorldPtToScreenPt3(wpos, spos))
                    {
                        sx = std::clamp(spos.x, 0.0f, 1.0f) * width;
                        sy = (1.f - std::clamp(spos.y, 0.0f, 1.0f)) * height;
                        drew = true;
                    }
                }
            }
        }

        // 3) Approximation using per-player anchors (convert into display world)
        if (!drew && hasWorld && dispWsId != 0 && itWs->second.WorldSpaceFormId != dispWsId)
        {
            glm::vec3 dstPos{};
            if (ComputeCrossWorldApprox(pid, itWs->second.WorldSpaceFormId, itPos->second.Pos, dispWsId, dstPos))
            {
                NiPoint3 wpos(dstPos);
                NiPoint3 spos{};
                if (HUDMenuUtils::WorldPtToScreenPt3(wpos, spos))
                {
                    sx = std::clamp(spos.x, 0.0f, 1.0f) * width;
                    sy = (1.f - std::clamp(spos.y, 0.0f, 1.0f)) * height;
                    drew = true;
                }
            }
        }

        // 4) Fallback to last known position already in display world
        if (!drew)
        {
            auto itHistPid = m_lastPerWorld.find(pid);
            if (itHistPid != m_lastPerWorld.end())
            {
                auto itDispPos = itHistPid->second.find(dispWsId);
                if (itDispPos != itHistPid->second.end())
                {
                    const auto& p = itDispPos->second;
                    NiPoint3 wpos(p);
                    NiPoint3 spos{};
                    if (HUDMenuUtils::WorldPtToScreenPt3(wpos, spos))
                    {
                        sx = std::clamp(spos.x, 0.0f, 1.0f) * width;
                        sy = (1.f - std::clamp(spos.y, 0.0f, 1.0f)) * height;
                        drew = true;
                    }
                }
            }
        }

        // 5) Retain last projected screen position briefly to hide gaps during transitions
        if (!drew)
        {
            constexpr uint64_t cKeepMs = 3000; // 3 seconds
            auto itLastScr = m_lastScreen.find(pid);
            if (itLastScr != m_lastScreen.end() && (tick - itLastScr->second.Tick) <= cKeepMs)
            {
                sx = itLastScr->second.sx;
                sy = itLastScr->second.sy;
                drew = true;
            }
        }

        if (!drew)
            continue; // nothing to draw for this member on this map

        // Smooth towards new projected position using an exponential moving average
        auto itLS = m_lastScreen.find(pid);
        if (itLS != m_lastScreen.end())
        {
            const uint64_t dt = tick - itLS->second.Tick;
            // Time constant ~150ms for quick but smooth convergence
            float alpha = 1.0f - std::exp(-static_cast<float>(dt) / 150.0f);
            if (alpha < 0.f) alpha = 0.f; if (alpha > 1.f) alpha = 1.f;
            sx = itLS->second.sx + (sx - itLS->second.sx) * alpha;
            sy = itLS->second.sy + (sy - itLS->second.sy) * alpha;
        }

        // Cache last projected (smoothed) position
        m_lastScreen[pid] = LastScreen{sx, sy, tick};

        if (!first) os << ","; first = false;
        os << "{\"x\":" << sx << ",\"y\":" << sy << ",\"id\":" << pid << "}";
    }

    // If nothing to draw, send empty list
    if (first)
    {
        m_world.GetOverlayService().SetPartyPinsJson("[]");
        return;
    }

    os << "]";
    m_world.GetOverlayService().SetPartyPinsJson(os.str());
}


bool PartyMapOverlayService::ComputeCrossWorldApprox(uint32_t aPlayerId, uint32_t aSrcWsId, const glm::vec3& aSrcPos,
                                                     uint32_t aDstWsId, glm::vec3& aOutDstPos) const noexcept
{
    auto itHistPid = m_lastPerWorld.find(aPlayerId);
    if (itHistPid == m_lastPerWorld.end())
        return false;

    const auto& perWorld = itHistPid->second;
    auto itSrc = perWorld.find(aSrcWsId);
    auto itDst = perWorld.find(aDstWsId);
    if (itSrc == perWorld.end() || itDst == perWorld.end())
        return false;

    const glm::vec3 srcAnchor = itSrc->second;
    const glm::vec3 dstAnchor = itDst->second;

    // Translation-only approximation: assumes no rotation/scale differences.
    aOutDstPos = dstAnchor + (aSrcPos - srcAnchor);
    return true;
}

void PartyMapOverlayService::SetWaypointFor(uint32_t aPlayerId) noexcept
{
    const auto itPos = m_last.find(aPlayerId);
    if (itPos == m_last.end())
        return;

    const auto itWs = m_worlds.find(aPlayerId);
    if (itWs == m_worlds.end() || !itWs->second.HasWorld || itWs->second.WorldSpaceFormId == 0)
        return; // cannot set map waypoint without a worldspace

    Vector3_NetQuantize pos{};
    pos.x = itPos->second.Pos.x;
    pos.y = itPos->second.Pos.y;
    pos.z = itPos->second.Pos.z;

    m_world.GetDispatcher().trigger(SetWaypointEvent(pos, itWs->second.WorldSpaceFormId));
}


