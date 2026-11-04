# Remote corpse synchronization

The remote component retains the canonical dead/alive state even without loaded 3D.
Spawning and reconciliation defer NPC death until a high process, middle process,
loaded node and ready animation graph are available. Zero-health broadcasts use the
same path. Death notifications update pending spawns, including those without a form
component. Ownership grants that would kill an unready actor are declined.

For dead NPCs, movement snapshots still synchronize position, but do not overwrite
the animation graph. Queued actions are discarded without
resetting the differential decoder. Already-dead actors are not resurrected to repair
their pose; this patch prevents premature death and subsequent living animation replay.

## Binary evidence

Read-only Ghidra MCP, Skyrim SE 1.6.1170 unpacked database, image base 0x140000000.
Address Library script maps ID 37896 to 0x1406972D0 (KillImpl).
The decompiled routine has an early virtual-state check at vtable +0x4C8 and returns
when that check succeeds. Its physical-body branch checks:

```c
lVar12 = param_1[0x1f];
if (((lVar12 == 0) || (*(char *)(lVar12 + 0x137) != 0)) || (lVar10 == 0)) {
    FUN_14068e640(param_1);
    *(uint *)(param_1 + 0x1d) |= 0x800;
} else {
    // loaded-body death processing
}
```

`lVar10` comes from 0x1402E5770, which returns the cached loaded node or the pointer
at [actor+0x68]+0x68. The repository already asserts AIProcess::movementType at 0x137;
CommonLibSSE-NG names this byte processLevel and defines high as zero. No offsets or
hooks were added. Requiring a ready animation graph is a conservative source-level
guard; it is not proven sufficient by the decompilation. Skyrim 1.7.104 was not
independently inspected, so equivalence remains assumed.

## In-game verification still required

- Player 1 owns an existing dead NPC near Embershard Mine; player 2 approaches and
  must see a lying, lootable corpse without idle animations.
- Repeat with player 2 leaving/re-entering, joining late, and receiving ownership.
- Kill a living remote NPC while player 2 is loading its cell.
- Verify a genuine respawn permits normal animation again; player death is unchanged.
- Confirm corpse loot/equipment and ordinary living NPC animations still work.

Compilation does not establish that these gameplay checks pass. Previously saved
standing corpses may still need a normal game reload; no forced resurrection is used.
