# SM64DS Packer

A small desktop window accepts a Nintendo DS ROM by drag-and-drop or the
**CHOOSE ROM** action. The window reproduces the original DS bottom loading
screen at 320x240 logical pixels, displayed at a crisp 2x size, with original
pattern, star, frame and fonts. Click the orange **CHOOSE ROM** button or press Enter to choose a ROM.
The header is compact, the status and progress frames share the same width, and
the footer has a dedicated accessible button with a clear bottom margin.
The icon uses the original star. Choose where to save the resulting `.sm64ds` file, then
copy that single file into `SD:/3ds/sm64ds/`. Any basename is accepted. This
archive is an alternative to supplying a `.nds` ROM; it is not a CIA and is not
installed through FBI.

The converter currently accepts the verified original European ASMP revision 0
ROM. Renaming a different ROM does not make it compatible. Conversion is local:
there are no uploads, telemetry or downloaded game assets. The distributed utility contains extraction offsets, checksums and generated
filesystem catalogs, plus the approved original loading-screen interface graphics.
The playable game data in the output archive comes from the ROM supplied by the
user. Original interface graphics are credited to Nintendo.

The conversion runs in a background worker so the window stays responsive.
Click **CANCEL**, press Escape or close the window to request safe cancellation.
Progress spans ROM checking, writing and readback verification without resetting
between phases. The desktop says **Preparing your game...** while working,
and the last 9 percent says **Almost there!**. All hint text is centered using
the actual opaque glyph bounds and has a clear gap above the progress bar. It checks the full ROM
SHA-1, SHA-256 and CRC32 before creating an output, then streams data in 64 KiB
chunks. Each output member is checked while writing and again by reading back the
temporary archive. On success a sibling temporary file is atomically renamed to
the chosen output. Errors/cancellation remove only that temporary file and keep
an existing output. Never unplug an SD card while conversion is running.

## Run from source

Python 3.11 or later is recommended. The graphical interface uses PySide6 and
runs on macOS, Windows and Linux (desktop PC). The supplied native Mac build is
for Apple Silicon with macOS 14 or later. Windows and Linux use the source
package launchers unless a native build for those platforms is supplied.

```sh
python -m venv .venv
# Activate the environment using your operating system's normal command.
python -m pip install -r requirements.txt
python app.py
```

Run those commands inside `tools/sm64ds_packer`. In the source ZIP, Windows
users can instead double-click **Start Windows.cmd**; Linux users can run
`sh "Start Linux.sh"`. These launchers create a private virtual environment
beside the source and install the pinned GUI dependencies on first use. Python
and network access for dependency installation are required. For scripting, the converter core
needs only Python's standard library:

```sh
python app.py --rom "My Mario.nds" --output "My Mario.sm64ds"
```

The CLI refuses to replace an existing output. The GUI asks before replacement.
The input ROM is opened read-only and may never be used as the destination.

## Native packaging

Build separately **on each operating system**; an app built on macOS is not a
Windows executable. Install `requirements.txt`, then run:

```sh
pyinstaller --noconfirm "SM64DS Packer.spec"
```

The macOS build creates `dist/SM64DS Packer.app`; Windows creates
`dist/SM64DS Packer/SM64DS Packer.exe`, and Linux creates the corresponding native
executable directory. Distribute the entire platform output folder, not just the
executable. The source and recipe metadata can also be distributed for all three
platforms. Windows and Linux builds require their own build/test hosts.

## Archive format v1

The extension is `.sm64ds`. The archive is uncompressed, making bounded random
reads possible directly on the 3DS without a second extraction step or thousands
of SD files.

The fixed 128-byte little-endian header uses Python struct
`<8sIIQ16s64s24s`: magic `SM64DSP1`, version 1, file count 2450, payload bytes
15686745, schema ID `1f256505ddfbac65`, metadata SHA-256 as 64 ASCII hexadecimal
characters, and 24 reserved zero bytes. The payload concatenates the canonical
manifest's files in exact manifest order with no per-file padding, paths or
compression. Total archive size is **15,686,873 bytes**. The 3DS reader knows the
index from its matching manifest and validates every member before mounting it.
