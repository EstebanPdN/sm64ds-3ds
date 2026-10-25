# Test ownership

The source tree contains both portable regressions and resource-backed
investigation runners. Registered ROM-free tests live in the root CMake
configuration and `cmake/IntegratedHostTests.cmake`.

| Suite | Owns |
| --- | --- |
| `test_core`, math/model/NARC/geometry/input families | Shared parsing and historical native boundaries. |
| `options_menu/` | Menu actions, art bounds, publication, diagnostics and source-preparation contracts. |
| `options_camera/` | C-stick, stylus and runtime camera handoff. The Python pixel gate also needs its declared art fixtures and Pillow. |
| `options_display/` | Display plans, title backdrop and presentation ownership. |
| `movement_lab/` | Buttons, jump/session continuity, lifetime, DS bridge and platform/collision contacts. |
| `integrated_audio_save/` | Gain ramp, queue metrics, boot allocation, bounded settings and failure-injected storage. |
| `native_benchmark/` | Scenario marker/model contracts. |
| `test_build_tools.py` | SDK resolution, source freeze, candidate closure, BUILD reservation and unsupported optimized-Python rejection. |
| `test_integrated_vm.py`, `test_read_mapped_capture.py` | Authored process-capture reader bounds. |
| `reference/`, `support/` | Declared fixture data and test-only service stubs. |

Follow [Testing](../docs/TESTING.md) for commands and evidence boundaries.
Do not silently skip private-input tests and then report a whole-game pass.
New tests should exercise a real contract or regression; avoid duplicating
the implementation in a second function merely to assert equality.
