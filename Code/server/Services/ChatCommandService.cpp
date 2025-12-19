#include <Services/ChatCommandService.h>

#include <Components.h>
#include <GameServer.h>
#include <World.h>

#include <ChatMessageTypes.h>
#include <Messages/NotifyChatMessageBroadcast.h>
#include <Messages/SendChatMessageRequest.h>
#include <Messages/TeleportCommandResponse.h>
#include <Services/VoteTimeService.h>

#include <Events/PacketEvent.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <regex>

namespace
{
TiltedPhoques::String ToLowerCopy(const TiltedPhoques::String& text)
{
    TiltedPhoques::String out = text;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

TiltedPhoques::String TrimCopy(const TiltedPhoques::String& value)
{
    TiltedPhoques::String trimmed = value;
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front())))
        trimmed.erase(trimmed.begin());
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back())))
        trimmed.pop_back();
    return trimmed;
}

bool ParseIntArg(const TiltedPhoques::String& text, int& outValue)
{
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    int value = 0;
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc() || result.ptr != end)
        return false;
    outValue = value;
    return true;
}

Player* FindPlayerByUsernameInsensitive(World& world, const TiltedPhoques::String& username)
{
    if (username.empty())
        return nullptr;

    const TiltedPhoques::String needle = ToLowerCopy(username);
    for (Player* player : world.GetPlayerManager())
    {
        if (!player)
            continue;

        if (ToLowerCopy(player->GetUsername()) == needle)
            return player;
    }

    return nullptr;
}

TiltedPhoques::String JoinArgs(const TiltedPhoques::Vector<TiltedPhoques::String>& args)
{
    TiltedPhoques::String joined;
    for (size_t i = 0; i < args.size(); ++i)
    {
        if (i > 0)
            joined += ' ';
        joined += args[i];
    }
    return joined;
}
} // namespace

class HelpCommand final : public ChatCommand
{
public:
    const char* GetName() const noexcept override { return "help"; }
    const char* GetDescription() const noexcept override { return "Show available commands"; }

    bool Execute(World& world, Player* player, const ChatCommandContext& context) const override
    {
        (void)context;
        auto& service = world.GetChatCommandService();
        const auto commands = service.BuildCommandList(player);
        TiltedPhoques::String line = "Commands:";
        for (const auto& command : commands)
        {
            line += " ";
            line += command.Name;
        }
        service.SendSystemMessage(player, line.c_str());
        return true;
    }
};

class LocalCommand final : public ChatCommand
{
public:
    const char* GetName() const noexcept override { return "local"; }
    const char* GetDescription() const noexcept override { return "Send a message to nearby players"; }

    bool Execute(World& world, Player* player, const ChatCommandContext& context) const override
    {
        const auto content = JoinArgs(context.Args);
        if (content.empty())
        {
            world.GetChatCommandService().SendSystemMessage(player, "Usage: /local <message>");
            return true;
        }

        world.GetChatCommandService().SendChatMessage(kLocalChat, content, player);
        return true;
    }
};

class PartyCommand final : public ChatCommand
{
public:
    const char* GetName() const noexcept override { return "party"; }
    const char* GetDescription() const noexcept override { return "Send a message to your party"; }

    bool Execute(World& world, Player* player, const ChatCommandContext& context) const override
    {
        const auto content = JoinArgs(context.Args);
        if (content.empty())
        {
            world.GetChatCommandService().SendSystemMessage(player, "Usage: /party <message>");
            return true;
        }

        world.GetChatCommandService().SendChatMessage(kPartyChat, content, player);
        return true;
    }
};

class VoteTimeCommand final : public ChatCommand
{
public:
    const char* GetName() const noexcept override { return "votetime"; }
    const char* GetDescription() const noexcept override { return "Start a vote to change time"; }

    bool Execute(World& world, Player* player, const ChatCommandContext& context) const override
    {
        (void)context;
        world.ctx().at<VoteTimeService>().HandleChatCommand(player, context.Raw);
        return true;
    }
};

class YesCommand final : public ChatCommand
{
public:
    const char* GetName() const noexcept override { return "yes"; }
    const char* GetDescription() const noexcept override { return "Vote yes in the active vote"; }

    bool Execute(World& world, Player* player, const ChatCommandContext& context) const override
    {
        (void)context;
        world.ctx().at<VoteTimeService>().HandleChatCommand(player, "/yes");
        return true;
    }
};

class NoCommand final : public ChatCommand
{
public:
    const char* GetName() const noexcept override { return "no"; }
    const char* GetDescription() const noexcept override { return "Vote no in the active vote"; }

    bool Execute(World& world, Player* player, const ChatCommandContext& context) const override
    {
        (void)context;
        world.ctx().at<VoteTimeService>().HandleChatCommand(player, "/no");
        return true;
    }
};

class SetTimeCommand final : public ChatCommand
{
public:
    const char* GetName() const noexcept override { return "settime"; }
    const char* GetDescription() const noexcept override { return "Set the server time"; }
    bool RequiresAdmin() const noexcept override { return true; }

    bool Execute(World& world, Player* player, const ChatCommandContext& context) const override
    {
        if (context.Args.size() < 2)
        {
            world.GetChatCommandService().SendSystemMessage(player, "Usage: /settime <hour> <minute>");
            return true;
        }

        int hour = 0;
        int minute = 0;
        if (!ParseIntArg(context.Args[0], hour) || !ParseIntArg(context.Args[1], minute))
        {
            world.GetChatCommandService().SendSystemMessage(player, "Usage: /settime <hour> <minute>");
            return true;
        }

        const float timescale = world.GetCalendarService().GetTimeScale();
        if (!world.GetCalendarService().SetTime(hour, minute, timescale))
        {
            world.GetChatCommandService().SendSystemMessage(player, "SetTime failed: hour must be 0-23 and minute 0-59.");
            return true;
        }

        world.GetChatCommandService().SendSystemMessage(player, "Time updated.");
        return true;
    }
};

class TeleportCommand final : public ChatCommand
{
public:
    explicit TeleportCommand(const char* alias)
        : m_alias(alias)
    {
    }

    const char* GetName() const noexcept override { return m_alias; }
    const char* GetDescription() const noexcept override { return "Teleport to a player"; }
    bool RequiresAdmin() const noexcept override { return true; }

    bool Execute(World& world, Player* player, const ChatCommandContext& context) const override
    {
        if (context.Args.empty())
        {
            world.GetChatCommandService().SendSystemMessage(player, "Usage: /teleport <player>");
            return true;
        }

        Player* target = FindPlayerByUsernameInsensitive(world, context.Args[0]);
        if (!target)
        {
            world.GetChatCommandService().SendSystemMessage(player, "Teleport failed: player not found.");
            return true;
        }

        const auto character = target->GetCharacter();
        if (!character.has_value())
        {
            world.GetChatCommandService().SendSystemMessage(player, "Teleport failed: target has no character.");
            return true;
        }

        const auto* movement = world.try_get<MovementComponent>(*character);
        if (!movement)
        {
            world.GetChatCommandService().SendSystemMessage(player, "Teleport failed: target position unavailable.");
            return true;
        }

        TeleportCommandResponse response{};
        const auto& cellComponent = target->GetCellComponent();
        response.CellId = cellComponent.Cell;
        response.WorldSpaceId = cellComponent.WorldSpaceId;
        response.Position = movement->Position;
        player->Send(response);

        world.GetChatCommandService().SendSystemMessage(player, "Teleporting...");
        return true;
    }

private:
    const char* m_alias;
};

ChatCommandService::ChatCommandService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
{
    (void)aDispatcher;
    RegisterCommand(TiltedPhoques::CastUnique<ChatCommand>(TiltedPhoques::MakeUnique<HelpCommand>()));
    RegisterCommand(TiltedPhoques::CastUnique<ChatCommand>(TiltedPhoques::MakeUnique<LocalCommand>()));
    RegisterCommand(TiltedPhoques::CastUnique<ChatCommand>(TiltedPhoques::MakeUnique<PartyCommand>()));
    RegisterCommand(TiltedPhoques::CastUnique<ChatCommand>(TiltedPhoques::MakeUnique<VoteTimeCommand>()));
    RegisterCommand(TiltedPhoques::CastUnique<ChatCommand>(TiltedPhoques::MakeUnique<YesCommand>()));
    RegisterCommand(TiltedPhoques::CastUnique<ChatCommand>(TiltedPhoques::MakeUnique<NoCommand>()));
    RegisterCommand(TiltedPhoques::CastUnique<ChatCommand>(TiltedPhoques::MakeUnique<SetTimeCommand>()));
    RegisterCommand(TiltedPhoques::CastUnique<ChatCommand>(TiltedPhoques::MakeUnique<TeleportCommand>("teleport")));
    RegisterCommand(TiltedPhoques::CastUnique<ChatCommand>(TiltedPhoques::MakeUnique<TeleportCommand>("tp")));
}

bool ChatCommandService::TryHandle(Player* player, const TiltedPhoques::String& message) const
{
    ChatCommandContext context{};
    if (!ParseCommand(message, context))
        return false;

    const auto* command = FindCommand(context.NameLower);
    if (!command)
        return false;

    if (command->RequiresAdmin() && !IsAdmin(player))
    {
        SendSystemMessage(player, "You do not have permission to use that command.");
        return true;
    }

    return command->Execute(m_world, player, context);
}

void ChatCommandService::SendCommandList(Player* player) const noexcept
{
    if (!player)
        return;

    NotifyCommandList notify{};
    notify.Commands = BuildCommandList(player);
    player->Send(notify);
}

TiltedPhoques::Vector<NotifyCommandList::CommandEntry> ChatCommandService::BuildCommandList(Player* player) const
{
    TiltedPhoques::Vector<NotifyCommandList::CommandEntry> out;
    const bool admin = IsAdmin(player);
    out.reserve(m_commands.size());
    for (const auto& command : m_commands)
    {
        if (command->RequiresAdmin() && !admin)
            continue;
        NotifyCommandList::CommandEntry entry{};
        entry.Name = TiltedPhoques::String("/") + command->GetName();
        entry.Description = command->GetDescription();
        out.push_back(std::move(entry));
    }
    return out;
}

void ChatCommandService::SendSystemMessage(Player* player, std::string_view message) const noexcept
{
    if (!player)
        return;

    NotifyChatMessageBroadcast notify{};
    notify.MessageType = ChatMessageType::kSystemMessage;
    notify.PlayerName = "Server";
    notify.ChatMessage = message.data();
    player->Send(notify);
}

void ChatCommandService::SendChatMessage(ChatMessageType type, const TiltedPhoques::String& content, Player* sender) const noexcept
{
    if (!sender)
        return;

    NotifyChatMessageBroadcast notifyMessage{};
    std::regex escapeHtml{"<[^>]+>\\s+(?=<)|<[^>]+>"};
    notifyMessage.MessageType = type;
    notifyMessage.PlayerName = std::regex_replace(sender->GetUsername(), escapeHtml, "");
    notifyMessage.ChatMessage = std::regex_replace(content, escapeHtml, "");

    auto character = sender->GetCharacter();

    switch (notifyMessage.MessageType)
    {
    case kGlobalChat: GameServer::Get()->SendToPlayers(notifyMessage); break;
    case kSystemMessage: break;
    case kPlayerDialogue: GameServer::Get()->SendToParty(notifyMessage, sender->GetParty()); break;
    case kPartyChat: GameServer::Get()->SendToParty(notifyMessage, sender->GetParty()); break;
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

bool ChatCommandService::ParseCommand(const TiltedPhoques::String& raw, ChatCommandContext& out) const
{
    out.Raw = TrimCopy(raw);
    if (out.Raw.empty() || out.Raw.front() != '/')
        return false;

    TiltedPhoques::String text = out.Raw;
    text.erase(text.begin());
    text = TrimCopy(text);
    if (text.empty())
        return false;

    size_t pos = 0;
    while (pos < text.size())
    {
        while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])))
            ++pos;
        if (pos >= text.size())
            break;
        size_t end = pos;
        while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end])))
            ++end;
        out.Args.emplace_back(text.substr(pos, end - pos));
        pos = end;
    }

    if (out.Args.empty())
        return false;

    out.NameLower = ToLowerCopy(out.Args[0]);
    out.Args.erase(out.Args.begin());
    return true;
}

const ChatCommand* ChatCommandService::FindCommand(std::string_view nameLower) const
{
    TiltedPhoques::String key(nameLower);
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const auto it = m_commandLookup.find(key);
    return it == m_commandLookup.end() ? nullptr : it->second;
}

bool ChatCommandService::IsAdmin(Player* player) const
{
    if (!player)
        return false;
    return m_world.GetAdminService().IsAdmin(player->GetUsername());
}

void ChatCommandService::RegisterCommand(TiltedPhoques::UniquePtr<ChatCommand> command)
{
    if (!command)
        return;

    const TiltedPhoques::String key = ToLowerCopy(command->GetName());
    m_commandLookup[key] = command.get();
    m_commands.push_back(std::move(command));
}
