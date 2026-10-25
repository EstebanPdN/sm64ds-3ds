# Compatibility and current status

**In development; no public binary release.** The current runtime baseline
is local BUILD 24. Source publication adds organization, documentation and
portable dependency selection; it does not create another tested game build.

## Inputs and launch formats

| Area | Current scope |
| --- | --- |
| Game ROM | European ASMP revision 0, 16,777,216 bytes, SHA-1 `1367529f2cb23e76ef295cb1727333ae8f0a6cd7`. |
| Prepared companion | Matching `.sm64ds` v1 schema; any basename in `SD:/3ds/sm64ds/`. |
| CIA | Integrated native title `000400000F536400`; distinct from the historical viewer. |
| 3DSX | Homebrew Launcher path, with budgets limited by available application memory. |
| Controls | Original actions, Circle Pad analog input, touch UI and configurable movement/camera. |
| C-stick | Compatible New systems; Old systems use available original controls. |
| Presentation | Native upper-screen rendering, alternate display modes and eligible stereo/interpolation. |

Other ROM regions/revisions and modified ROMs are not accepted by the
integrated preparation contract. The old model viewer has a different USA
data path; its documentation is not the current game's input specification.

## Console profiles

The CIA retains an Old 80 MiB profile at 268 MHz. New configurations can
request 124 MiB, 804 MHz, L2 and core 2, with larger GPU/texture/preload
budgets. A New system launched in a smaller memory mode keeps the smaller
budgets, and unavailable core access falls back. See `console_profile.*`
and the exheader template for the implemented policy.

These are requested application profiles, not physical measurements of
available system memory or performance. Old and New memory-mode boot/level
checks were performed in Azahar; physical installation and behavior remain
to be qualified on identified devices.

## Evidence boundary

| Check | Current conclusion |
| --- | --- |
| Host regressions and ARM/build audits | Recorded for BUILD 24; source publication runs the portable subset again. |
| Missing-settings crash | Causally identified and fixed; previous helper fails the new first-boot regression. |
| Azahar Old/New | First boot, directed level, pause and visible build checked; save/backup recovery checked in isolated profiles. |
| Original title flow | Old introduction → title/options → file selector observed with real touch input. |
| Whole-game progression | Incomplete coverage. |
| Physical Old/New | Pending qualification. |
| Sustained FPS / audible quality / power-loss safety | No general hardware claim. |

The optional 60 Hz render path preserves the 30 Hz game clock. It does not
guarantee 60 displayed frames in all scenes. Hybrid/N64 movement is still
under development, especially complex abilities, interactions and scripts.
The [roadmap](ROADMAP.md) and [status journal](STATUS.md) retain open work.
