#include <Services/PartyService.h>

#include <Services/TransportService.h>

#include <Events/UpdateEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/PartyJoinedEvent.h>
#include <Events/PartyLeftEvent.h>

#include <Messages/NotifyPlayerList.h>
#include <Messages/NotifyPartyInfo.h>
#include <Messages/NotifyPartyInvite.h>
#include <Messages/PartyInviteRequest.h>
#include <Messages/PartyAcceptInviteRequest.h>
#include <Messages/PartyLeaveRequest.h>
#include <Messages/NotifyPartyJoined.h>
#include <Messages/NotifyPartyLeft.h>
#include <Messages/PartyCreateRequest.h>
#include <Messages/PartyChangeLeaderRequest.h>
#include <Messages/PartyKickRequest.h>
#include <Messages/PartyActorNamesRequest.h>
#include <Messages/PartyOptionsUpdateRequest.h>
#include <Messages/NotifyPlayerProfileImage.h>
#include <Messages/NotifyPlayerActorName.h>
#include <Messages/NotifyPartyOptions.h>
#include <Messages/NotifyPartyLeaderCellLock.h>

#include <OverlayApp.hpp>

#include <Forms/TESGlobal.h>
#include <Forms/TESObjectCELL.h>
#include <Forms/TESWorldSpace.h>
#include <PlayerCharacter.h>
#include <Structs/GridCellCoords.h>
#include <fmt/format.h>

PartyService::PartyService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransportService) noexcept
    : m_world(aWorld)
    , m_transport(aTransportService)
{
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&PartyService::OnUpdate>(this);
    m_disconnectConnection = aDispatcher.sink<DisconnectedEvent>().connect<&PartyService::OnDisconnected>(this);

    m_playerListConnection = aDispatcher.sink<NotifyPlayerList>().connect<&PartyService::OnPlayerList>(this);
    m_partyInfoConnection = aDispatcher.sink<NotifyPartyInfo>().connect<&PartyService::OnPartyInfo>(this);
    m_partyInviteConnection = aDispatcher.sink<NotifyPartyInvite>().connect<&PartyService::OnPartyInvite>(this);
    m_partyJoinedConnection = aDispatcher.sink<NotifyPartyJoined>().connect<&PartyService::OnPartyJoined>(this);
    m_partyLeftConnection = aDispatcher.sink<NotifyPartyLeft>().connect<&PartyService::OnPartyLeft>(this);
    m_playerAvatarConnection = aDispatcher.sink<NotifyPlayerProfileImage>().connect<&PartyService::OnPlayerProfileImage>(this);
    m_playerActorNameConnection = aDispatcher.sink<NotifyPlayerActorName>().connect<&PartyService::OnPlayerActorName>(this);
    m_partyOptionsConnection = aDispatcher.sink<NotifyPartyOptions>().connect<&PartyService::OnPartyOptions>(this);
    m_partyLeaderCellLockConnection = aDispatcher.sink<NotifyPartyLeaderCellLock>().connect<&PartyService::OnPartyLeaderCellLock>(this);
}

const String* PartyService::GetActorName(uint32_t aPlayerId) const noexcept
{
    const auto it = m_actorNames.find(aPlayerId);
    if (it == m_actorNames.end())
        return nullptr;

    return &it->second;
}

void PartyService::CreateParty() const noexcept
{
    PartyCreateRequest request;
    m_transport.Send(request);
}

void PartyService::LeaveParty() const noexcept
{
    PartyLeaveRequest request;
    m_transport.Send(request);
}

void PartyService::CreateInvite(const uint32_t aPlayerId) const noexcept
{
    PartyInviteRequest request;
    request.PlayerId = aPlayerId;
    m_transport.Send(request);
}

void PartyService::AcceptInvite(const uint32_t aInviterId) const noexcept
{
    if (!m_invitations.contains(aInviterId))
        return;

    PartyAcceptInviteRequest request;
    request.InviterId = aInviterId;
    m_transport.Send(request);
}

void PartyService::KickPartyMember(const uint32_t aPlayerId) const noexcept
{
    PartyKickRequest kickMessage;
    kickMessage.PartyMemberPlayerId = aPlayerId;
    m_transport.Send(kickMessage);
}

void PartyService::ChangePartyLeader(const uint32_t aPlayerId) const noexcept
{
    PartyChangeLeaderRequest changeMessage;
    changeMessage.PartyMemberPlayerId = aPlayerId;
    m_transport.Send(changeMessage);
}

void PartyService::UpdatePartyOptions(const PartyOptions& aOptions) noexcept
{
    if (!m_inParty || !m_isLeader)
        return;

    m_partyOptions = aOptions;

    PartyOptionsUpdateRequest request{};
    request.Options = aOptions;
    m_transport.Send(request);
}

bool PartyService::IsCellLockActiveForLocal() const noexcept
{
    return m_inParty && !m_isLeader && m_partyOptions.LockPartyToLeaderCell();
}

bool PartyService::AllowCellChangeDuringLock() const noexcept
{
    if (!IsCellLockActiveForLocal())
        return true;

    return m_transport.GetClock().GetCurrentTick() < m_cellLockTeleportAllowUntil;
}

void PartyService::NotifyCellLockBlocked() noexcept
{
    if (!IsCellLockActiveForLocal() || AllowCellChangeDuringLock() || m_cellLockTeleportActive)
        return;

    const auto currentTick = m_transport.GetClock().GetCurrentTick();
    if (currentTick < m_cellLockBlockedBannerUntil)
        return;

    m_cellLockBlockedBannerUntil = currentTick + 2000;

    if (auto* pOverlayApp = m_world.GetOverlayService().GetOverlayApp())
    {
        auto pArgs = CefListValue::Create();
        pArgs->SetString(0, "Party leader locked the party to their cell.");
        pArgs->SetInt(1, 2000);
        pOverlayApp->ExecuteAsync("showBanner", pArgs);
    }
}

void PartyService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    const auto cCurrentTick = m_transport.GetClock().GetCurrentTick();
    if (m_nextUpdate > cCurrentTick)
        return;

    // Update once every second
    m_nextUpdate = cCurrentTick + 1000;

    UpdateCellLockCountdown(cCurrentTick);

    auto itor = std::begin(m_invitations);
    while (itor != std::end(m_invitations))
    {
        if (itor->second < cCurrentTick)
            itor = m_invitations.erase(itor);
        else
            ++itor;
    }
}

void PartyService::OnDisconnected(const DisconnectedEvent& acEvent) noexcept
{
    DestroyParty();
    m_actorNames.clear();
}

void PartyService::OnPlayerList(const NotifyPlayerList& acPlayerList) noexcept
{
    m_players = acPlayerList.Players;

    for (auto it = m_actorNames.begin(); it != m_actorNames.end();)
    {
        if (m_players.find(it->first) == m_players.end())
            it = m_actorNames.erase(it);
        else
            ++it;
    }
}

void PartyService::OnPlayerProfileImage(const NotifyPlayerProfileImage& acMessage) noexcept
{
    auto& entry = m_players[acMessage.PlayerId];
    entry.Avatar = acMessage.Avatar;
}

void PartyService::OnPlayerActorName(const NotifyPlayerActorName& acMessage) noexcept
{
    if (acMessage.ActorName.empty())
        return;

    m_actorNames[acMessage.PlayerId] = acMessage.ActorName;
}

void PartyService::OnPartyOptions(const NotifyPartyOptions& acMessage) noexcept
{
    m_partyOptions = acMessage.Options;

    auto pOptions = CefDictionaryValue::Create();
    pOptions->SetBool("syncFastTravelMarkers", m_partyOptions.SyncFastTravelMarkers());
    pOptions->SetBool("showPartyMemberMarkers", m_partyOptions.ShowPartyMemberMarkers());
    pOptions->SetBool("syncDeadBodyLoot", m_partyOptions.SyncDeadBodyLoot());
    pOptions->SetBool("lockPartyToLeaderCell", m_partyOptions.LockPartyToLeaderCell());

    auto pArguments = CefListValue::Create();
    pArguments->SetDictionary(0, pOptions);

    m_world.GetOverlayService().GetOverlayApp()->ExecuteAsync("partyOptions", pArguments);
}

void PartyService::OnPartyLeaderCellLock(const NotifyPartyLeaderCellLock& acMessage) noexcept
{
    if (!m_inParty || m_isLeader)
        return;

    if (acMessage.Cancelled)
    {
        m_cellLockTeleportActive = false;
        m_cellLockTeleportAllowUntil = 0;
        m_cellLockLastCountdown = 0;
        ClearCellLockBanner();
        return;
    }

    if (!m_partyOptions.LockPartyToLeaderCell())
        return;

    m_cellLockWorldSpaceId = acMessage.WorldSpaceId;
    m_cellLockCellId = acMessage.CellId;
    m_cellLockPosition = acMessage.Position;
    m_cellLockTeleportEndTick = m_transport.GetClock().GetCurrentTick() + (static_cast<uint64_t>(acMessage.CountdownSeconds) * 1000);
    m_cellLockTeleportActive = true;
    m_cellLockTeleportAllowUntil = 0;
    m_cellLockLastCountdown = acMessage.CountdownSeconds;

    ShowCellLockBanner(acMessage.CountdownSeconds);
}

void PartyService::OnPartyInfo(const NotifyPartyInfo& acPartyInfo) noexcept
{
    if (m_inParty)
    {
        spdlog::debug("[PartyService]: Got party info update");
        m_isLeader = acPartyInfo.IsLeader;
        m_leaderPlayerId = acPartyInfo.LeaderPlayerId;
        m_partyMembers = acPartyInfo.PlayerIds;

        PartyActorNamesRequest actorNamesRequest{};
        m_transport.Send(actorNamesRequest);

        // TODO: this can be done a bit prettier
        if (m_isLeader)
        {
            TESGlobal* pWorldEncountersEnabled = Cast<TESGlobal>(TESForm::GetById(0xB8EC1));
            pWorldEncountersEnabled->f = 1.f;
        }

        auto pArguments = CefListValue::Create();

        auto pPlayerIds = CefListValue::Create();
        for (int i = 0; i < m_partyMembers.size(); i++)
            pPlayerIds->SetInt(i, m_partyMembers[i]);

        pArguments->SetList(0, pPlayerIds);
        pArguments->SetInt(1, acPartyInfo.LeaderPlayerId);

        m_world.GetOverlayService().GetOverlayApp()->ExecuteAsync("partyInfo", pArguments);
    }
}

void PartyService::OnPartyInvite(const NotifyPartyInvite& acPartyInvite) noexcept
{
    spdlog::debug("[PartyService]: Got party invite from {}", acPartyInvite.InviterId);

    m_invitations[acPartyInvite.InviterId] = acPartyInvite.ExpiryTick;

    auto pArguments = CefListValue::Create();
    pArguments->SetInt(0, acPartyInvite.InviterId);
    m_world.GetOverlayService().GetOverlayApp()->ExecuteAsync("partyInviteReceived", pArguments);
}

void PartyService::OnPartyJoined(const NotifyPartyJoined& acPartyJoined) noexcept
{
    spdlog::debug("[PartyService]: Joined party. LeaderId: {}, IsLeader: {}", acPartyJoined.LeaderPlayerId, acPartyJoined.IsLeader);

    m_inParty = true;
    m_isLeader = acPartyJoined.IsLeader;
    m_leaderPlayerId = acPartyJoined.LeaderPlayerId;
    m_partyMembers = acPartyJoined.PlayerIds;

    PartyActorNamesRequest actorNamesRequest{};
    m_transport.Send(actorNamesRequest);

    m_world.GetDispatcher().trigger(PartyJoinedEvent(m_isLeader));
}

void PartyService::OnPartyLeft(const NotifyPartyLeft& acPartyLeft) noexcept
{
    spdlog::debug("[PartyService]: Left party");

    DestroyParty();

    m_world.GetDispatcher().trigger(PartyLeftEvent());
}

void PartyService::DestroyParty() noexcept
{
    m_inParty = false;
    m_isLeader = false;
    m_leaderPlayerId = -1;
    m_partyMembers.clear();
    m_partyOptions = PartyOptions{};
    m_cellLockTeleportActive = false;
    m_cellLockTeleportAllowUntil = 0;
    m_cellLockLastCountdown = 0;
    m_cellLockBlockedBannerUntil = 0;
    ClearCellLockBanner();
}

void PartyService::UpdateCellLockCountdown(uint64_t aCurrentTick) noexcept
{
    if (!m_cellLockTeleportActive)
        return;

    if (aCurrentTick >= m_cellLockTeleportEndTick)
    {
        ShowCellLockBanner(0);
        TeleportLocalPlayer(m_cellLockWorldSpaceId, m_cellLockCellId, m_cellLockPosition);
        m_cellLockTeleportActive = false;
        m_cellLockTeleportAllowUntil = aCurrentTick + 5000;
        return;
    }

    const auto remainingMs = m_cellLockTeleportEndTick - aCurrentTick;
    const uint16_t secondsRemaining = static_cast<uint16_t>((remainingMs + 999) / 1000);
    if (secondsRemaining == m_cellLockLastCountdown)
        return;

    m_cellLockLastCountdown = secondsRemaining;
    ShowCellLockBanner(secondsRemaining);
}

void PartyService::ShowCellLockBanner(uint16_t aSecondsRemaining) const noexcept
{
    if (auto* pOverlayApp = m_world.GetOverlayService().GetOverlayApp())
    {
        const std::string message = aSecondsRemaining > 0
            ? fmt::format("Party leader changed cells.\nTeleporting in {}s", aSecondsRemaining)
            : "Teleporting to party leader...";

        auto pArgs = CefListValue::Create();
        pArgs->SetString(0, message);
        pArgs->SetInt(1, 1100);
        pOverlayApp->ExecuteAsync("showBanner", pArgs);
    }
}

void PartyService::ClearCellLockBanner() const noexcept
{
    if (auto* pOverlayApp = m_world.GetOverlayService().GetOverlayApp())
    {
        auto pArgs = CefListValue::Create();
        pArgs->SetString(0, "");
        pArgs->SetInt(1, 1);
        pOverlayApp->ExecuteAsync("showBanner", pArgs);
    }
}

void PartyService::TeleportLocalPlayer(const GameId& acWorldSpaceId, const GameId& acCellId, const Vector3_NetQuantize& acPosition) const noexcept
{
    auto& modSystem = m_world.GetModSystem();

    TESObjectCELL* pCell = nullptr;
    if (!acWorldSpaceId)
    {
        const uint32_t cellId = modSystem.GetGameId(acCellId);
        pCell = Cast<TESObjectCELL>(TESForm::GetById(cellId));
    }
    else
    {
        const uint32_t worldSpaceId = modSystem.GetGameId(acWorldSpaceId);
        TESWorldSpace* pWorldSpace = Cast<TESWorldSpace>(TESForm::GetById(worldSpaceId));
        if (pWorldSpace)
        {
            GridCellCoords coordinates = GridCellCoords::CalculateGridCellCoords(acPosition);
            pCell = pWorldSpace->LoadCell(coordinates.X, coordinates.Y);
        }
    }

    if (!pCell)
    {
        spdlog::error("Party cell lock teleport failed: destination cell not available.");
        return;
    }

    if (auto* pPlayer = PlayerCharacter::Get())
        pPlayer->MoveTo(pCell, acPosition);
}
