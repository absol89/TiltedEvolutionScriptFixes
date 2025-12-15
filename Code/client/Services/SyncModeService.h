#pragma once

#include <Structs/SyncMode.h>

#include <TiltedCore/Stl.hpp>

struct World;
struct TransportService;
struct Actor;

struct ConnectedEvent;
struct DisconnectedEvent;
struct UpdateEvent;
struct NotifyPlayerJoined;
struct NotifyPlayerLeft;
struct NotifyPlayerSyncMode;
struct ShaderReferenceEffect;

/**
 * @brief Keeps track of player sync modes and applies ghost visuals locally.
 */
struct SyncModeService
{
    SyncModeService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept;
    ~SyncModeService() noexcept = default;

    TP_NOCOPYMOVE(SyncModeService);

    void SetLocalMode(SyncMode aMode) noexcept;
    [[nodiscard]] SyncMode GetLocalMode() const noexcept { return m_localMode; }
    void OnActor3DUpdated(Actor* apActor) noexcept;

private:
    void OnConnected(const ConnectedEvent& acEvent) noexcept;
    void OnDisconnected(const DisconnectedEvent&) noexcept;
    void OnUpdate(const UpdateEvent&) noexcept;
    void OnPlayerJoined(const NotifyPlayerJoined& acMessage) noexcept;
    void OnPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept;
    void OnNotifyPlayerSyncMode(const NotifyPlayerSyncMode& acMessage) noexcept;
    void OnRemoteComponentRemoved(entt::registry& aRegistry, entt::entity aEntity) noexcept;
    void RequestResync() const noexcept;
    void UpdateOverlaySyncStatus() const noexcept;

    bool ShouldGhost(uint32_t aPlayerId) const noexcept;
    void RefreshGhostStates() noexcept;
    bool ToggleGhostState(entt::entity aEntity, bool aGhost) noexcept;
    bool ApplyGhostToActor(Actor* apActor, bool aGhost) noexcept;

    World& m_world;
    entt::dispatcher& m_dispatcher;
    TransportService& m_transport;

    SyncMode m_localMode{SyncMode::Normal};
    uint32_t m_localPlayerId{0};

    TiltedPhoques::Map<uint32_t, SyncMode> m_remoteModes;
    TiltedPhoques::Set<uint32_t> m_pending3DRefresh;
    TiltedPhoques::Set<uint32_t> m_glowApplied;

    entt::scoped_connection m_connectedConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_playerJoinedConnection;
    entt::scoped_connection m_playerLeftConnection;
    entt::scoped_connection m_notifySyncModeConnection;
    entt::scoped_connection m_remoteRemovedConnection;
};
