# Skyrim Together Reborn: ScriptFixes

A fork of [Skyrim Together Reborn](https://github.com/tiltedphoques/TiltedEvolution) (codename "TiltedEvolution"),
the multiplayer mod for Skyrim Special Edition. It's where fixes are tested in real playtests before they go upstream,
and it adds two things official releases don't have:

- **Builds for Skyrim 1.6.1170** (the GOG and older Steam version), next to the usual 1.7.104.
- **A Plugin API**, so SKSE plugins can sync their own features between players without patching Skyrim Together.
  Ready-made plugins and the guides to write your own are in
  [absol89/AllTogetherSync](https://github.com/absol89/AllTogetherSync).

This is not an official Skyrim Together release. Please report problems with these builds here, not to the Skyrim
Together team.

## Playing

Download a build from [Releases](https://github.com/absol89/TiltedEvolutionScriptFixes/releases). Each release has a
zip per game version:

| Your Skyrim | Download |
|---|---|
| Steam, 1.7.104 (current) | `...-steam-1.7.104.zip` |
| GOG, or Steam downgraded to 1.6.1170 | `...-1.6.1170-GOG.zip` |

Install it with your mod manager in place of the official Skyrim Together. The release notes say what changed.

**Everyone in a session, and the server, must use builds from the same release.** The server turns away a client
from a different build.

To sync more mods (RaceMenu looks, SkyParkour, ragdolls, LipSync), add the plugins from
[AllTogetherSync](https://github.com/absol89/AllTogetherSync). They're ordinary SKSE plugins and replace
none of Skyrim Together's files.

### Reporting a bug

Open an [issue](https://github.com/absol89/TiltedEvolutionScriptFixes/issues) and include:

- which release and which Skyrim version you play on, and whether you hosted or joined;
- what you did, and what happened instead;
- your logs: Skyrim Together's own (in `SkyrimTogetherReborn\logs`; with Mod Organizer 2, look in its `overwrite`
  folder), and your crash log if the game crashed;
- if the game froze: any `.dmp` files the plugins wrote next to the SKSE logs.

## Branches

| Branch | What it is |
|---|---|
| `1.8-playtest` | The playtest builds. Releases are built from here, and pull requests go here. |
| `master` | An older validation branch. Not used for builds. |
| `feat/...`, `fix/...`, `codex/...` | Pull requests to upstream Skyrim Together, each based on upstream's `dev`. |

Tags like `v1.8.7-7` mark the commit each playtest release was built from.

## Contributing

**Your plugin needs something from Skyrim Together.** Read
[Changing Skyrim Together](https://github.com/absol89/AllTogetherSync/blob/main/docs/changing-skyrim-together.md):
open an issue describing what your plugin needs, then a pull request against `1.8-playtest` that adds it as a new
Plugin API version. The API is in [`Code/plugin_api/include/SkyrimTogetherAPI.h`](https://github.com/absol89/TiltedEvolutionScriptFixes/blob/1.8-playtest/Code/plugin_api/include/SkyrimTogetherAPI.h).

**You fixed a bug in Skyrim Together.** If it helps every player, send it upstream to
[tiltedphoques/TiltedEvolution](https://github.com/tiltedphoques/TiltedEvolution) on its `dev` branch. If you want it
tested in our playtests first, or it only concerns this fork (1.6.1170, the Plugin API), open the pull request here
against `1.8-playtest`.

Please follow [CODE_GUIDELINES.md](CODE_GUIDELINES.md) and run clang-format. [AGENTS.md](AGENTS.md) gives an overview
of the architecture: what the client and the server each do, and where things live.

### Building

The simplest way is GitHub Actions. Fork this repository, enable Actions on your fork, then open **Actions → Playable
Skyrim Together Build → Run workflow** and pick your branch. It builds the client for 1.7.104 and 1.6.1170, and the
server, and attaches them to the run.

To build on your own PC, follow upstream's
[build guide](https://wiki.tiltedphoques.com/tilted-online/technical-documentation/build-guide). For a 1.6.1170 build,
configure with `xmake f --skyrim_version=1.6.1170`.

### Source tree

* [**client/**](./Code/client): Skyrim SE client: engine hooks, reverse-engineered game types and the sync logic.
* [**plugin_api/**](https://github.com/absol89/TiltedEvolutionScriptFixes/tree/1.8-playtest/Code/plugin_api): The Plugin API header that SKSE plugins include.
* [**immersive_launcher/**](./Code/immersive_launcher): `SkyrimTogether.exe`, which starts the game with the client loaded.
* [**immersive_elf/**](./Code/immersive_elf): `EarlyLoad.dll` helper DLL for the launcher. Detects if Mod Organizer 2 was injected first.
* [**tp_process/**](./Code/tp_process): Worker for the CEF (Chromium Embedded Framework) overlay.
* [**skyrim_ui/**](./Code/skyrim_ui): In-game UI, written in Angular/TypeScript.
* [**server/**](./Code/server): Game server (`STServer.dll`).
* [**server_runner/**](./Code/server_runner): Dedicated server executable that hosts the server DLL.
* [**encoding/**](./Code/encoding): Network message and struct definitions shared by the client and server.
* [**common/**](./Code/common), [**base/**](./Code/base): Shared utilities.
* [**components/**](./Code/components): Shared modules (console/settings, ESP loader, resources, ImGui).
* [**tests/**](./Code/tests): Unit tests.
* [**GameFiles/**](./GameFiles): Plugins, scripts and assets shipped with the mod.
* [**Libraries/**](./Libraries): Networking, hooking, reverse-engineering and UI libraries (git submodules).
* [**Tools/**](./Tools): Reverse-engineering and address scripts.

## Credits and license

Skyrim Together Reborn is made by [Tilted Phoques](https://github.com/tiltedphoques) and its contributors. This fork
is maintained by absol89, with the Plugin API's V2 designed by mailamea10.

[![GNU GPLv3 Image](https://www.gnu.org/graphics/gplv3-127x51.png)](http://www.gnu.org/licenses/gpl-3.0.en.html)

TiltedEvolution is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
