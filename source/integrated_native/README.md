# Integrated native adapters

These are project-owned integration inputs. The native provider is copied
to generated build output, then `tools/prepare_integrated_*.py` validate
known contracts and apply these adapters. Do not edit the immutable provider
or a closed candidate as the source of a fix.

- `rom_*`, `packed_*`, `boot_allocator.hpp`, `setup_power.hpp`: discovery,
  bounded preparation, companion access, boot allocation and lifecycle.
- `loading_*`, `rom_art.hpp`: boot presentation, locale and resource views.
- `render_60.hpp`, material/uniform/affine helpers: visual interpolation,
  GPU/PPU integration and cache contracts.
- `options/`: persistent settings, native UI, analog/camera input, console
  profiles, audio gain/storage helpers, level prefetch and diagnostics.
- `movement/`: actor/session ownership, DS collision/platform bridge and
  optional N64 solver adaptation.
- `native_diagnostics.hpp`: readable process/VRAM/display capture integration.

`options/build_version.hpp` is the current reserved BUILD counter. The
builder increments it before freezing sources. `options/atomic_file.hpp`
must remain safe before the retail heap exists: no default-allocator C++
containers in the boot settings path.

Ownership, workers and teardown are documented in
[Architecture](../../docs/ARCHITECTURE.md); detailed preparation invariants
are in [Development](../../docs/DEVELOPMENT.md). Each behavior change should
name and run its owning suite in [Testing](../../docs/TESTING.md).
