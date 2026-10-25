# Changelog

All entries below are **unreleased**. BUILD numbers identify local test
candidates, not GitHub releases or a promise of full-game compatibility.

## Unreleased — source publication

- Document the integrated game as the main development path, with separate
  maps for the historical runtime and model viewer.
- Add current build, architecture, testing, SD-data, compatibility and
  diagnostic guides, preserving dated engineering records.
- Publish the pinned native source and motion-library submodule, with
  provenance and component-specific notices.
- Allow explicit motion-source and animation paths, removing the requirement
  for the maintainer's sibling MK64 project folder.
- Add repository checks, ROM-free CI, issue forms and contribution guidance.
- Keep private game inputs, dumps, generated packages and design iterations
  outside the public Git tree. No binary release is created.

## Local BUILD 24 — 2026-10-25

- Fix a first-boot crash when absent settings triggered C++ allocation before
  the original game heap was initialized. Backup and temporary paths now use
  bounded stack storage.
- Add an increasing BUILD label to title, pause, options and diagnostics, and
  carry it into build/package receipts. Failed reservations leave gaps.
- Add allocation-forbidden first-boot regression coverage. Host/ARM build and
  audit gates pass; directed Azahar Old/New checks cover first boot, level,
  pause and settings recovery. Physical qualification remains pending.

## Earlier local candidates — October 2026

Native-resolution/stereo fixes, direct companion access, level preloading,
console-specific budgets, interface layout, camera/movement corrections,
audio gain ramps, backup-first storage and diagnostic capture evolved across
local candidates. Exact artifact scope and limitations are preserved in
[STATUS.md](docs/STATUS.md), [the documentation index](docs/README.md) and
the dated investigation records. Those records are not public releases.
