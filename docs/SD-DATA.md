# SD data and recovery

The integrated game uses `SD:/3ds/sm64ds/`. Replacing its executable should
preserve this directory and existing saves/settings. Do not use the older
model viewer's data layout for the integrated ROM-only game.

| File | Purpose |
| --- | --- |
| Any compatible `.nds` | User's European ASMP rev0 ROM; read-only input. |
| Any matching `.sm64ds` | Prepared companion, read directly by member offset. |
| `prepared.sm64ds` | Default filename created by on-console preparation. |
| Companion `.part` | Temporary preparation output; validated before publication/recovery. |
| `user-test.sav` | Current integrated game's save file. |
| `settings.ini` | Persistent presentation, input, audio and developer options. |
| `.bak` siblings | Recovery copy used by checked replacement/read paths. |
| `user-test.log` | Local boot/runtime log. |
| `level-files.bin` | Learned level file lists used by bounded preloading. |
| `dumps/`, `dump-sequence.txt` | Private diagnostic folders and consecutive numbering. |

ROM filenames are free, with case-insensitive `.nds` extension matching.
Renaming an incompatible ROM does not change its identity. Companion
filenames are also free; preserve the `.sm64ds` extension and matching schema.
Both inputs are local; the executable is installed separately.

## First preparation

Provide a compatible ROM or prepared companion in the directory above.
Preparation validates the input and writes one archive, avoiding thousands
of SD entries. Leave space for the ROM, companion, temporary output and save
backups; the existing installation guide recommends at least 64 MiB beyond
the ROM. Full diagnostics can require substantially more space.

The original first interactive introduction expects a touch on the central
star, rather than the text beneath it. START opens pause during gameplay.
The lower-left BUILD label identifies the installed candidate.

## Interrupted writes and settings recovery

Checked storage validates complete writes, flush/close status and replacement.
The 3DS path moves an existing primary to a backup before replacing it, with
rollback on failure. Missing primary settings can recover from `.bak`.
Overlong paths fail before write. This does not promise SD durability under
power removal; retain independent backups of valuable saves.

The `.sm64ds` format and exact integrity fields are documented in
[Packer](SM64DS_PACKER.md). Invalid/stale companions must be regenerated from
the supported ROM using the matching build's preparation rules. Do not
delete saves to repair a companion or a settings problem.

## Configuration reference

`options/enhance_settings.h` is the source of truth for settings fields and
defaults; `enhance_settings.cpp` owns parsing, serialization and limits.
Unknown or invalid fields are counted and skipped. Display settings that
require renderer reconfiguration apply at their defined resume/startup
boundary; live settings are exchanged through the existing atomic state.

Keep native dumps, saves, ROMs, companions and firmware out of GitHub issues
and source archives. See [Diagnostics](DIAGNOSTICS.md) for a useful report.
