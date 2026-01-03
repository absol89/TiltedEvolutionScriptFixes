#pragma once

#include <Events/PacketEvent.h>
#include <Structs/PartyOptions.h>
#include <Structs/Vector3_NetQuantize.h>

struct World;
struct UpdateEvent;
struct PlayerJoinEvent;
struct PlayerLeaveEvent;
struct PartyInviteRequest;
struct PartyAcceptInviteRequest;
struct PartyLeaveRequest;
struct NotifyPartyInfo;
struct PartyCreateRequest;
struct PartyChangeLeaderRequest;
struct PartyKickRequest;
struct PartyPositionUpdateRequest;
struct PartyPositionsRequest;
struct PartyActorNamesRequest;
struct PartyOptionsUpdateRequest;

/**
 * @brief Manages every party in the server.
 */
struct PartyService
{
    struct Party
    {
        uint32_t LeaderPlayerId;
        Vector<Player*> Members;
        GameId CachedWeather{};
        PartyOptions Options{};
        struct LeaderCellSnapshot
        {
            GameId WorldSpaceId{};
            GameId CellId{};
            Vector3_NetQuantize Position{};
            bool HasLocation{false};
        } LeaderCell{};
        bool PendingCellLockNotify{false};
        uint64_t PendingCellLockNotifyAt{0};
        uint32_t PendingCellLockLeaderId{0};
    };

    PartyService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~PartyService() noexcept = default;

    TP_NOCOPYMOVE(PartyService);

    const Party* GetById(uint32_t aId) const noexcept;
    bool IsPlayerInParty(Player* const apPlayer) const noexcept;
    bool IsPlayerLeader(Player* const apPlayer) noexcept;
    Party* GetPlayerParty(Player* const apPlayer) noexcept;
    void UpdateLeaderCellSnapshot(Party& aParty, Player* apLeader) noexcept;
    void NotifyPartyLeaderCellLock(Party& aParty, Player* apLeader, bool aCancelled) noexcept;
    void ScheduleLeaderCellLockNotify(Party& aParty, Player* apLeader) noexcept;

protected:
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnPlayerJoin(const PlayerJoinEvent& acEvent) noexcept;
    void OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept;
    void OnPartyInvite(const PacketEvent<PartyInviteRequest>& acPacket) noexcept;
    void OnPartyAcceptInvite(const PacketEvent<PartyAcceptInviteRequest>& acPacket) noexcept;
    void OnPartyLeave(const PacketEvent<PartyLeaveRequest>& acPacket) noexcept;
    void OnPartyCreate(const PacketEvent<PartyCreateRequest>& acPacket) noexcept;
    void OnPartyChangeLeader(const PacketEvent<PartyChangeLeaderRequest>& acPacket) noexcept;
    void OnPartyKick(const PacketEvent<PartyKickRequest>& acPacket) noexcept;
    void OnPartyPositionUpdate(const PacketEvent<PartyPositionUpdateRequest>& acPacket) noexcept;
    void OnPartyPositionsRequest(const PacketEvent<PartyPositionsRequest>& acPacket) noexcept;
    void OnPartyActorNamesRequest(const PacketEvent<PartyActorNamesRequest>& acPacket) noexcept;
    void OnPartyOptionsUpdate(const PacketEvent<PartyOptionsUpdateRequest>& acPacket) noexcept;
    void RemovePlayerFromParty(Player* apPlayer) noexcept;

    void BroadcastPlayerList(Player* apPlayer = nullptr) const noexcept;
    void BroadcastPartyInfo(uint32_t aPartyId) const noexcept;

private:
    World& m_world;

    TiltedPhoques::Map<uint32_t, Party> m_parties;
    uint32_t m_nextId{0};
    uint64_t m_nextInvitationExpire{0};
    uint64_t m_nextPositionsBroadcast{0};

    entt::scoped_connection m_updateEvent;
    entt::scoped_connection m_playerJoinConnection;
    entt::scoped_connection m_playerLeaveConnection;
    entt::scoped_connection m_partyInviteConnection;
    entt::scoped_connection m_partyAcceptInviteConnection;
    entt::scoped_connection m_partyLeaveConnection;
    entt::scoped_connection m_partyCreateConnection;
    entt::scoped_connection m_partyChangeLeaderConnection;
    entt::scoped_connection m_partyKickConnection;
    entt::scoped_connection m_partyPositionUpdateConnection;
    entt::scoped_connection m_partyPositionsRequestConnection;
    entt::scoped_connection m_partyActorNamesRequestConnection;
    entt::scoped_connection m_partyOptionsUpdateConnection;

    void SendPartyJoinedEvent(Party& aParty, Player* aPlayer) noexcept;
};
