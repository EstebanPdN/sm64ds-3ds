# Documentation

The integrated ROM-only game is the current development path. The model
viewer, experimental runtime and movement laboratory remain useful source
references, with their own targets and limits.

## Current guides

| Task | Guide |
| --- | --- |
| Understand supported inputs and evidence | [Compatibility](COMPATIBILITY.md) |
| Prepare the SD card or recover settings | [SD data](SD-DATA.md) |
| Build from a clean source checkout | [Building](BUILDING.md) |
| Locate a subsystem or entry point | [Repository map](REPOSITORY.md) |
| Understand runtime ownership | [Architecture](ARCHITECTURE.md) |
| Run the relevant checks | [Testing](TESTING.md) |
| Investigate a crash or slowdown | [Diagnostics](DIAGNOSTICS.md) |
| Inspect source pins and licenses | [Upstream](UPSTREAM.md), [notices](legal/THIRD_PARTY_NOTICES.md) |
| Find outstanding work | [Roadmap](ROADMAP.md) |
| Read exact candidate validation | [Status journal](STATUS.md) |
| Make a desktop companion | [SM64DS Packer](SM64DS_PACKER.md) |

## Detailed implementation references

- [Integration and render60](INTEGRATED_NATIVE.md), [adapter contracts and development](DEVELOPMENT.md).
- [ROM-only preparation](ROM_ONLY_INSTALL.md), [native files](NATIVE_FILES.md), [input](NATIVE_INPUT.md).
- [RAM capture formats](NATIVE_RAM_DIAGNOSTICS.md), [performance campaign](PERFORMANCE_CAMPAIGN.md).
- [Movement laboratory](MOVEMENT_LAB.md), [native projection batches](NATIVE_PROJECTION_BATCHES.md).
- [Renderer study](NP49_RENDERER_STUDY.md), [GX decoding](NATIVE_GX_DECODE_AUDIT.md).
- [Runtime](NATIVE_RUNTIME.md), [player](NATIVE_PLAYER.md), [primitives](NATIVE_PRIMITIVES.md), [legacy packaging](PACKAGING.md).

These longer records contain dated observations about different binaries.
The newer compatibility/build guides take precedence for current setup;
private `build/` receipts named in journals are not part of this checkout.

## Historical plans and investigations

- [Pre-publication README](history/README_PRE_PUBLICATION.md).
- [October 21 inventory](INVENTARIO_PORT_2026-10-21.md), [current-work checklist](CURRENT_WORK_CHECKLIST.md).
- [October 22 code pass](CODE_PASS_2026-10-22.md), [test plan](PLAN_PRUEBAS_2026-10-22.md).
- [Menu implementation](IMPLEMENTATION_MENU_2026-10-09.md), [options progress](IN_GAME_OPTIONS_PROGRESS.md).
- [HUD progress](NATIVE_HUD_PROGRESS.md), [HUD/camera/front-options revision](HUD_CAMERA_FRONT_OPTIONS_REVISION.md).
- [Upstream review](UPSTREAM_REVIEW_2026-10-16.md).

Historical plans may include tasks completed in later candidates. They are
retained to explain decisions and failures, not presented as current promises.
