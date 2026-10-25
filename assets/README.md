# Asset scope

This tree contains approved interface and local package resources. Original
Nintendo artwork retains its rights holder and is not licensed as project
code. No ROM, prepared gameplay archive, firmware or process-memory capture
belongs here.

- `icons/mario-yellow-approved.png`: approved CIA/3DSX application icon.
- `banners/approved-logo-v2/`: approved modeled logo, editable scene/tools,
  native runtime export and package preview. The owner's raw tracing
  reference and duplicate/abandoned revisions remain local.
- `ui/`: generated/native interface sprites, font atlases and their metadata.
  The original font silhouettes and approved retail UI retain Nintendo
  attribution. Some regeneration tools additionally need private extracted
  ROM files or original reference sheets that are not in the public tree.
- `proposals/lower-hud-2026-10-18/`: only the two approved camera-arrow PNGs,
  layout metadata, generator source and comparison-map fixture required by
  the existing exact-pixel camera gate are retained. Other proposals and
  temporary renders remain ignored. `generate.py --camera-header OUTPUT`
  uses these checked sprites; its historical proposal-rendering branch
  needs the original local sheets/extraction and is not a clean-build step.

The desktop Packer's included interface resources and their credit context
live under `tools/sm64ds_packer/`. Game payloads are always derived locally
from the user's ROM. See [credits](../docs/legal/CREDITS.md),
[notices](../docs/legal/THIRD_PARTY_NOTICES.md) and
[README imagery](../docs/images/README.md).
