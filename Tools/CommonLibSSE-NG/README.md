# CommonLibSSE-NG patch for Skyrim 1.7.x

Skyrim Together itself doesn't use CommonLibSSE, but SKSE plugins that talk to it through the plugin API usually do
(for example [STCampfireSync](https://github.com/DJLegends1011/STCampfireSync)). Built against an unpatched
[CommonLibSSE-NG](https://github.com/CharmedBaryon/CommonLibSSE-NG), such a plugin can't start on Skyrim 1.7.x.
`skyrim-1.7.patch` fixes that. It's made against CommonLibSSE-NG `b93280e8`, which is also its current `main`.

## Why the change is needed

1. **1.7.x was treated as SE.** CommonLib picks the runtime from the game's minor version: 4 is VR, 6 is AE and
   everything else falls back to SE (`REL::Module`). On 1.7.104 the minor version is 7, so the plugin behaved as if it
   were running on 1.5.97: it looked for `Data/SKSE/Plugins/version-1-7-104-0.bin` (the SE file name) and would have
   used SE address IDs and struct layouts. Players saw:

   > REL/ID.h(219): failed to open address library file

   1.7.x is the AE engine (Skyrim Together's own `AGENTS.md` notes 1.6.1170 and 1.7.104 share engine code and address
   IDs), so the patch treats minor version 6 and later as AE.

2. **The 1.7.x address library uses a newer file format.** With 1.7.x recognised as AE, CommonLib opens the right
   file, `versionlib-1-7-104-0.bin`, but every 1.7.x Address Library file is format 5 while CommonLib only reads
   format 2 (all 1.6.x files are format 2). Players then saw:

   > REL/ID.h(257): Unsupported address library format: 5

   Format 5 is simpler than format 2: a fixed header (format, 4 × version, 64-byte module name, 64-bit pointer size,
   32-bit count), then one 32-bit offset per ID, where 0 means the ID has no address. The patch reads it into the same
   sorted ID → offset table that format 2 produces, keeping only IDs that have an address. Skyrim Together's own
   reader (`Code/client/VersionDb.h`, `LoadV5`) and `Tools/Scripts/addrlib.py` read the same layout.

Nothing changes for 1.5.97, 1.6.x or VR: those still take the old code paths.

## Using it

From the CommonLibSSE-NG checkout your plugin builds against:

```sh
git am <path to this repo>/Tools/CommonLibSSE-NG/skyrim-1.7.patch
```

Then rebuild the plugin. Keep the plugin's SE/AE/VR runtime options enabled so one DLL covers every version.

Before shipping, check that every address ID the plugin's CommonLib calls use exists in the 1.7.x file, for example:

```sh
python Tools/Scripts/addrlib.py -v 1.7.104 403521 400327
```

Anything printed as `not found` would fail at startup with "Failed to find the id within the address library".
