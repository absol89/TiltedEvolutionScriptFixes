#include <Services/PluginService.h>

#include <World.h>
#include <Components.h>
#include <Structs/Tints.h>
#include <Systems/FaceGenSystem.h>

#include <Messages/ModMessage.h>
#include <Messages/NotifyModMessage.h>
#include <Messages/SendModMessageRequest.h>
#include <Messages/AddonHelloRequest.h>
#include <Messages/NotifyAddonCapabilities.h>
#include <Messages/NotifyAddonNear.h>
#include <Messages/SendAddonNearRequest.h>

#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>

#include <cmath>

using namespace SkyrimTogetherAPI;

PluginService& PluginService::Get() noexcept
{
    // Never destroyed: a game job thread can still run the World update, and with it OnUpdate, while the process
    // exits, and plugins keep the interface pointer. Built with the default allocator, so its containers never take
    // memory from whichever allocator happens to be scoped on the thread that first asks (see StringCache::Get).
    static PluginService* s_pInstance = []
    {
        TiltedPhoques::ScopedAllocator _{TiltedPhoques::Allocator::GetDefault()};
        return new PluginService();
    }();
    return *s_pInstance;
}

ChannelHandle PluginService::RegisterChannel(const char* apName, MessageCallback aOnMessage, PartyChangedCallback aOnPartyChanged) noexcept
{
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

    m_channels.push_back({cName, aOnMessage, aOnPartyChanged});
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
    if (!entt::locator<World>::has_value() || !World::Get().GetTransport().IsOnline())
        return 0;

    return World::Get().GetTransport().GetLocalPlayerId();
}

bool PluginService::IsInParty() const noexcept
{
    return entt::locator<World>::has_value() && World::Get().GetPartyService().IsInParty();
}

SendResult PluginService::Queue(ChannelHandle aChannel, uint32_t aTargetPlayerId, const char* apBytes, size_t aLength) noexcept
{
    String channelName;
    {
        std::lock_guard cLock(m_lock);

        if (aChannel == 0 || aChannel > m_channels.size())
            return SendResult::BadChannel;

        channelName = m_channels[aChannel - 1].Name;
    }

    if (aLength > kMaxMessageSize)
        return SendResult::TooLarge;

    if (!entt::locator<World>::has_value() || !World::Get().GetTransport().IsOnline())
        return SendResult::NotConnected;

    const bool cToAll = aTargetPlayerId == ModMessage::kAllPlayers;
    if (cToAll && !m_nearSupported)
        return SendResult::NotSupported;

    if (!cToAll && !World::Get().GetPartyService().IsInParty())
        return SendResult::NotInParty;

    if (aTargetPlayerId != 0 && !cToAll)
    {
        std::lock_guard cLock(m_lock);

        if (!std::binary_search(m_members.begin(), m_members.end(), aTargetPlayerId))
            return SendResult::NotInParty;
    }

    // The state may have changed by the time the World update gets to it, in which case the message is dropped
    World::Get().GetRunner().Queue(
        [channelName = std::move(channelName), aTargetPlayerId, cToAll, data = String(apBytes ? apBytes : "", apBytes ? aLength : 0)]()
        {
            auto& world = World::Get();
            if (!world.GetTransport().IsOnline() || (!cToAll && !world.GetPartyService().IsInParty()))
                return;

            SendModMessageRequest request{};
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
            if (cChannel.Name == acMessage.Channel)
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

void PluginService::NotifyPartyChanges() noexcept
{
    const uint32_t cLocalId = GetLocalPlayerId();
    const auto& cParty = World::Get().GetPartyService();

    Vector<uint32_t> members;
    if (cParty.IsInParty())
    {
        for (const uint32_t cId : cParty.GetPartyMembers())
        {
            if (cId != cLocalId)
                members.push_back(cId);
        }
    }
    std::sort(members.begin(), members.end());

    Vector<uint32_t> joined, left;
    Vector<PartyChangedCallback> callbacks;
    {
        std::lock_guard cLock(m_lock);

        std::set_difference(members.begin(), members.end(), m_members.begin(), m_members.end(), std::back_inserter(joined));
        std::set_difference(m_members.begin(), m_members.end(), members.begin(), members.end(), std::back_inserter(left));
        m_members = members;

        for (const Channel& cChannel : m_channels)
        {
            if (cChannel.OnPartyChanged)
                callbacks.push_back(cChannel.OnPartyChanged);
        }
    }

    for (const PartyChangedCallback cCallback : callbacks)
    {
        for (const uint32_t cId : joined)
            cCallback(cId, true);
        for (const uint32_t cId : left)
            cCallback(cId, false);
    }
}

bool PluginService::SetNearMessageCallback(ChannelHandle aChannel, NearMessageCallback aOnNearMessage) noexcept
{
    std::lock_guard cLock(m_lock);

    if (aChannel == 0 || aChannel > m_channels.size())
        return false;

    m_channels[aChannel - 1].OnNearMessage = aOnNearMessage;
    if (aOnNearMessage)
        m_anyNearChannel = true;
    return true;
}

SendResult PluginService::SendNear(ChannelHandle aChannel, uint32_t aServerId, Delivery aDelivery, const char* apBytes, size_t aLength) noexcept
{
    {
        std::lock_guard cLock(m_lock);

        if (aChannel == 0 || aChannel > m_channels.size())
            return SendResult::BadChannel;
    }

    if (aLength > kMaxNearMessageSize)
        return SendResult::TooLarge;

    if (!entt::locator<World>::has_value() || !World::Get().GetTransport().IsOnline())
        return SendResult::NotConnected;

    if (!m_nearSupported)
        return SendResult::NotSupported;

    // The server checks this too; checking here saves sending what it would drop
    if (!IsOwnedLocally(aServerId))
        return SendResult::NotOwner;

    // Waits in the channel's queue until its budget allows it (SendQueuedNear, during the World update). The copy is
    // made with the default allocator: the caller may be on a thread with a scoped one that does not outlive the frame.
    TiltedPhoques::ScopedAllocator _{TiltedPhoques::Allocator::GetDefault()};
    String data(apBytes ? apBytes : "", apBytes ? aLength : 0);

    std::lock_guard cLock(m_nearLock);
    if (m_nearQueues.size() < aChannel)
        m_nearQueues.resize(aChannel, NearQueue{{}, {}, {}, kNearBurst});
    NearQueue& queue = m_nearQueues[aChannel - 1];

    if (aDelivery == Delivery::Unreliable)
    {
        // Replaces the actor's waiting message, keeping its turn
        auto [itor, cInserted] = queue.Unreliable.try_emplace(aServerId);
        itor->second = std::move(data);
        if (cInserted)
            queue.UnreliableOrder.push_back(aServerId);
    }
    else
    {
        if (queue.Reliable.size() >= kMaxQueuedNearMessages)
            return SendResult::QueueFull;
        queue.Reliable.push_back({aServerId, std::move(data)});
    }

    return SendResult::Ok;
}

size_t PluginService::GetNearQueued(ChannelHandle aChannel) const noexcept
{
    std::lock_guard cLock(m_nearLock);

    if (aChannel == 0 || aChannel > m_nearQueues.size())
        return 0;

    const NearQueue& cQueue = m_nearQueues[aChannel - 1];
    return cQueue.Reliable.size() + cQueue.Unreliable.size();
}

void PluginService::SendQueuedNear(double aDelta) noexcept
{
    auto& transport = World::Get().GetTransport();

    std::lock_guard cLock(m_nearLock);
    for (size_t i = 0; i < m_nearQueues.size(); ++i)
    {
        NearQueue& queue = m_nearQueues[i];
        queue.Tokens = std::min(kNearBurst, queue.Tokens + aDelta * kNearMessagesPerSecond);
        if (queue.Reliable.empty() && queue.UnreliableOrder.empty())
            continue;

        String channelName;
        {
            std::lock_guard cChannelLock(m_lock);
            channelName = m_channels[i].Name;
        }

        // Reliable first: an unreliable message lost to the budget is replaced by the next one anyway. The data is
        // copied into the request, not moved: the request's string may use another allocator, and allocators compare
        // equal, so a move would hand it a buffer it then frees with the wrong one.
        while (queue.Tokens >= 1.0 && !queue.Reliable.empty())
        {
            SendAddonNearRequest request{};
            request.Channel = channelName;
            request.ServerId = queue.Reliable.front().ServerId;
            request.Unreliable = false;
            request.Data = queue.Reliable.front().Data;
            queue.Reliable.pop_front();
            transport.Send(request, TiltedPhoques::kReliable);
            queue.Tokens -= 1.0;
        }

        while (queue.Tokens >= 1.0 && !queue.UnreliableOrder.empty())
        {
            const uint32_t cServerId = queue.UnreliableOrder.front();
            queue.UnreliableOrder.pop_front();
            const auto cItor = queue.Unreliable.find(cServerId);
            if (cItor == queue.Unreliable.end())
                continue;

            SendAddonNearRequest request{};
            request.Channel = channelName;
            request.ServerId = cServerId;
            request.Unreliable = true;
            request.Data = cItor->second;
            queue.Unreliable.erase(cItor);
            transport.Send(request, TiltedPhoques::kUnreliableNoNagle);
            queue.Tokens -= 1.0;
        }
    }
}

void PluginService::ClearQueuedNear() noexcept
{
    std::lock_guard cLock(m_nearLock);

    // A new connection starts with the server's bucket full, so ours starts full too
    for (NearQueue& queue : m_nearQueues)
        queue = NearQueue{{}, {}, {}, kNearBurst};
}

SendResult PluginService::SendToAll(ChannelHandle aChannel, const char* apBytes, size_t aLength) noexcept
{
    return Queue(aChannel, ModMessage::kAllPlayers, apBytes, aLength);
}

bool PluginService::SetFaceTints(uint32_t aServerId, const FaceTint* apTints, size_t aCount) noexcept
{
    if (aServerId == 0 || !apTints || aCount == 0 || aCount > kMaxFaceTints)
        return false;

    if (!entt::locator<World>::has_value() || !World::Get().GetTransport().IsOnline())
        return false;

    // Copied on the calling thread: the plugin's array is only valid for the call. With the default allocator, as the
    // copy is freed during a later World update.
    TiltedPhoques::ScopedAllocator _{TiltedPhoques::Allocator::GetDefault()};
    struct Copied
    {
        String Texture;
        float Alpha;
        uint32_t Color;
        uint32_t Type;
    };
    Vector<Copied> tints;
    tints.reserve(aCount);
    for (size_t i = 0; i < aCount; ++i)
    {
        const FaceTint& cTint = apTints[i];
        if (!std::isfinite(cTint.Alpha))
            return false;

        // Reading up to one byte past the limit is enough to tell the path is too long
        const size_t cLength = cTint.Texture ? strnlen(cTint.Texture, kMaxFaceTintTextureSize + 1) : 0;
        if (cLength > kMaxFaceTintTextureSize)
            return false;

        tints.push_back({String(cTint.Texture ? cTint.Texture : "", cLength), cTint.Alpha, cTint.Color, cTint.Type});
    }

    // FaceGenSystem paints a copy whose FaceGenComponent is not generated yet, on the next update with its head loaded
    World::Get().GetRunner().Queue(
        [aServerId, tints = std::move(tints)]()
        {
            auto& world = World::Get();
            const auto cEntity = Utils::FindEntityByServerId(aServerId);
            if (!cEntity || !world.try_get<RemoteComponent>(*cEntity) || !world.try_get<FormIdComponent>(*cEntity))
                return;

            Tints faceTints;
            faceTints.Entries.reserve(tints.size());
            for (const Copied& cTint : tints)
            {
                Tints::Entry entry{};
                entry.Name = cTint.Texture;
                entry.Alpha = cTint.Alpha;
                entry.Color = cTint.Color;
                entry.Type = cTint.Type;
                faceTints.Entries.push_back(std::move(entry));
            }
            FaceGenSystem::Setup(world, *cEntity, faceTints);
        });

    return true;
}

bool PluginService::IsNearSupported() const noexcept
{
    return m_nearSupported;
}

uint32_t PluginService::GetServerId(uint32_t aFormId) const noexcept
{
    std::lock_guard cLock(m_idLock);

    const auto cItor = m_serverIdsByFormId.find(aFormId);
    return cItor != m_serverIdsByFormId.end() ? cItor->second : 0;
}

uint32_t PluginService::GetFormId(uint32_t aServerId) const noexcept
{
    std::lock_guard cLock(m_idLock);

    const auto cItor = m_actorsByServerId.find(aServerId);
    return cItor != m_actorsByServerId.end() ? cItor->second.FormId : 0;
}

bool PluginService::IsOwnedLocally(uint32_t aServerId) const noexcept
{
    std::lock_guard cLock(m_idLock);

    const auto cItor = m_actorsByServerId.find(aServerId);
    return cItor != m_actorsByServerId.end() && cItor->second.IsLocal;
}

void PluginService::OnNotifyAddonNear(const NotifyAddonNear& acMessage) noexcept
{
    NearMessageCallback onNearMessage = nullptr;
    {
        std::lock_guard cLock(m_lock);

        for (const Channel& cChannel : m_channels)
        {
            if (cChannel.Name == acMessage.Channel)
            {
                onNearMessage = cChannel.OnNearMessage;
                break;
            }
        }
    }

    // No plugin takes messages about actors on that channel on this client: nothing to do
    if (onNearMessage)
        onNearMessage(acMessage.SenderPlayerId, acMessage.ServerId, acMessage.Data.data(), acMessage.Data.size());
}

void PluginService::OnNotifyAddonCapabilities(const NotifyAddonCapabilities& acMessage) noexcept
{
    m_nearSupported = acMessage.ApiVersion >= static_cast<uint32_t>(InterfaceVersion::V2) && acMessage.MaxNearMessageSize >= kMaxNearMessageSize;
    spdlog::info("Plugin API: the server relays messages about actors: {}", m_nearSupported.load() ? "yes" : "no");
}

void PluginService::OnConnected(const ConnectedEvent&) noexcept
{
    m_nearSupported = false;
    ClearQueuedNear();

    // A server without Plugin API V2 cannot parse this and drops it, so SendNear stays off there
    AddonHelloRequest request{};
    request.ApiVersion = static_cast<uint32_t>(InterfaceVersion::V2);
    World::Get().GetTransport().Send(request);
}

void PluginService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    m_nearSupported = false;
    ClearQueuedNear();

    std::lock_guard cLock(m_idLock);
    m_actorsByServerId.clear();
    m_serverIdsByFormId.clear();
}

void PluginService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    if (!m_anyNearChannel || !World::Get().GetTransport().IsOnline())
        return;

    if (m_nearSupported)
        SendQueuedNear(acEvent.Delta);

    auto& world = World::Get();
    const auto localView = world.view<LocalComponent, FormIdComponent>();
    const auto remoteView = world.view<RemoteComponent, FormIdComponent>();

    // Refilled in place: clearing keeps the buckets, so a frame normally allocates nothing, and the maps never take
    // over memory from containers built with another allocator.
    std::lock_guard cLock(m_idLock);
    m_actorsByServerId.clear();
    m_serverIdsByFormId.clear();

    for (const auto cEntity : localView)
    {
        const uint32_t cServerId = localView.get<LocalComponent>(cEntity).Id;
        const uint32_t cFormId = localView.get<FormIdComponent>(cEntity).Id;
        m_actorsByServerId[cServerId] = {cFormId, true};
        m_serverIdsByFormId[cFormId] = cServerId;
    }

    for (const auto cEntity : remoteView)
    {
        const uint32_t cServerId = remoteView.get<RemoteComponent>(cEntity).Id;
        const uint32_t cFormId = remoteView.get<FormIdComponent>(cEntity).Id;
        m_actorsByServerId[cServerId] = {cFormId, false};
        m_serverIdsByFormId[cFormId] = cServerId;
    }
}
