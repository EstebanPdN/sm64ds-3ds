# Native options model and CPU artwork checks

The test is independent of libctru and never starts Azahar. It checks navigation,
changes to persisted settings, disabled capture actions, one-shot capture/delete
requests, confirmation defaulting to Keep, Update remaining inert, touch Back,
close/save flags, generation changes, and guard words around every 320x240 image.
The stress loop changes selections/values 2,000 times under ASan/UBSan.

Generate original diagrams before compiling:

```sh
.venv/integrated-native/bin/python tools/generate_integrated_options_art.py --extracted third_party/sm64ds-3ds/sm64ds-decomp/extracted --output build/integrated-native/options-art/options_art.hpp
clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Isource/integrated_native/options -Isource/integrated_native -Ibuild/integrated-native/rom-bootstrap -Ibuild/integrated-native/options-art source/integrated_native/options/enhance_overlay.cpp source/integrated_native/options/enhance_settings.cpp tests/options_menu/test_menu.cpp -o /tmp/sm64ds-options-test
```

Run in a private build directory; the test writes reproducible PPM previews.
Generated `options_art.hpp` contains Nintendo's original three control-mode
illustrations, reconstructed from tile graphics, palette banks and NSC tile maps.
It is generated locally, not committed as a large array. The adjacent JSON records
source asset hashes and output hash. Original artwork and fonts come from the
same pinned extracted ROM assets as the approved loading screen.

`test_hooks.py` separately validates disposable-tree Stage hooks and executes the
exact injected state assignments/callbacks under ASan/UBSan for normal and VS
parents and all original selection values. It verifies that duplicate application,
vendor targets, symlinks into vendor and changed source anchors are rejected;
failed source-contract validation leaves the copy unchanged. Run with:

```sh
.venv/integrated-native/bin/python tests/options_menu/test_hooks.py
```

The generated hooks cover original Stage pause Controller/Options entries and
the retail title's existing options element (ID 7). The title uses its original
per-element hit test; no new screen coordinates are introduced. Its selection
callback opens the native menu before starting the old options transition.
`test_title_hooks.py` executes this guard for all 24 original element IDs and
checks the original ID 7 -> substate 8 -> sound/backlight source contract.
`PS_Init` also has a direct Controller entry; its parent background initialization
must be assessed separately when that entry route is integrated.

## Real Stage pause extension

The current integration is `native_pause_menu.{h,cpp}`, `native_pause_art.hpp`
and `tools/prepare_integrated_native_pause.py`. The lower UI is a transparent
320x240 bitmap with pixel-authored frames and1:1 glyphs. It does not write its
new labels/panels into original character VRAM. A local PPU DISPCNT view removes
only old lower BG0/BG1/OBJ while retaining BG2/BG3 map and real register bytes.
Upper original Controller diagrams remain owned by the game. SettingsOverlay
is an offline legacy model, not an alternative in-game screen.

`test_native_pause.cpp` exercises the current Controller/Buttons hierarchy,
Standard/Touch/Dual-hand selection, independent sensitivities, staged preview
and rollback, nested OK and Back, Audio, Display, Developer actions, deletion
confirmation, Update placeholder, touch navigation and bounded encoding under
ASan/UBSan. `test_native_pause_art.cpp` verifies native drawing bounds, alpha
map gaps and stable field font. `test_integrated_native_ui.py` additionally
compares actual captured PPU pixels and cache toggles against the original
PPU with explicitly masked private fixture registers.
`test_native_pause_hooks.py` validates pristine-copy patching, native 7/9 setup
before native painting, original 8/A bodies retained under the non-3DS branch,
actual parent capture/restoration, retained original Controller illustration
calls, duplicate/vendor/symlink rejection and all-or-nothing contract checking.
Its retained historical DS-path check measures labels against the original font's
advance table, including Circle Pad Sensitivity: 105 and : 200 (131 px, inside
144 px of cream frame). The old `test_hooks.py`/`test_title_hooks.py` target the
superseded independent overlay and should not gate this native integration.

The runtime drains `sm64ds_native_pause_take_requests()` only at a safe frame
boundary: bit 1 saves Settings; bit 2 captures full RAM; bit 4 deletes owned
dumps. Sound is committed through the original SetSoundMode, Controller through
SetControllerMode. B/Back returns to the category or cancels the session; OK
accepts settings. A direct Controller shortcut closes through the existing
Stage::Behavior -> PS_Cleanup lifecycle. Normal pause entry records its actual
parent state (1 castle / 3 course / 5 VS), rebuilds its original entry (0/2/4),
and restores the original selected row after setup. data_0209f218 is not a VS
flag.

No ROM or packed-asset manifest is modified. The root pause Controller label is
repainted only in the original two text-map rows by callbacks appended to the
original Message::Display (castle y80) / DisplayPauseText (course y72). New glyph
tiles append after the original message allocation; Continue/Options/Quit maps
and glyph tiles remain intact. VS has no Controller entry in the original case5
menu, so its labels remain unchanged.

Audio is a category with no inline value. Its Sound mode row cycles Surround,
Headphones and Stereo. Custom submenu rows consistently use the original 8px
font; the native OK button and root pause CONTROLLER label retain 16px graphics.
The capture action is labelled Dump RAM. Audio keyboard/touch navigation and
Back/rollback are covered by the native model gate.

Volume is the second Audio row (0..200%, default 100 at the center), with 5%
keyboard/side-button steps and held touch dragging. The Python-generated slider
uses original DS palette bank 1 without changing any global colors. Its 36 tiles
occupy only BG0 0x300..0x323; glyphs remain in 0x200..0x2ff. The host gate executes
the actual 4bpp tile/map writer at 0/100/200 and an out-of-range value, checking
every untouched byte/map cell and rejecting short buffers without mutation.
A private BG0-only 0x4f glyph supplies the authored percent sign; it is never
passed to the original BMG engine. The generator is deterministic and owns all
new sprite pixels; previews use the existing menu palette for inspection.

The temporary tile ranges are disjoint from OUR dynamic glyphs, but are NOT
guaranteed globally free retail character slots. Native entry therefore saves
the actual 64KiB character bank and both original 1024-entry maps once, before
original Controller/Options setup via the recorded parent transition. The
direct Controller shortcut saves its original prepared state before native
painting. The exact pointer owners are restored on accept/cancel before
original parent reinitialization or PS_Cleanup; later CONTROLLER label repaint
is never rolled back. Tests cover Audio/Controller/Update, both exit choices,
repeated capture preserving the first snapshot, aliased character/map owners,
invalid sizes and exact restoration of every original byte/map entry. This
uses a static 68KiB snapshot, with no per-frame snapshot or heap allocation.

Volume now has a 192x56 panel at x32/y72, its label at y80, and the slider at
y104..119, providing eight pixels of padding. Its expanded hit row is y72..127.
Update has the same cream panel at x32/y56 behind its text at y64/88.

2026-10-21 host regression additions
----------------------------------

Run `python3 tests/options_menu/test_prepare_contracts.py` for the actual camera,
display, pause, title and native UI preparers. It checks two independently
prepared pristine copies for identical results, duplicate rejection, late-source
contract failures without partial writes, vendor/symlink refusal, and contract
checks under optimized Python. New C++ harnesses execute the native touch camera
branch over all 256x192 coordinates in all three control modes, the injected
pause hold/invalidation branch, and the actual PPU's DISPCNT/cache-key code over
both physical display routes, readiness states and all 65,536 low register masks.
These harnesses use ASan/UBSan. The current native title pump test now includes
opening and closing before a second blocked sample, under ASan/UBSan.

The PPU regression is significant for title ownership: engine B is upper at the
title, so hiding its BG0/BG1/OBJ as if it were always lower removes upper art.
The native pause mask now applies only when POWCNT1 routes engine B below A.
The captured-pixel replay covers both routes too, but needs its historical
private fixtures; missing fixtures are not a successful captured-pixel gate.

The model additionally rejects overflowing panel coordinates and avoids repaint
requests for stationary/saturated Volume gestures. This is reduced work, with
no FPS claim. Pause uses one worker decision for layout and raster hold, and
invalidates the held target before any new resident draw that could fail.

The approved category painter is preserved. These host/source/ARM checks do not
replace a live screenshot, hardware acceptance or a full linked candidate.
The original HUD plus two original 32x32 camera buttons and their touch regions
remains pending; the rejected replacement atlas stays disconnected. Upper
sprites in Wide and immediate Display application during pause remain separate
acceptance items; the current UI explicitly defers Display changes until resume.
