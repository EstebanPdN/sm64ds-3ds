# Packaging identity

`sm64ds-integrated.rsf` defines the current integrated game's title identity,
services, memory/core requests and stack. Its Title ID is
`000400000F536400`. `sm64ds.rsf` belongs to the historical viewer and must
not be substituted into an integrated package.

Use `tools/package_integrated_cia.py` with a closed candidate. The packager
verifies the executable against its ELF and includes notices/instructions.
It uses the approved icon and 3D logo; it never publishes a GitHub release.
Keep package outputs under ignored `dist/`. See [Building](../docs/BUILDING.md).
