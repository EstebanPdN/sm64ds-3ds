# Documentation

The integrated ROM-only game is the current development path; its
native-renderer test candidates (native-test CIA) are the current testing
line. The model viewer, experimental runtime and movement laboratory remain
useful source references, with their own targets and limits.

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
| Run the automatic SELECT+START test | [Test mode](TEST_MODE.md) |
| Build a native-renderer test candidate | [Native pipeline](../tools/native_renderer/pipeline/README.md) |
| Read exact candidate validation | [Status journal](STATUS.md), [current-work checklist](CURRENT_WORK_CHECKLIST.md) |
| Make a desktop companion | [SM64DS Packer](SM64DS_PACKER.md) |

## Detailed implementation references

- [Integration and render60](INTEGRATED_NATIVE.md), [adapter contracts and development](DEVELOPMENT.md).
- [ROM-only preparation](ROM_ONLY_INSTALL.md), [native files](NATIVE_FILES.md), [input](NATIVE_INPUT.md).
- [RAM capture formats](NATIVE_RAM_DIAGNOSTICS.md), [performance campaign](PERFORMANCE_CAMPAIGN.md).
- [Movement laboratory](MOVEMENT_LAB.md), [native projection batches](NATIVE_PROJECTION_BATCHES.md).
- [Renderer study](NP49_RENDERER_STUDY.md), [GX decoding](NATIVE_GX_DECODE_AUDIT.md).
- [Runtime](NATIVE_RUNTIME.md), [player](NATIVE_PLAYER.md), [primitives](NATIVE_PRIMITIVES.md), [legacy packaging](PACKAGING.md).

## Native renderer

- [Source map](../source/native_renderer/README.md), [game-side adapters](../source/integrated_native/renderer/README.md), [tools](../tools/native_renderer/README.md).
- [Design](NATIVE-RENDERER.md), [assets](NATIVE-ASSETS.md), [model producers](NATIVE-MODEL-PRODUCERS.md), [compositor](NATIVE-COMPOSITOR.md), [display state](NATIVE-DISPLAY-STATE.md).
- [Component capture](NATIVE-COMPONENT-CAPTURE.md), [interface](NATIVE-INTERFACE.md), [menu 2D](NATIVE-MENU-2D.md), [backgrounds](NATIVE-BACKGROUNDS.md), [particles](NATIVE-PARTICLES.md).
- [60 FPS budget and measurements](NATIVE_60FPS_BUDGET.md), [attribute batch prototype](NATIVE_ATTRIBUTE_BATCH_PROTOTYPE.md), [resident palette design](NATIVE_RESIDENT_PALETTE_DESIGN.md).
- [3DS port rendering research](3DS_PORT_RENDER_RESEARCH.md), [DS and SM64DS ports research](PORTS_RESEARCH_2026-11-17.md), [ARMv6 audio mixer](PERF_ARMV6_MIXER_2026-11-09.md), [double-image performance study](PERFORMANCE_DOUBLE_BUILD55.md).

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
- [Native renderer bug log, 2026-10-27](NATIVE_RENDERER_BUGS_2026-10-27.md), [test-mode journal (Spanish)](MODO_PRUEBA.md).

Historical plans may include tasks completed in later candidates. They are
retained to explain decisions and failures, not presented as current promises.
