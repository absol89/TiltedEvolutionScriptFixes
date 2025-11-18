#include <GameServer.h>

#include <Services/OverlayService.h>

#include <ChatMessageTypes.h>

#include <Messages/NotifyChatMessageBroadcast.h>
#include <Messages/SendChatMessageRequest.h>
#include <Messages/PlayerDialogueRequest.h>
#include <Messages/NotifyPlayerDialogue.h>
#include <Messages/TeleportRequest.h>
#include <Messages/NotifyTeleport.h>
#include <Messages/NotifyTeleportRequest.h>
#include <Messages/TeleportResponse.h>
#include <fmt/format.h>
#include <algorithm>
#include <cctype>


#include <Messages/RequestPlayerHealthUpdate.h>
#include <Messages/NotifyPlayerHealthUpdate.h>

#include "Game/Player.h"
#include <Events/PlayerLeaveEvent.h>
#include <Components/MovementComponent.h>

#include <regex>
#include <string_view>


namespace
{
bool IsVoteTimeCommand(const TiltedPhoques::String& msg) noexcept
{
    TiltedPhoques::String text = msg;
    // trim spaces
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.erase(text.begin());
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.pop_back();
    // to lower
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (text.rfind("/votetime", 0) == 0)
        return true;
    if (text == "/yes" || text == "/no")
        return true;
    return false;
}

void SendSystemMessage(Player* apPlayer, std::string_view aMessage)
{
    if (!apPlayer)
        return;

    NotifyChatMessageBroadcast notify{};
    notify.MessageType = ChatMessageType::kSystemMessage;
    notify.PlayerName = "Server";
    notify.ChatMessage = aMessage.data();
    apPlayer->Send(notify);
}

bool PopulateTeleportDestination(World& aWorld, Player* apTarget, NotifyTeleport& aOutMessage) noexcept
{
    if (!apTarget)
        return false;

    const auto character = apTarget->GetCharacter();
    if (!character.has_value())
        return false;

    const auto* pMovementComponent = aWorld.try_get<MovementComponent>(*character);
    if (!pMovementComponent)
        return false;

    const auto& cellComponent = apTarget->GetCellComponent();
    aOutMessage.CellId = cellComponent.Cell;
    aOutMessage.Position = pMovementComponent->Position;
    aOutMessage.WorldSpaceId = cellComponent.WorldSpaceId;
    return true;
}
} // namespace


OverlayService::OverlayService(World& aWorld, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
{
    m_chatMessageConnection = aDispatcher.sink<PacketEvent<SendChatMessageRequest>>().connect<&OverlayService::HandleChatMessage>(this);
    m_playerDialogueConnection = aDispatcher.sink<PacketEvent<PlayerDialogueRequest>>().connect<&OverlayService::OnPlayerDialogue>(this);
    m_teleportConnection = aDispatcher.sink<PacketEvent<TeleportRequest>>().connect<&OverlayService::OnTeleport>(this);
    m_teleportResponseConnection = aDispatcher.sink<PacketEvent<TeleportResponse>>().connect<&OverlayService::OnTeleportResponse>(this);
    m_playerHealthConnection = aDispatcher.sink<PacketEvent<RequestPlayerHealthUpdate>>().connect<&OverlayService::OnPlayerHealthUpdate>(this);
    m_playerLeaveConnection = aDispatcher.sink<PlayerLeaveEvent>().connect<&OverlayService::OnPlayerLeave>(this);
}

void sendPlayerMessage(const ChatMessageType acType, const String acContent, Player* aSendingPlayer) noexcept
{
    NotifyChatMessageBroadcast notifyMessage{};

    std::regex escapeHtml{"<[^>]+>\\s+(?=<)|<[^>]+>"};
    notifyMessage.MessageType = acType;
    notifyMessage.PlayerName = std::regex_replace(aSendingPlayer->GetUsername(), escapeHtml, "");
    notifyMessage.ChatMessage = std::regex_replace(acContent, escapeHtml, "");

    auto character = aSendingPlayer->GetCharacter();

    switch (notifyMessage.MessageType)
    {
    case kGlobalChat: GameServer::Get()->SendToPlayers(notifyMessage); break;

    case kSystemMessage: spdlog::error("PlayerId {} attempted to send a System Message.", aSendingPlayer->GetId()); break;

    case kPlayerDialogue: GameServer::Get()->SendToParty(notifyMessage, aSendingPlayer->GetParty()); break;

    case kPartyChat: GameServer::Get()->SendToParty(notifyMessage, aSendingPlayer->GetParty()); break;

    case kLocalChat:
        if (character)
        {
            if (!GameServer::Get()->SendToPlayersInRange(notifyMessage, *character))
                spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
        }
        break;
    default: spdlog::error("{} is not a known MessageType", static_cast<uint64_t>(notifyMessage.MessageType)); break;
    }
}

void OverlayService::HandleChatMessage(const PacketEvent<SendChatMessageRequest>& acMessage) const noexcept
{
    // Intercept and suppress vote commands before scripts handle chat,
    // to avoid third-party script packs printing "unknown command".
    if (IsVoteTimeCommand(acMessage.Packet.ChatMessage))
        return;

    auto [canceled, reason] = m_world.GetScriptService().HandleChatMessage(*acMessage.pPlayer->GetCharacter(), acMessage.Packet.ChatMessage);
    if (canceled)
        return;

    sendPlayerMessage(acMessage.Packet.MessageType, acMessage.Packet.ChatMessage, acMessage.pPlayer);
}

void OverlayService::OnPlayerDialogue(const PacketEvent<PlayerDialogueRequest>& acMessage) const noexcept
{
    sendPlayerMessage(kPlayerDialogue, acMessage.Packet.Text, acMessage.pPlayer);
}

void OverlayService::OnTeleport(const PacketEvent<TeleportRequest>& acMessage) noexcept
{
    Player* pRequester = acMessage.pPlayer;
    if (!pRequester)
        return;

    Player* pTargetPlayer = m_world.GetPlayerManager().GetById(acMessage.Packet.PlayerId);
    if (!pTargetPlayer)
    {
        SendSystemMessage(pRequester, "Teleport request failed: player not found.");
        return;
    }

    if (pTargetPlayer->GetId() == pRequester->GetId())
    {
        SendSystemMessage(pRequester, "You cannot request to teleport to yourself.");
        return;
    }

    auto& pending = m_pendingTeleportRequests[pTargetPlayer->GetId()];
    if (!pending.insert(pRequester->GetId()).second)
    {
        SendSystemMessage(pRequester, fmt::format("Teleport request to {} is already pending.", pTargetPlayer->GetUsername().c_str()));
        return;
    }

    NotifyTeleportRequest notify{};
    notify.RequesterId = static_cast<uint16_t>(pRequester->GetId());
   notify.RequesterName = pRequester->GetUsername();
   pTargetPlayer->Send(notify);

    SendSystemMessage(pTargetPlayer, fmt::format("{} wants to teleport to you.", pRequester->GetUsername().c_str()));
    SendSystemMessage(pRequester, fmt::format("Teleport request sent to {}.", pTargetPlayer->GetUsername().c_str()));
}

void OverlayService::OnTeleportResponse(const PacketEvent<TeleportResponse>& acMessage) noexcept
{
    Player* pResponder = acMessage.pPlayer;
    if (!pResponder)
        return;

    const uint32_t responderId = pResponder->GetId();
    const uint32_t requesterId = acMessage.Packet.RequesterId;

    auto pendingIt = m_pendingTeleportRequests.find(responderId);
    if (pendingIt == m_pendingTeleportRequests.end())
    {
        SendSystemMessage(pResponder, "No pending teleport request found.");
        return;
    }

    auto& requesters = pendingIt->second;
    if (requesters.erase(requesterId) == 0)
    {
        SendSystemMessage(pResponder, "No pending teleport request found.");
        return;
    }

    if (requesters.empty())
        m_pendingTeleportRequests.erase(pendingIt);

    Player* pRequester = m_world.GetPlayerManager().GetById(requesterId);
    if (!pRequester)
    {
        SendSystemMessage(pResponder, "Teleport requester is no longer online.");
        return;
    }

    if (!acMessage.Packet.Accepted)
    {
        SendSystemMessage(pRequester, fmt::format("{} declined your teleport request.", pResponder->GetUsername().c_str()));
        SendSystemMessage(pResponder, fmt::format("You declined {}'s teleport request.", pRequester->GetUsername().c_str()));
        return;
    }

    NotifyTeleport response{};
    if (!PopulateTeleportDestination(m_world, pResponder, response))
    {
        SendSystemMessage(pResponder, "Unable to locate your position for teleport.");
        SendSystemMessage(pRequester, "Teleport request failed.");
        return;
    }

    pRequester->Send(response);
    SendSystemMessage(pRequester, fmt::format("{} accepted your teleport request.", pResponder->GetUsername().c_str()));
    SendSystemMessage(pResponder, fmt::format("You accepted {}'s teleport request.", pRequester->GetUsername().c_str()));
}

void OverlayService::OnPlayerHealthUpdate(const PacketEvent<RequestPlayerHealthUpdate>& acMessage) const noexcept
{
    NotifyPlayerHealthUpdate notify{};
    notify.PlayerId = acMessage.pPlayer->GetId();
    notify.Percentage = acMessage.Packet.Percentage;

    GameServer::Get()->SendToParty(notify, acMessage.pPlayer->GetParty(), acMessage.GetSender());
}

void OverlayService::OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept
{
    if (!acEvent.pPlayer)
        return;

    const uint32_t playerId = acEvent.pPlayer->GetId();
    m_pendingTeleportRequests.erase(playerId);

    for (auto it = m_pendingTeleportRequests.begin(); it != m_pendingTeleportRequests.end();)
    {
        it->second.erase(playerId);
        if (it->second.empty())
            it = m_pendingTeleportRequests.erase(it);
        else
            ++it;
    }
}
