# Tools

Run maintained entry points from the repository root. Their relative paths
are stable interfaces used by CMake and other Python tools; the many audit
and inspection helpers are grouped below by naming convention.

| Entry point / family | Responsibility |
| --- | --- |
| `setup_dependencies.py` | Prepare/verify fixed public source checkouts without game downloads. |
| `build_integrated_native.py` | Current integrated candidate: gates, ARM build, audits, source freeze and BUILD reservation. |
| `prepare_integrated_*.py` | Contract-checked transformations of generated provider copies. |
| `package_integrated_cia.py`, `verify_integrated_cia.py` | Local package creation and executable/exheader verification. |
| `verify_native_3dsx.py`, `audit_integrated_native.py` | ELF/3DSX identity, link and native closure checks. |
| `run_integrated_native.py`, `run_integrated_campaign.py` | Isolated local emulator acquisition and scenario campaigns. |
| `analyze_integrated_*.py`, `inspect_integrated_*.py` | Private trace, RAM, render and performance analysis. |
| `inspect_luma_dump.py`, `read_mapped_capture.py` | Validated local crash/process-memory readers. |
| `generate_integrated_*.py`, UI generators | Local catalogs, checked ROM resource recipes and approved interface headers. |
| `test_integrated_*.py`, `gate_integrated_render60.py` | Synthetic/resource gates used by the builder. |
| `movement_lab/` | Motion source adaptation, solver build, lab and contact analysis. |
| `sm64ds_packer/` | Desktop/CLI companion converter; see [Packer guide](../docs/SM64DS_PACKER.md). |
| `native_core.py`, `link_native.py`, `build_native_runtime.py` | Historical runtime/ABI build path. |
| `audit_native_*`, `test_native_*`, `read_native_*`, `replay_native_*` | Earlier and shared native service/renderer investigations; inspect each runner's prerequisites. |
| `asset_tool.cpp`, `*_probe.cpp` | Host parser/resource probes built by CMake or dedicated runners. |
| `deliver_integrated_local.py` | Explicit local-network delivery of a verified package; never an automatic source-publication step. |

Use `--help` where provided and read a runner before invoking it. Some legacy
scripts require private ROM extractions or exact historical build artifacts;
they are not all standalone portable tests. Outputs belong under ignored
`build/` or `dist/`. No tool should upload local game inputs or diagnostics.
