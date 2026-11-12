#include <Services/PluginService.h>

#include <World.h>
#include <Components.h>

#include <Messages/ModMessage.h>
#include <Messages/NotifyModMessage.h>
#include <Messages/SendModMessageRequest.h>

using namespace SkyrimTogetherAPI;

PluginService& PluginService::Get() noexcept
{
    static PluginService s_instance;
    return s_instance;
}

ChannelHandle PluginService::RegisterChannel(const char* apName, MessageCallback aOnMessage, PartyChangedCallback aOnPartyChanged) noexcept
{
    return RegisterScopedChannel(apName, ChannelScope::Party, aOnMessage, aOnPartyChanged);
}

ChannelHandle PluginService::RegisterScopedChannel(const char* apName, ChannelScope aScope, MessageCallback aOnMessage, PartyChangedCallback aOnPartyChanged) noexcept
{
    if (aScope != ChannelScope::Party && aScope != ChannelScope::Nearby)
        return 0;

    // Reading up to one byte past the limit is enough to tell the name is too long
    const String cName = apName ? String(apName, strnlen(apName, kMaxChannelNameSize + 1)) : String();
    if (!ModMessage::IsValidChannelName(cName))
        return 0;

    std::lock_guard cLock(m_lock);

    for (const Channel& cChannel : m_channels)
    {
        if (cChannel.Name == cName)
            return 0;
    }

    m_channels.push_back({cName, aScope, {}, aOnMessage, aOnPartyChanged});
    return static_cast<ChannelHandle>(m_channels.size());
}

SendResult PluginService::Send(ChannelHandle aChannel, const char* apBytes, size_t aLength) noexcept
{
    return Queue(aChannel, 0, apBytes, aLength);
}

SendResult PluginService::SendTo(ChannelHandle aChannel, uint32_t aPlayerId, const char* apBytes, size_t aLength) noexcept
{
    // 0 means the whole party, so it is not a valid target
    if (aPlayerId == 0)
        return SendResult::NotInParty;

    return Queue(aChannel, aPlayerId, apBytes, aLength);
}

uint32_t PluginService::GetLocalPlayerId() const noexcept
{
    std::lock_guard cLock(m_lock);
    return m_localPlayerId;
}

bool PluginService::IsInParty() const noexcept
{
    std::lock_guard cLock(m_lock);
    return m_inParty;
}

uint32_t PluginService::GetPartyLeaderId() const noexcept
{
    std::lock_guard cLock(m_lock);
    return m_partyLeaderId;
}

SendResult PluginService::Queue(ChannelHandle aChannel, uint32_t aTargetPlayerId, const char* apBytes, size_t aLength) noexcept
{
    if (aLength > kMaxMessageSize || (aLength != 0 && !apBytes))
        return SendResult::TooLarge;

    String channelName;
    ChannelScope scope;
    {
        std::lock_guard cLock(m_lock);
        if (aChannel == 0 || aChannel > m_channels.size())
            return SendResult::BadChannel;
        if (!m_localPlayerId)
            return SendResult::NotConnected;
        const auto& channel = m_channels[aChannel - 1];
        channelName = channel.Name;
        scope = channel.Scope;
        if (scope == ChannelScope::Party && !m_inParty)
            return SendResult::NotInParty;
        const auto& peers = scope == ChannelScope::Party ? m_members : m_nearby;
        if (aTargetPlayerId != 0 && !std::binary_search(peers.begin(), peers.end(), aTargetPlayerId))
            return SendResult::NotInParty;
    }

    // The state may have changed by the time the World update gets to it, in which case the message is dropped
    World::Get().GetRunner().Queue(
        [channelName = std::move(channelName), scope, aTargetPlayerId, data = String(apBytes ? apBytes : "", apBytes ? aLength : 0)]()
        {
            auto& world = World::Get();
            if (!world.GetTransport().IsOnline() || (scope == ChannelScope::Party && !world.GetPartyService().IsInParty()))
                return;

            SendModMessageRequest request{};
            request.Scope = scope;
            request.Channel = channelName;
            request.TargetPlayerId = aTargetPlayerId;
            request.Data = data;
            world.GetTransport().Send(request);
        });

    return SendResult::Ok;
}

void PluginService::OnNotifyModMessage(const NotifyModMessage& acMessage) noexcept
{
    MessageCallback onMessage = nullptr;
    {
        std::lock_guard cLock(m_lock);

        for (const Channel& cChannel : m_channels)
        {
            if (cChannel.Name == acMessage.Channel && cChannel.Scope == acMessage.Scope)
            {
                onMessage = cChannel.OnMessage;
                break;
            }
        }
    }

    // No plugin registered that channel on this client: nothing to do
    if (onMessage)
        onMessage(acMessage.SenderPlayerId, acMessage.Data.data(), acMessage.Data.size());
}

void PluginService::Update() noexcept
{
    auto& world = World::Get();
    const bool cOnline = world.GetTransport().IsOnline();
    const uint32_t cLocalId = cOnline ? world.GetTransport().GetLocalPlayerId() : 0;
    const auto& party = world.GetPartyService();
    const bool cInParty = cOnline && party.IsInParty();
    const uint32_t cLeaderId = cInParty ? party.GetLeaderPlayerId() : 0;
    Vector<uint32_t> members, nearby;
    if (cInParty)
        for (uint32_t id : party.GetPartyMembers())
            if (id != cLocalId)
                members.push_back(id);
    if (cOnline)
        for (auto entity : world.view<FormIdComponent, PlayerComponent, RemoteComponent>())
        {
            const uint32_t id = world.get<PlayerComponent>(entity).Id;
            if (id != cLocalId)
                nearby.push_back(id);
        }
    for (auto* pPeers : {&members, &nearby})
    {
        std::sort(pPeers->begin(), pPeers->end());
        pPeers->erase(std::unique(pPeers->begin(), pPeers->end()), pPeers->end());
    }

    struct Change { PartyChangedCallback Callback; uint32_t Id; bool Joined; };
    Vector<Change> changes;
    {
        std::lock_guard cLock(m_lock);
        m_localPlayerId = cLocalId;
        m_inParty = cInParty;
        m_partyLeaderId = cLeaderId;
        m_members = members;
        m_nearby = nearby;
        for (Channel& channel : m_channels)
        {
            const auto& peers = channel.Scope == ChannelScope::Party ? members : nearby;
            if (channel.OnPartyChanged)
            {
                for (uint32_t id : channel.Peers)
                    if (!std::binary_search(peers.begin(), peers.end(), id))
                        changes.push_back({channel.OnPartyChanged, id, false});
                for (uint32_t id : peers)
                    if (!std::binary_search(channel.Peers.begin(), channel.Peers.end(), id))
                        changes.push_back({channel.OnPartyChanged, id, true});
            }
            channel.Peers = peers;
        }
    }
    // Callbacks can register channels or send, so never invoke them while holding the lock.
    for (const Change& change : changes)
        change.Callback(change.Id, change.Joined);
}
