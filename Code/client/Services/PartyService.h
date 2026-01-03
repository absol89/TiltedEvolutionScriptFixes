#pragma once

#include <Messages/NotifyPlayerList.h>
#include <Structs/GameId.h>
#include <Structs/PartyOptions.h>
#include <Structs/Vector3_NetQuantize.h>

struct World;
struct ImguiService;
struct TransportService;
struct UpdateEvent;
struct DisconnectedEvent;
struct NotifyPartyInfo;
struct NotifyPartyInvite;
struct NotifyPartyJoined;
struct NotifyPartyLeft;
struct NotifyPlayerProfileImage;
struct NotifyPlayerActorName;
struct NotifyPartyOptions;
struct NotifyPartyLeaderCellLock;

/**
 * @brief Manages the party of the local player.
 */
struct PartyService
{
    PartyService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransportService) noexcept;
    ~PartyService() = default;

    TP_NOCOPYMOVE(PartyService);

    [[nodiscard]] bool IsInParty() const noexcept { return m_inParty; }
    [[nodiscard]] bool IsLeader() const noexcept { return m_isLeader; }
    [[nodiscard]] uint32_t GetLeaderPlayerId() const noexcept { return m_leaderPlayerId; }

    const Vector<uint32_t>& GetPartyMembers() const noexcept { return m_partyMembers; }
    const Map<uint32_t, NotifyPlayerList::PlayerListEntry>& GetPlayers() const noexcept { return m_players; }
    const String* GetActorName(uint32_t aPlayerId) const noexcept;
    const PartyOptions& GetPartyOptions() const noexcept { return m_partyOptions; }
    Map<uint32_t, uint64_t>& GetInvitations() noexcept { return m_invitations; }
    [[nodiscard]] bool IsCellLockActiveForLocal() const noexcept;
    [[nodiscard]] bool AllowCellChangeDuringLock() const noexcept;
    void NotifyCellLockBlocked() noexcept;

    void CreateParty() const noexcept;
    void LeaveParty() const noexcept;
    void CreateInvite(const uint32_t aPlayerId) const noexcept;
    void AcceptInvite(const uint32_t aInviterId) const noexcept;
    void KickPartyMember(const uint32_t aPlayerId) const noexcept;
    void ChangePartyLeader(const uint32_t aPlayerId) const noexcept;
    void UpdatePartyOptions(const PartyOptions& aOptions) noexcept;

protected:
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void OnPlayerList(const NotifyPlayerList& acPlayerList) noexcept;
    void OnPartyInfo(const NotifyPartyInfo& acPartyInfo) noexcept;
    void OnPartyInvite(const NotifyPartyInvite& acPartyInvite) noexcept;
    void OnPartyJoined(const NotifyPartyJoined& acPartyJoined) noexcept;
    void OnPartyLeft(const NotifyPartyLeft& acPartyLeft) noexcept;
    void OnPlayerProfileImage(const NotifyPlayerProfileImage& acMessage) noexcept;
    void OnPlayerActorName(const NotifyPlayerActorName& acMessage) noexcept;
    void OnPartyOptions(const NotifyPartyOptions& acMessage) noexcept;
    void OnPartyLeaderCellLock(const NotifyPartyLeaderCellLock& acMessage) noexcept;

private:
    void DestroyParty() noexcept;
    void UpdateCellLockCountdown(uint64_t aCurrentTick) noexcept;
    void ShowCellLockBanner(uint16_t aSecondsRemaining) const noexcept;
    void ClearCellLockBanner() const noexcept;
    void TeleportLocalPlayer(const GameId& acWorldSpaceId, const GameId& acCellId, const Vector3_NetQuantize& acPosition) const noexcept;

    Map<uint32_t, NotifyPlayerList::PlayerListEntry> m_players;
    Map<uint32_t, String> m_actorNames;
    Map<uint32_t, uint64_t> m_invitations;
    uint64_t m_nextUpdate{0};

    bool m_inParty = false;
    bool m_isLeader = false;
    uint32_t m_leaderPlayerId;
    Vector<uint32_t> m_partyMembers;
    PartyOptions m_partyOptions{};
    bool m_cellLockTeleportActive = false;
    uint64_t m_cellLockTeleportEndTick{0};
    uint64_t m_cellLockTeleportAllowUntil{0};
    uint16_t m_cellLockLastCountdown{0};
    uint64_t m_cellLockBlockedBannerUntil{0};
    GameId m_cellLockWorldSpaceId{};
    GameId m_cellLockCellId{};
    Vector3_NetQuantize m_cellLockPosition{};

    World& m_world;
    TransportService& m_transport;

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_disconnectConnection;
    entt::scoped_connection m_playerListConnection;
    entt::scoped_connection m_partyInfoConnection;
    entt::scoped_connection m_partyInviteConnection;
    entt::scoped_connection m_partyJoinedConnection;
    entt::scoped_connection m_partyLeftConnection;
    entt::scoped_connection m_playerAvatarConnection;
    entt::scoped_connection m_playerActorNameConnection;
    entt::scoped_connection m_partyOptionsConnection;
    entt::scoped_connection m_partyLeaderCellLockConnection;
};
