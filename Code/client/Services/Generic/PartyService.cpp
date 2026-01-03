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

#include <OverlayApp.hpp>

#include <Forms/TESGlobal.h>

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

void PartyService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    const auto cCurrentTick = m_transport.GetClock().GetCurrentTick();
    if (m_nextUpdate > cCurrentTick)
        return;

    // Update once every second
    m_nextUpdate = cCurrentTick + 1000;

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

    auto pArguments = CefListValue::Create();
    pArguments->SetDictionary(0, pOptions);

    m_world.GetOverlayService().GetOverlayApp()->ExecuteAsync("partyOptions", pArguments);
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
}
