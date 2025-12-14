#pragma once

#include <Structs/GameId.h>

struct World;
struct TransportService;

struct UpdateEvent;
struct SetWaypointEvent;
struct RemoveWaypointEvent;
struct NotifySetWaypoint;
struct NotifyRemoveWaypoint;
struct NotifyPartyFastTravelMarkers;
struct PartyJoinedEvent;

/**
 * @brief Handles map-related synchronization (waypoints, party fast travel markers).
 */
struct MapService
{
    MapService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept;
    ~MapService() noexcept = default;

    TP_NOCOPYMOVE(MapService);

  protected:
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnSetWaypoint(const SetWaypointEvent& acMessage) noexcept;
    void OnRemoveWaypoint(const RemoveWaypointEvent& acMessage) noexcept;
    void OnNotifySetWaypoint(const NotifySetWaypoint& acMessage) noexcept;
    void OnNotifyRemoveWaypoint(const NotifyRemoveWaypoint& acMessage) noexcept;
    void OnNotifyPartyFastTravelMarkers(const NotifyPartyFastTravelMarkers& acMessage) noexcept;
    void OnPartyJoined(const PartyJoinedEvent& acMessage) noexcept;

  private:
    World& m_world;
    entt::dispatcher& m_dispatcher;
    TransportService& m_transport;

    void SendFastTravelMarkers(const TiltedPhoques::Vector<GameId>& aMarkers) noexcept;
    TiltedPhoques::Vector<GameId> CollectLocalFastTravelMarkers() const noexcept;
    void ProcessPendingFastTravelMarkers() noexcept;

    uint64_t m_nextMarkerScanTick{0};
    TiltedPhoques::Set<GameId> m_knownFastTravelMarkers{};
    TiltedPhoques::Vector<GameId> m_pendingFastTravelMarkers{};

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_playerSetWaypointConnection;
    entt::scoped_connection m_playerRemoveWaypointConnection;
    entt::scoped_connection m_playerNotifySetWaypointConnection;
    entt::scoped_connection m_playerNotifyRemoveWaypointConnection;
    entt::scoped_connection m_partyFastTravelMarkersConnection;
    entt::scoped_connection m_partyJoinedConnection;
};
