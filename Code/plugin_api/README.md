# Plugin API V1 and scoped channels

The client exports `SkyrimTogetherRequestPluginAPI` from SkyrimTogether.exe. Request V1 to get
`IVSkyrimTogether1`. Unknown versions return null. There is deliberately no generic `RequestPluginAPI`
export: plugins looking for an absent third-party DLL must not accidentally receive STR's interface.
The launcher preserves its PE export directory while loading SkyrimSE.exe.

This restores DJLegends' V1 party-message implementation from the October 17 commits
2108feaa, 433bc52d, 734c5410, bc63f848, f5b4625d, 18a5312a, 0029b127 and 5fa96b48.
Plugin API V2 (84cbc0ce and later V2 implementations) is excluded. Scoped registration below is new code;
it does not expose actor control or use the V2 interface.

## Choose an audience in the plugin

Existing `RegisterChannel` calls remain party-only. New plugins may look up
`SkyrimTogetherRegisterScopedChannel` using `RegisterScopedChannel_t` from the header:

```cpp
auto reg = reinterpret_cast<SkyrimTogetherAPI::RegisterScopedChannel_t>(
    GetProcAddress(GetModuleHandleW(nullptr), SkyrimTogetherAPI::kRegisterScopedChannelName));
auto channel = reg
    ? reg("Example.MyPlugin", SkyrimTogetherAPI::ChannelScope::Nearby, OnMessage, OnPeersChanged)
    : api->RegisterChannel("Example.MyPlugin", OnMessage, OnPeersChanged);
api->Send(channel, bytes, length);
```

Use `ChannelScope::Party` or the original registration method for private party features. Use
`ChannelScope::Nearby` for features visible to players in the local area, regardless of party.
The channel name and scope must match on both clients. Names are not hardcoded in STR, so new plugins
can use either audience without another executable release. Channel registrations last until process exit;
callbacks must remain valid for that lifetime. Invalid scopes/duplicate names return handle 0.

`Send` broadcasts to the channel's audience; `SendTo` addresses one eligible peer. `IsInParty` retains
its literal V1 meaning; it is not a requirement for nearby sends. `NotInParty` from a nearby targeted
send means the target is not in the current nearby snapshot (the existing result enum is unchanged).
The server independently checks every recipient: party equality, or matching interior/worldspace
and loaded-grid range. Both client and server must run the new protocol build.

## Party leader

The party leader hosts the world: NPCs, scenes and quest progression run on the leader's client. Plugins
that move NPCs or start scenes, or that need one authoritative client, can look up
`SkyrimTogetherGetPartyLeaderId` using `GetPartyLeaderId_t` from the header:

```cpp
auto getLeader = reinterpret_cast<SkyrimTogetherAPI::GetPartyLeaderId_t>(
    GetProcAddress(GetModuleHandleW(nullptr), SkyrimTogetherAPI::kGetPartyLeaderIdName));
const uint32_t leader = getLeader ? getLeader() : 0;
const bool isLeader = leader != 0 && leader == api->GetLocalPlayerId();
```

It returns 0 while not in a party, and is absent on older executables, so plugins need a fallback (for
example: act alone when not in a party). It reads the same locked snapshot as the other getters, so a
leadership change shows up after the next World update.

## Lifetime and resynchronization

The registration callback reports audience joins/leaves. Nearby membership follows loaded remote player
entities; party membership follows the current party. Snapshots refresh after the World update. Callbacks
run in that update context, NOT guaranteed on Skyrim's main thread. Queue engine work through SKSE AddTask.
Calls from plugin threads use locked snapshots. Do not retain message data beyond the callback.

There is no persistent object store or replay on the server. A plugin resends its own state on a peer's
arrival and removes remote state on departure. Objects which should survive their owner leaving require
plugin-level persistence/ownership. Spatial objects must include or validate their own location context;
a nearby channel follows the sending player, not an independently tracked object.

Limits remain 256 payload bytes, 64 channel-name bytes, and a per-player 20 messages/second budget with
40-message bursts across channels. Plugins with larger state should batch and pace their announcements.

## Validation

Check exports in the built launcher, protocol round trips for both scopes and invalid scope rejection.
In game: test Campfire F8, late arrival, disconnect/reconnect and cell travel with two players outside
any party; then repeat with party-only configuration and a third player outside the party. Confirm
unrelated plugins never find a generic RequestPluginAPI in the launcher. No new engine hooks or offsets
are introduced here; actual runtime export visibility and in-game behavior still require this test.
