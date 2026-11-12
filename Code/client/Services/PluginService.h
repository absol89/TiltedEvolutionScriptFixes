#pragma once

#include <SkyrimTogetherAPI.h>

struct NotifyModMessage;

/**
 * @brief The Plugin API that SKSE plugins use to sync their own state through Skyrim Together.
 *
 * Not a World service, as plugins may ask for the interface before the World exists. The World forwards
 * NotifyModMessage to it.
 */
struct PluginService final : SkyrimTogetherAPI::IVSkyrimTogether1
{
    [[nodiscard]] static PluginService& Get() noexcept;

    TP_NOCOPYMOVE(PluginService);

    SkyrimTogetherAPI::ChannelHandle
    RegisterChannel(const char* apName, SkyrimTogetherAPI::MessageCallback aOnMessage, SkyrimTogetherAPI::PartyChangedCallback aOnPartyChanged) noexcept override;
    SkyrimTogetherAPI::ChannelHandle RegisterScopedChannel(const char* apName, SkyrimTogetherAPI::ChannelScope aScope,
        SkyrimTogetherAPI::MessageCallback aOnMessage, SkyrimTogetherAPI::PartyChangedCallback aOnPeersChanged) noexcept;
    void Update() noexcept;

    SkyrimTogetherAPI::SendResult Send(SkyrimTogetherAPI::ChannelHandle aChannel, const char* apBytes, size_t aLength) noexcept override;
    SkyrimTogetherAPI::SendResult SendTo(SkyrimTogetherAPI::ChannelHandle aChannel, uint32_t aPlayerId, const char* apBytes, size_t aLength) noexcept override;
    uint32_t GetLocalPlayerId() const noexcept override;
    bool IsInParty() const noexcept override;
    // Not part of the V1 vtable, exported as SkyrimTogetherGetPartyLeaderId. 0 while not in a party.
    [[nodiscard]] uint32_t GetPartyLeaderId() const noexcept;

    /**
     * @brief Hands a relayed Mod Message to the plugin on its channel, if any. Runs during the World update.
     */
    void OnNotifyModMessage(const NotifyModMessage& acMessage) noexcept;

private:
    PluginService() = default;

    struct Channel
    {
        String Name;
        SkyrimTogetherAPI::ChannelScope Scope;
        Vector<uint32_t> Peers;
        SkyrimTogetherAPI::MessageCallback OnMessage;
        SkyrimTogetherAPI::PartyChangedCallback OnPartyChanged;
    };

    // Validates on the calling thread, then sends during the World update. aTargetPlayerId 0 means the registered audience.
    SkyrimTogetherAPI::SendResult Queue(SkyrimTogetherAPI::ChannelHandle aChannel, uint32_t aTargetPlayerId, const char* apBytes, size_t aLength) noexcept;

    mutable std::mutex m_lock;
    // A handle is the index in here plus one
    Vector<Channel> m_channels;
    // The other party members as of the last change, sorted. Lets any thread check a SendTo target.
    Vector<uint32_t> m_members;
    Vector<uint32_t> m_nearby;
    uint32_t m_localPlayerId{0};
    bool m_inParty{false};
    uint32_t m_partyLeaderId{0};
};
