#pragma once

#include <RemotePerks.h>

struct World;
struct TransportService;

struct UpdateEvent;
struct ConnectedEvent;
struct DisconnectedEvent;
struct ServerSettings;
struct GridCellChangeEvent;
struct CellChangeEvent;
struct PlayerDialogueEvent;
struct PlayerLevelEvent;
struct PartyJoinedEvent;
struct PartyLeftEvent;

struct NotifyPlayerRespawn;
struct NotifyPlayerPerks;
struct NotifyPlayerJoined;
struct NotifyPlayerLeft;

/**
 * @brief Handles logic related to the local player.
 */
struct PlayerService
{
    PlayerService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept;
    ~PlayerService() noexcept = default;

    TP_NOCOPYMOVE(PlayerService);

protected:
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnConnected(const ConnectedEvent& acEvent) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void OnServerSettingsReceived(const ServerSettings& acSettings) noexcept;
    void OnNotifyPlayerRespawn(const NotifyPlayerRespawn& acMessage) const noexcept;
    void OnGridCellChangeEvent(const GridCellChangeEvent& acEvent) const noexcept;
    void OnCellChangeEvent(const CellChangeEvent& acEvent) const noexcept;
    void OnPlayerDialogueEvent(const PlayerDialogueEvent& acEvent) const noexcept;
    void OnPlayerLevelEvent(const PlayerLevelEvent& acEvent) const noexcept;
    void OnPartyJoinedEvent(const PartyJoinedEvent& acEvent) noexcept;
    void OnPartyLeftEvent(const PartyLeftEvent& acEvent) noexcept;
    void OnNotifyPlayerPerks(const NotifyPlayerPerks& acMessage) noexcept;
    void OnNotifyPlayerJoined(const NotifyPlayerJoined& acMessage) noexcept;
    void OnNotifyPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept;

private:
    /**
     * @brief Run the respawn timer, and if it hits 0, respawn the player.
     */
    void RunRespawnUpdates() noexcept;
    void RunPostDeathUpdates() noexcept;
    /**
     * @brief Make sure difficulty doesn't get changed while connected
     */
    void RunDifficultyUpdates() const noexcept;
    void RunLevelUpdates() const noexcept;
    void RunBeastFormDetection() const noexcept;
    /**
     * @brief Share the local player's perks when they change, and tie the remote players' perks to their copies.
     */
    void RunPerkUpdates() noexcept;

    void ToggleDeathSystem(bool aSet) noexcept;

    World& m_world;
    entt::dispatcher& m_dispatcher;
    TransportService& m_transport;

    std::chrono::steady_clock::time_point m_respawnDeadline;
    int32_t m_serverDifficulty = 6;
    int32_t m_previousDifficulty = 6;

    bool m_isDeathSystemEnabled = true;

    bool m_knockdownStart = false;
    std::chrono::steady_clock::time_point m_knockdownDeadline;

    bool m_godmodeStart = false;
    std::chrono::steady_clock::time_point m_godmodeDeadline;

    uint32_t m_cachedMainSpellId = 0;
    uint32_t m_cachedSecondarySpellId = 0;
    uint32_t m_cachedPowerId = 0;

    std::vector<std::pair<GameId, uint8_t>> m_sentPerks;
    bool m_arePerksSent = false;
    std::unordered_map<uint32_t, RemotePerks::PerkDataPtr> m_remotePerks;

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_connectedConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_settingsConnection;
    entt::scoped_connection m_notifyRespawnConnection;
    entt::scoped_connection m_gridCellChangeConnection;
    entt::scoped_connection m_cellChangeConnection;
    entt::scoped_connection m_playerDialogueConnection;
    entt::scoped_connection m_playerLevelConnection;
    entt::scoped_connection m_partyJoinedConnection;
    entt::scoped_connection m_partyLeftConnection;
    entt::scoped_connection m_notifyPlayerPerksConnection;
    entt::scoped_connection m_playerJoinedConnection;
    entt::scoped_connection m_playerLeftConnection;
};
