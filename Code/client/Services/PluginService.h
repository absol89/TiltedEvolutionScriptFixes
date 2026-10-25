#pragma once

#include <SkyrimTogetherAPI.h>

#include <deque>
#include <unordered_map>

struct NotifyModMessage;
struct NotifyAddonCapabilities;
struct NotifyAddonNear;
struct ConnectedEvent;
struct DisconnectedEvent;
struct UpdateEvent;

/**
 * @brief The Plugin API that SKSE plugins use to sync their own state through Skyrim Together.
 *
 * Not a World service, as plugins may ask for the interface before the World exists. The World forwards
 * NotifyModMessage, NotifyAddonNear and the connection events to it.
 */
struct PluginService final : SkyrimTogetherAPI::IVSkyrimTogether3
{
    [[nodiscard]] static PluginService& Get() noexcept;

    TP_NOCOPYMOVE(PluginService);

    SkyrimTogetherAPI::ChannelHandle
    RegisterChannel(const char* apName, SkyrimTogetherAPI::MessageCallback aOnMessage, SkyrimTogetherAPI::PartyChangedCallback aOnPartyChanged) noexcept override;
    SkyrimTogetherAPI::SendResult Send(SkyrimTogetherAPI::ChannelHandle aChannel, const char* apBytes, size_t aLength) noexcept override;
    SkyrimTogetherAPI::SendResult SendTo(SkyrimTogetherAPI::ChannelHandle aChannel, uint32_t aPlayerId, const char* apBytes, size_t aLength) noexcept override;
    uint32_t GetLocalPlayerId() const noexcept override;
    bool IsInParty() const noexcept override;

    bool SetNearMessageCallback(SkyrimTogetherAPI::ChannelHandle aChannel, SkyrimTogetherAPI::NearMessageCallback aOnNearMessage) noexcept override;
    SkyrimTogetherAPI::SendResult
    SendNear(SkyrimTogetherAPI::ChannelHandle aChannel, uint32_t aServerId, SkyrimTogetherAPI::Delivery aDelivery, const char* apBytes, size_t aLength) noexcept override;
    SkyrimTogetherAPI::SendResult SendToAll(SkyrimTogetherAPI::ChannelHandle aChannel, const char* apBytes, size_t aLength) noexcept override;
    bool IsNearSupported() const noexcept override;
    uint32_t GetServerId(uint32_t aFormId) const noexcept override;
    uint32_t GetFormId(uint32_t aServerId) const noexcept override;
    bool IsOwnedLocally(uint32_t aServerId) const noexcept override;

    bool SetFaceTints(uint32_t aServerId, const SkyrimTogetherAPI::FaceTint* apTints, size_t aCount) noexcept override;
    size_t GetNearQueued(SkyrimTogetherAPI::ChannelHandle aChannel) const noexcept override;
    uint64_t GetNetworkTick() const noexcept override;
    bool GetPlaybackTick(uint32_t aServerId, double* apTick) const noexcept override;
    size_t GetPlayers(SkyrimTogetherAPI::PlayerInfo* apPlayers, size_t aMax) const noexcept override;
    bool GetServerAddress(char* apHost, size_t aSize, uint16_t* apPort) const noexcept override;

    /**
     * @brief The "host:port" this client is about to connect to, for GetServerAddress.
     */
    void SetServerEndpoint(const std::string& acEndpoint) noexcept;

    /**
     * @brief Hands a relayed Mod Message to the plugin on its channel, if any. Runs during the World update.
     */
    void OnNotifyModMessage(const NotifyModMessage& acMessage) noexcept;

    /**
     * @brief Hands a relayed message about an actor to the plugin on its channel, if any. Runs during the World update.
     */
    void OnNotifyAddonNear(const NotifyAddonNear& acMessage) noexcept;

    /**
     * @brief The server's answer to the hello sent on connecting: whether it relays SendNear.
     */
    void OnNotifyAddonCapabilities(const NotifyAddonCapabilities& acMessage) noexcept;

    /**
     * @brief Asks the server which Plugin API features it relays.
     */
    void OnConnected(const ConnectedEvent& acEvent) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;

    /**
     * @brief Sends the messages about actors that the channels' budgets allow, and refreshes the actor IDs that any
     * thread can look up, while a plugin uses messages about actors.
     */
    void OnUpdate(const UpdateEvent& acEvent) noexcept;

    /**
     * @brief Any event after which PartyService's member list may have changed. Runs during the World update.
     */
    template <class T> void OnPartyEvent(const T&) noexcept { NotifyPartyChanges(); }

private:
    PluginService() = default;

    // Compares the party members with the last ones seen and tells every channel about the difference
    void NotifyPartyChanges() noexcept;

    struct Channel
    {
        String Name;
        SkyrimTogetherAPI::MessageCallback OnMessage;
        SkyrimTogetherAPI::PartyChangedCallback OnPartyChanged;
        SkyrimTogetherAPI::NearMessageCallback OnNearMessage{nullptr};
    };

    struct ActorIds
    {
        uint32_t FormId;
        bool IsLocal;
    };

    // Validates on the calling thread, then sends during the World update. aTargetPlayerId 0 means the whole party,
    // ModMessage::kAllPlayers every player on the server.
    SkyrimTogetherAPI::SendResult Queue(SkyrimTogetherAPI::ChannelHandle aChannel, uint32_t aTargetPlayerId, const char* apBytes, size_t aLength) noexcept;

    mutable std::mutex m_lock;
    // A handle is the index in here plus one
    Vector<Channel> m_channels;
    // The other party members as of the last change, sorted. Lets any thread check a SendTo target.
    Vector<uint32_t> m_members;
    // Whether any channel takes messages about actors, so the IDs below are worth keeping up to date
    std::atomic<bool> m_anyNearChannel{false};
    std::atomic<bool> m_nearSupported{false};

    // Messages about actors waiting for their channel's budget. The server relays a limited number per player and
    // channel and drops the rest without telling anyone, so they leave at a pace it accepts.
    struct NearQueue
    {
        struct Message
        {
            uint32_t ServerId;
            String Data;
        };
        // In order, none dropped
        std::deque<Message> Reliable;
        // Only the newest per actor: an unreliable message replaces the one before it. Actors take turns.
        std::unordered_map<uint32_t, String> Unreliable;
        std::deque<uint32_t> UnreliableOrder;
        double Tokens;
    };

    // Sends what the budgets allow; World update only
    void SendQueuedNear(double aDelta) noexcept;
    void ClearQueuedNear() noexcept;

    // Refreshes what any thread can read about the session as of this frame (network tick, playback ticks, players)
    void UpdateSession() noexcept;

    mutable std::mutex m_idLock;
    // The synced actors as of the last frame, both ways
    Map<uint32_t, ActorIds> m_actorsByServerId;
    Map<uint32_t, uint32_t> m_serverIdsByFormId;
    // Per copy with movement playing: its playback tick minus the network tick, as of the last frame
    Map<uint32_t, double> m_playbackOffsets;
    // The other players with their characters loaded, as of the last frame
    Vector<SkyrimTogetherAPI::PlayerInfo> m_players;

    // The network tick at the last frame, and when that was (steady clock), so it can be advanced between frames
    std::atomic<uint64_t> m_networkTick{0};
    std::atomic<int64_t> m_networkTickAt{0};

    // The server last connected to, under m_lock
    String m_serverHost;
    uint16_t m_serverPort{0};

    mutable std::mutex m_nearLock;
    // By channel handle minus one, grown on first use
    std::vector<NearQueue> m_nearQueues;
};
