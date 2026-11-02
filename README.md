# Skyrim Together
#### codename "TiltedEvolution"
[![GitHub release](https://img.shields.io/github/v/release/tiltedphoques/TiltedEvolution?color=23314E)](https://github.com/tiltedphoques/TiltedEvolution/releases)
[![Nexus Mods](https://img.shields.io/badge/Nexus%20Mods-download-FB923C?logo=nexusmods&logoColor=white)](https://www.nexusmods.com/skyrimspecialedition/mods/69993)
[![Language](https://img.shields.io/github/languages/top/tiltedphoques/TiltedEvolution?logo=cplusplus&color=F34B7D&logoColor=white)](https://github.com/tiltedphoques/TiltedEvolution)
[![Build status](https://github.com/tiltedphoques/TiltedEvolution/actions/workflows/windows.yml/badge.svg)](https://github.com/tiltedphoques/TiltedEvolution/actions/workflows/windows.yml)
[![Build linux](https://github.com/tiltedphoques/TiltedEvolution/actions/workflows/linux.yml/badge.svg)](https://github.com/tiltedphoques/TiltedEvolution/actions/workflows/linux.yml)
[![Discord](https://img.shields.io/discord/247835175860305931.svg?label=&logo=discord&logoColor=ffffff&color=7389D8&labelColor=6A7EC2)](https://discord.gg/skyrimtogether)

<img src="https://avatars.githubusercontent.com/u/52131158?s=200&v=4" align="right"
     alt="Size Limit logo by Anton Lovchikov" width="110" height="100">

TiltedEvolution is a framework created to enable multiplayer in Bethesda games, currently supporting **Skyrim Special Edition**.

## v1.8.7-8 playtest
This branch (`API-Together-1.8.7-8`, tag `v1.8.7-8`) is a community playtest build of this fork. It is not an official Skyrim Together release. Please report anything below that misbehaves, ideally with two or more players and the client and server logs.

### New features
- **Proximity voice chat**: built in and relayed through the game server, so it needs no separate program or port. Uses Opus. Servers turn it on with `Voice:bVoip`. Players pick an input device and *Connect to voip* in Settings, and can set per-player volumes and a noise gate from the player list.
- **Player-to-player trading**: invites, offers, ready-up countdown, crafted potions and enchanted items, and a trade invitation setting.
- **Name tags and privacy**: name tags above players with on/party/off filters, "Known names" (players outside your party show as *Stranger*), floating chat lines above heads, and Swedish strings.
- **Item drops** (upstream #913, draft): dropped items are synced as server-tracked references. Other players' crafted potions spawn from their recipe.
- **Mod animation sync and `/wave`** (upstream #879, DJLegends).

### New server settings
- `Gameplay:bAllowConsoleCommands` (default `true`). When `false`, commands typed into the console are refused, but the console still opens for reading reference ids, and mods can still run commands. `ToggleConsoleCommands` switches it while the server runs.
- `Voice:bVoip` (default `false`): proximity voice chat.
- `Gameplay:fGoldLossFactor`: values above 1.0 are now a flat amount of gold lost on death.
- Server settings can be overridden with environment variables (upstream #936).

### Sync and stability fixes to test
- NPC ownership and inventories (upstream #932, miredirex): no stale updates after a missed transfer, no duplicate owners, outfits kept through assignment. Inventories wait until the NPC's body and process are loaded. A snapshot without worn items no longer strips a living NPC.
- Dead NPCs stay down when another player loads the cell.
- Exterior door and container locks sync across nearby cells.
- Large messages (big inventories, long mod lists) are no longer sent cut off.
- Sleeping or waiting advances the shared time (upstream #897).
- Weapon draw and sheathe state is restored when refused (upstream #898). Stale object replication bindings are cleaned up (upstream #891).
- Crash fixes: an activated object deleted before activation, a missing launched projectile, teleporting a remote actor into an unloaded exterior cell, and Papyrus natives that weren't ready at startup.
- Summons no longer get death items. Unresolved spells and shouts aren't equipped. Remote actors don't look crouched with different behavior mods. Torso-less remote players get an invisible body. Beast form leave and the Riften North Gate guard scene are fixed.
- Floating Subtitles can hook subtitles alongside Skyrim Together.
- Server list announces over IPv4 only.

### Playtest-only diagnostics
- mimalloc secure mode: heap corruption crashes with a log in `logs\mimalloc_errors.log` instead of freezing.
- Tag builds that aren't on master keep F6/F7 and the console. The console follows Skyrim Together Tweaks' `bEnableConsole`.

## Getting started
To play Skyrim Together, go to the [nexus page](https://www.nexusmods.com/skyrimspecialedition/mods/69993).

For general information, go to the [TiltedEvolution Wiki](https://wiki.tiltedphoques.com/tilted-online/).

Check out the [build guide](https://wiki.tiltedphoques.com/tilted-online/technical-documentation/build-guide) for setup and development info on the project. When writing code, check the CODE_GUIDELINES.md and make sure to run clang-format!

## Reporting bugs
If you would like to report a bug please report them in the "Issues" tab on this page. Detailed and reproducible bug reports are of great importance for the development of the project.

## Contributing
Have some experience in C++, and want to help advance the project faster? Contribute!
- Check the issues, it's a good place to start when you don't know what to do.
- Fork the repository and create pull requests to this repository.
- Create pull requests to the "dev" branch, not to master.
- Try to keep your code clean, following the code guidelines.

## Main project source tree

* [**client/**](./Code/client): Skyrim SE client: engine hooks, reverse-engineered game types and the sync logic.
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

## License
[![GNU GPLv3 Image](https://www.gnu.org/graphics/gplv3-127x51.png)](http://www.gnu.org/licenses/gpl-3.0.en.html)

TiltedEvolution is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
