# Source map

The current game uses [integrated_native/](integrated_native/README.md).
Its adapters are copied into a generated instance of the pinned backend by
checked preparation scripts. The backend itself lives under `third_party/`.

| Directory | Role |
| --- | --- |
| `integrated_native/` | Maintained ROM-only boot, UI, camera, movement, diagnostics and presentation adapters. |
| `movement_lab/` | Independent DS-world/N64-controller laboratory. |
| `core/` | Shared parsers, geometry, models and fixed math used by host checks and earlier runtime work. |
| `ctr/` | Historical viewer/runtime frontend and native platform adapters. |
| `game_port/` | Earlier original-game service/ABI replacements. |
| `game_data/` | Regional data declarations, relocation and callbacks. |
| `vendor/ntr/` | Attributed NTR resource decoders; retain their MIT notice. |

Older experiments remain complete source references rather than being
deleted as apparently unused code. Their targets, input regions and status
differ from the integrated game. Start with [Architecture](../docs/ARCHITECTURE.md)
before changing an ownership boundary.
