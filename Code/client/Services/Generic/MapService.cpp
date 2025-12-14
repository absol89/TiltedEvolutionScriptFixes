#include <Services/MapService.h>

#include <World.h>

#include <Events/UpdateEvent.h>
#include <Events/PartyJoinedEvent.h>
#include <Events/SetWaypointEvent.h>
#include <Events/RemoveWaypointEvent.h>
#include <Messages/PartyFastTravelMarkersRequest.h>
#include <Messages/NotifySetWaypoint.h>
#include <Messages/NotifyRemoveWaypoint.h>
#include <Messages/NotifyPartyFastTravelMarkers.h>
#include <Messages/RequestSetWaypoint.h>
#include <Messages/RequestRemoveWaypoint.h>

#include <ExtraData/ExtraMapMarker.h>

#include <PlayerCharacter.h>

MapService::MapService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld), m_dispatcher(aDispatcher), m_transport(aTransport)
{
    m_updateConnection = m_dispatcher.sink<UpdateEvent>().connect<&MapService::OnUpdate>(this);

    m_playerNotifySetWaypointConnection =
        m_dispatcher.sink<NotifySetWaypoint>().connect<&MapService::OnNotifySetWaypoint>(this);
    m_playerNotifyRemoveWaypointConnection =
        m_dispatcher.sink<NotifyRemoveWaypoint>().connect<&MapService::OnNotifyRemoveWaypoint>(this);
    m_playerSetWaypointConnection =
        m_dispatcher.sink<SetWaypointEvent>().connect<&MapService::OnSetWaypoint>(this);
    m_playerRemoveWaypointConnection =
        m_dispatcher.sink<RemoveWaypointEvent>().connect<&MapService::OnRemoveWaypoint>(this);

    m_partyFastTravelMarkersConnection =
        m_dispatcher.sink<NotifyPartyFastTravelMarkers>().connect<&MapService::OnNotifyPartyFastTravelMarkers>(this);
    m_partyJoinedConnection =
        m_dispatcher.sink<PartyJoinedEvent>().connect<&MapService::OnPartyJoined>(this);
}

namespace
{
constexpr uint8_t kMapMarkerFlag_Visible = 1 << 0;
constexpr uint8_t kMapMarkerFlag_CanTravelTo = 1 << 1;

inline bool IsFastTravelMarker(TESObjectREFR* apRefr) noexcept
{
    if (!apRefr)
        return false;

    auto* pExtraData = apRefr->GetExtraDataList();
    if (!pExtraData)
        return false;

    auto* pExtraMapMarker = static_cast<const ExtraMapMarker*>(pExtraData->GetByType(ExtraDataType::MapMarker));
    if (!pExtraMapMarker || !pExtraMapMarker->mapData)
        return false;

    const uint8_t flags = pExtraMapMarker->mapData->flags;
    return (flags & kMapMarkerFlag_Visible) && (flags & kMapMarkerFlag_CanTravelTo);
}

inline bool DiscoverFastTravelMarker(TESObjectREFR* apMarkerRefr) noexcept
{
    if (!apMarkerRefr)
        return false;

    auto* pExtraData = apMarkerRefr->GetExtraDataList();
    if (!pExtraData)
        return false;

    auto* pExtraMapMarker = static_cast<ExtraMapMarker*>(pExtraData->GetByType(ExtraDataType::MapMarker));
    if (!pExtraMapMarker || !pExtraMapMarker->mapData)
        return false;

    pExtraMapMarker->mapData->flags |= (kMapMarkerFlag_Visible | kMapMarkerFlag_CanTravelTo);

    auto* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return false;

    const auto handle = apMarkerRefr->GetHandle();
    if (!handle)
        return false;

    auto* pCurrentMarkers = pPlayer->GetCurrentMapMarkers();
    if (!pCurrentMarkers)
        return true;

    auto& currentMarkers = *pCurrentMarkers;
    for (uint32_t i = 0; i < currentMarkers.length; ++i)
    {
        if (currentMarkers[i].handle.iBits == handle.handle.iBits)
            return true;
    }

    const uint32_t oldLen = currentMarkers.length;
    currentMarkers.Resize(oldLen + 1);
    currentMarkers[oldLen] = handle;

    return true;
}
}

void MapService::OnUpdate(const UpdateEvent&) noexcept
{
    ProcessPendingFastTravelMarkers();

    if (!m_transport.IsConnected())
        return;

    if (!m_world.Get().GetPartyService().IsInParty())
        return;

    const auto now = m_transport.GetClock().GetCurrentTick();
    if (m_nextMarkerScanTick > now)
        return;

    // Scan at most twice per second.
    m_nextMarkerScanTick = now + 500;

    auto* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return;

    const auto* pCurrentMarkers = pPlayer->GetCurrentMapMarkers();
    if (!pCurrentMarkers)
        return;

    const auto& currentMarkers = *pCurrentMarkers;

    TiltedPhoques::Vector<GameId> newlyDiscovered{};
    newlyDiscovered.reserve(currentMarkers.length);

    ModSystem& modSystem = m_world.Get().GetModSystem();

    for (uint32_t i = 0; i < currentMarkers.length; ++i)
    {
        const uint32_t handleBits = currentMarkers[i].handle.iBits;
        if (handleBits == 0)
            continue;

        TESObjectREFR* pMarker = TESObjectREFR::GetByHandle(handleBits);
        if (!pMarker)
            continue;

        if (!IsFastTravelMarker(pMarker))
            continue;

        GameId markerId{};
        if (!modSystem.GetServerModId(pMarker->formID, markerId))
            continue;

        if (m_knownFastTravelMarkers.insert(markerId).second)
            newlyDiscovered.push_back(markerId);
    }

    SendFastTravelMarkers(newlyDiscovered);
}

void MapService::OnSetWaypoint(const SetWaypointEvent& acMessage) noexcept
{
    if (!m_transport.IsConnected())
        return;

    RequestSetWaypoint request{};
    request.Position = acMessage.Position;

    ModSystem& modSystem = m_world.Get().GetModSystem();
    modSystem.GetServerModId(acMessage.WorldSpaceFormID, request.WorldSpaceFormID);

    m_transport.Send(request);
}

void MapService::OnRemoveWaypoint(const RemoveWaypointEvent& acMessage) noexcept
{
    if (!m_transport.IsConnected())
        return;

    RequestRemoveWaypoint request{};
    m_transport.Send(request);
}

void MapService::OnNotifySetWaypoint(const NotifySetWaypoint& acMessage) noexcept
{
    NiPoint3 pos{};
    pos.x = acMessage.Position.x;
    pos.y = acMessage.Position.y;
    pos.z = acMessage.Position.z;

    ModSystem& modSystem = m_world.Get().GetModSystem();
    const uint32_t cWorldSpaceID = modSystem.GetGameId(acMessage.WorldSpaceFormID);
    TESWorldSpace* pWorldSpace = Cast<TESWorldSpace>(TESForm::GetById(cWorldSpaceID));

    PlayerCharacter::Get()->SetWaypoint(&pos, pWorldSpace);
}

void MapService::OnNotifyRemoveWaypoint(const NotifyRemoveWaypoint& acMessage) noexcept
{
    PlayerCharacter::Get()->RemoveWaypoint();
}

void MapService::OnNotifyPartyFastTravelMarkers(const NotifyPartyFastTravelMarkers& acMessage) noexcept
{
    // Queue for retry to handle missing mod resolution / unloaded references.
    for (const auto& marker : acMessage.Markers)
    {
        if (!marker)
            continue;

        if (m_knownFastTravelMarkers.insert(marker).second)
            m_pendingFastTravelMarkers.push_back(marker);
    }

    ProcessPendingFastTravelMarkers();
}

void MapService::OnPartyJoined(const PartyJoinedEvent&) noexcept
{
    // Full sync on join to ensure two-way union.
    m_nextMarkerScanTick = 0;

    spdlog::debug("Party joined: syncing fast travel markers");

    const auto markers = CollectLocalFastTravelMarkers();
    for (const auto& marker : markers)
        m_knownFastTravelMarkers.insert(marker);

    spdlog::debug("Party joined: sending {} fast travel markers", markers.size());
    SendFastTravelMarkers(markers);
}

void MapService::SendFastTravelMarkers(const TiltedPhoques::Vector<GameId>& aMarkers) noexcept
{
    if (aMarkers.empty())
        return;

    if (!m_transport.IsConnected())
        return;

    if (!m_world.Get().GetPartyService().IsInParty())
        return;

    PartyFastTravelMarkersRequest request{};
    request.Markers = aMarkers;
    m_transport.Send(request);
}

TiltedPhoques::Vector<GameId> MapService::CollectLocalFastTravelMarkers() const noexcept
{
    TiltedPhoques::Set<GameId> unique{};

    auto* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return {};

    const auto* pCurrentMarkers = pPlayer->GetCurrentMapMarkers();
    if (!pCurrentMarkers)
        return {};

    const auto& currentMarkers = *pCurrentMarkers;
    unique.reserve(currentMarkers.length);

    ModSystem& modSystem = m_world.Get().GetModSystem();

    for (uint32_t i = 0; i < currentMarkers.length; ++i)
    {
        const uint32_t handleBits = currentMarkers[i].handle.iBits;
        if (handleBits == 0)
            continue;

        TESObjectREFR* pMarker = TESObjectREFR::GetByHandle(handleBits);
        if (!pMarker)
            continue;

        if (!IsFastTravelMarker(pMarker))
            continue;

        GameId markerId{};
        if (modSystem.GetServerModId(pMarker->formID, markerId))
            unique.insert(markerId);
    }

    TiltedPhoques::Vector<GameId> out{};
    out.reserve(unique.size());
    for (const auto& marker : unique)
        out.push_back(marker);

    return out;
}

void MapService::ProcessPendingFastTravelMarkers() noexcept
{
    if (m_pendingFastTravelMarkers.empty())
        return;

    ModSystem& modSystem = m_world.Get().GetModSystem();

    TiltedPhoques::Vector<GameId> remaining{};
    remaining.reserve(m_pendingFastTravelMarkers.size());

    for (const auto& marker : m_pendingFastTravelMarkers)
    {
        const uint32_t gameFormId = modSystem.GetGameId(marker);
        if (gameFormId == 0)
        {
            remaining.push_back(marker);
            continue;
        }

        TESObjectREFR* pMarker = Cast<TESObjectREFR>(TESForm::GetById(gameFormId));
        if (!pMarker)
        {
            remaining.push_back(marker);
            continue;
        }

        if (!DiscoverFastTravelMarker(pMarker))
        {
            remaining.push_back(marker);
            continue;
        }
    }

    m_pendingFastTravelMarkers = std::move(remaining);
}
