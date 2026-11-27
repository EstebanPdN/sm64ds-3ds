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
| devkitPro / libctru / Citro3D | Their installed upstream notices | SDK dependencies, installed separately. |
| Citro3D 1.7.1 sources | [zlib notice](../../third_party/citro3d-uniforms-1.7.1/LICENSE) | Four unmodified files used as host test oracles; `source/native_renderer/native_uniforms.cpp` adapts `uniforms.c` and keeps the notice. |
| LZ4 1.9.4 library | [BSD-2-Clause notice](../../third_party/lz4/LICENSE), reproduced [below](#lz4) | `lib/lz4.c` and `lib/lz4.h`, unmodified, from the v1.9.4 release archive (SHA-256 `0b0e3aa07c8c063ddf40b082bdf7e37a1562bda40a0ff5272957f3e987e0e54b`). Compiled into the game for packed developer dumps (SM64VMZ1); the repository's other LZ4 code is a pure-Python block decoder in `tools/vm_packed.py`. |
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

## LZ4

The LZ4 compressor linked into the game (`third_party/lz4/lz4.c`) is
distributed under these terms:

```text
LZ4 Library
Copyright (c) 2011-2020, Yann Collet
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice, this
  list of conditions and the following disclaimer in the documentation and/or
  other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```
