# Third-party notices

Terms apply to their respective components. No software license in this
repository grants rights to Nintendo game data or trademarks.

| Component | Notice / terms | Publication scope |
| --- | --- | --- |
| Native SM64DS provider | [MIT notice](../../third_party/sm64ds-3ds/LICENSE) | Immutable source snapshot, with local adaptation in generated copies. |
| NTR decoder source | [Tango MIT notice](../../source/vendor/ntr/LICENSE) | Decoder source and interfaces. |
| libsm64 | [CC0 text at the pin](https://github.com/libsm64/libsm64/blob/fd11813208272b4271d92bd92feb8f3fdbe61be5/LICENSE.md) | Intact public Git submodule; upstream-derived material retains its provenance. |
| SM64DS decompilation | [Pinned MIT notice](https://github.com/SebastienBreton/sm64ds-decomp/blob/d3022c1463a260ba67a3dc148aea01eb8de92ca3/LICENSE) | Fetched source; ROM/extracted game inputs are excluded. |
| sseqplayer | [WTFPL v2 text](../../third_party/sm64ds-3ds/port/third_party/sseqplayer/LICENSE.TXT) | Vendored upstream audio component. |
| SM64 3DS reference | [Pinned source](https://github.com/mkst/sm64-port/tree/3f3c01eae67d6e9f3af89bc8073e37a5d153e00f) | Fetched for checked function imports; no new blanket license is assigned to its source or game assets. |
| devkitPro / libctru / Citro3D | Their installed upstream notices | SDK dependencies, not copied into the public tree. |
| Python build/analysis packages | Their installed upstream notices | Package versions are declared by the build tools/guides; environments are excluded. |
| Qt / PySide6 | Upstream Qt/PySide terms, including applicable LGPL/GPL/commercial options | Source utility imports the installed package; frozen packages need their own dependency notices. |
| PyInstaller | Upstream GPL with bootloader exception | Local desktop packaging tool; no frozen application is published here. |

Approved interface resources, packaging graphics and the example emulator
capture depict Nintendo-owned artwork. The project includes their credit
context; they are not placed under MIT/CC0 by inclusion. The companion
converter's [credits](../../tools/sm64ds_packer/CREDITS.txt) distinguish its
included interface art from ROM-supplied gameplay data.

Review component obligations again before distributing a compiled package.
This source publication creates no binary release and bundles no ROM,
prepared gameplay archive, firmware, save or raw process dump.
