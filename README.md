<h1 align="center">Super Mario 64 DS 3DS</h1>

<p align="center">
  <strong>Return to the castle. Take both screens with you.</strong><br>
  A native Super Mario 64 DS port in development for Nintendo 3DS.
</p>

<p align="center">
  <a href="https://github.com/EstebanPdN/sm64ds-3ds/releases/tag/beta-01">Download beta-01</a> ·
  <a href="#source-and-installation">Install</a> ·
  <a href="#features">Features</a> ·
  <a href="#documentation">Documentation</a> ·
  <a href="CHANGELOG.md">Changelog</a> ·
  <a href="CONTRIBUTING.md">Contribute</a> ·
  <a href="#community-and-bug-reports">Community</a>
</p>

<p align="center">
  <a href="docs/COMPATIBILITY.md"><img src="docs/images/badges/status.svg" alt="Status: in development" height="22"></a>
  <a href="docs/COMPATIBILITY.md"><img src="docs/images/badges/platform.svg" alt="Target: Old and New Nintendo 3DS" height="22"></a>
  <a href="https://github.com/EstebanPdN/sm64ds-3ds/releases/tag/beta-01"><strong>BUILD 142</strong></a>
  <a href="LICENSE.md"><img src="docs/images/badges/license.svg" alt="Component-specific licenses" height="22"></a>
</p>

<p align="center">
  <a href="https://estebanpdn.github.io/"><img src="docs/images/website.svg" alt="Visit Esteban’s Forge" width="142" height="46"></a>
  <a href="https://discord.com/servers/esteban-s-forge-1533248895663210506"><img src="docs/images/discord-official.svg" alt="Join our Discord community" width="166" height="46"></a>
  <!-- <a href="https://ko-fi.com/estebanpdn"><img src="docs/images/kofi.svg" alt="Support EstebanPdN on Ko-fi" width="190" height="46"></a> -->
  <a href="https://www.patreon.com/c/EstebanPdN"><img src="docs/images/patreon.svg" alt="Support EstebanPdN on Patreon" width="216" height="46"></a>
</p>

A native Nintendo 3DS adaptation of **Super Mario 64 DS**, based on
[Sébastien Breton’s native port](https://github.com/SebastienBreton/sm64ds-3ds)
and [Tango’s decompilation](https://github.com/tangosdev/sm64ds-decomp), with
analog movement, a touch interface and configurable camera and presentation.

This project was developed with the help of [OpenAI Codex](https://openai.com/codex/). Learn more about the development process [here](AI_DEVELOPMENT.md).

[**Download beta-01**](https://github.com/EstebanPdN/sm64ds-3ds/releases/tag/beta-01). This first public beta uses
the native renderer from BUILD 142. It targets Old and New Nintendo 3DS
systems, with hardware compatibility and performance still under testing.

## Features

<table>
  <tr>
    <td width="50%" valign="top"><strong>Explore with analog controls</strong><br>Circle Pad movement, touch controls and a free C-stick camera on compatible systems. Configurable movement modes.</td>
    <td width="50%" valign="top"><strong>Made for two screens</strong><br>Native upper-screen 3D, stereoscopic presentation and dedicated lower-screen pause and options menus.</td>
  </tr>
  <tr>
    <td width="50%" valign="top"><strong>Prepare once, load directly</strong><br>Game data and renderer banks are prepared locally from your compatible European ROM and read directly from SD.</td>
    <td width="50%" valign="top"><strong>Keep settings and track problems</strong><br>Persistent options, audio controls, backup recovery and a visible BUILD number for reproducible bug reports.</td>
  </tr>
</table>

The original simulation keeps its 30 Hz clock. **Lock FPS** (30, 60 or
Unlocked; 30 by default on Old 3DS and 60 on New 3DS) chooses whether each
update also shows an interpolated image. This is not a claim of sustained
60 FPS on either console model; see [compatibility and evidence](docs/COMPATIBILITY.md)
and the [automatic test mode](docs/TEST_MODE.md) used to measure it.

## Source and installation

1. Prepare your own European Nintendo DS dump (**ASMP**, revision **0**) with
   the [beta-01 data preparation tool](https://github.com/EstebanPdN/sm64ds-3ds/blob/beta-01/tools/native_renderer/README.md).
2. Copy `prepared.sm64ds`, `native/` and `build142-data.bin` to
   `SD:/3ds/sm64ds/`. Keep your saves and settings in place.
3. Install the CIA with FBI or put the 3DSX in `SD:/3ds/sm64ds/` for
   Homebrew Launcher. Both formats use the same SD data.
4. Touch the **star** to continue to the original title. The BUILD number
   appears at the lower left.

[![FBI installation QR for the beta-01 CIA](https://github.com/EstebanPdN/sm64ds-3ds/releases/download/beta-01/QR-beta-01-github.png)](https://github.com/EstebanPdN/sm64ds-3ds/releases/tag/beta-01)

Use `SHA256SUMS.txt` from the release to verify the downloads. The automated
hardware tour is disabled in the beta. Keep ROMs, prepared data, firmware,
saves and dumps private.

The [build guide](docs/BUILDING.md) describes the public source reference and
its development build. See [SD data and recovery](docs/SD-DATA.md) before
replacing a previous installation.

## Controls

| Input | Current use |
| --- | --- |
| Circle Pad | Analog movement; configurable sensitivity and run threshold. |
| A / B / X / Y, L / R | Game actions according to the selected controller and movement mode. |
| C-stick | Configurable camera on compatible New systems. |
| START | Open the in-game pause menu. |
| Touch screen | Original interactions, pause, controller and options menus. |
| 3D slider | Stereoscopic presentation where supported by the display path. |

Developer capture shortcuts and their disk-space implications are described
in [diagnostics](docs/DIAGNOSTICS.md).

## Documentation

| Guide | Purpose |
| --- | --- |
| [Documentation index](docs/README.md) | Current guides and historical investigations. |
| [Compatibility](docs/COMPATIBILITY.md) | Supported input, model profiles and validation limits. |
| [SD data](docs/SD-DATA.md) | ROM, companion, saves, settings and recovery. |
| [Building](docs/BUILDING.md) | Source pins, SDK, private inputs and packaging. |
| [Architecture](docs/ARCHITECTURE.md) | Startup, ownership, adapters, workers, caches and the native renderer. |
| [Testing](docs/TESTING.md) | ROM-free checks, ARM gates and emulator/hardware evidence. |
| [Test mode](docs/TEST_MODE.md) | The automatic SELECT+START level tour and its reports. |
| [Repository map](docs/REPOSITORY.md) | File placement and maintenance conventions. |
| [Source provenance](docs/UPSTREAM.md) | Dependencies, immutable pins and notices. |
| [Roadmap](docs/ROADMAP.md) | Remaining acceptance work. |

## Development

Requirements for portable checks: Git, Python 3.11+, CMake 3.20+ and a C++17
compiler. Clone with the public motion-library submodule:

<details>
<summary><strong>Clone and run the portable checks</strong></summary>

```sh
git clone --recurse-submodules https://github.com/EstebanPdN/sm64ds-3ds.git
cd sm64ds-3ds
python3 scripts/check-repository.py
cmake -S . -B build/host -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host --parallel 4
ctest --test-dir build/host --output-on-failure
```

</details>

The complete game additionally requires devkitPro/devkitARM, libctru,
Citro3D, the pinned Python packages and the local inputs listed in
[Building](docs/BUILDING.md). A clean source checkout does not contain
those game inputs. [Contributing](CONTRIBUTING.md) explains the checks and
compatibility contracts to preserve.

## Community and bug reports

[Join Discord](https://discord.com/servers/esteban-s-forge-1533248895663210506) for discussion and testing.
Use [GitHub issues](https://github.com/EstebanPdN/sm64ds-3ds/issues) for
reproducible bugs, including the visible BUILD number, console model and
steps to reproduce. Review logs and captures before sharing them; full
process dumps and game data must remain private.

## Credits

- Sébastien Breton and SM64DS 3DS contributors — the integrated native backend.
- Tango and sm64ds-decomp contributors — reconstruction, data formats and decoders.
- libsm64 and mkst/sm64-port contributors — the optional movement solver and reference logic.
- devkitPro, libctru and Citro3D contributors — Nintendo 3DS development tools and libraries.
- Nintendo — Super Mario 64 DS and original game content.
- EstebanPdN — adaptation, interface, diagnostics and maintenance, with help from Codex.

See the [full credits](docs/legal/CREDITS.md) and
[third-party notices](docs/legal/THIRD_PARTY_NOTICES.md).

## License and legal notice

Licenses are component-specific. The native provider and NTR decoders retain
their MIT notices; libsm64 retains its CC0 text. Other dependencies retain
their own terms. [LICENSE.md](LICENSE.md) explains the scope; no blanket
license is assigned to Nintendo content or to every file in this repository.

This is an unofficial fan project, unaffiliated with Nintendo. Screenshots
and approved interface/packaging artwork depict Nintendo-owned content and
do not grant rights to the game or its trademarks.

---

<p align="center">
  <a href="https://estebanpdn.github.io/">Esteban’s Forge</a> ·
  <a href="https://discord.com/servers/esteban-s-forge-1533248895663210506">Discord</a> ·
  <a href="docs/README.md">Documentation</a> ·
  <a href="docs/legal/CREDITS.md">Credits</a> ·
  <a href="docs/legal/THIRD_PARTY_NOTICES.md">Third-party notices</a><br>
  <sub>Super Mario 64 DS belongs to Nintendo. This is an independent fan project.</sub>
</p>
