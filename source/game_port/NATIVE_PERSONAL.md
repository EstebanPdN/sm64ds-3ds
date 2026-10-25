# Native personal settings

`native_personal.cpp` replaces original `func_02059eb0`, whose direct read of
`0x027ffce4` stopped DSMT initialization in attempt
`1791594468176316000`. The provider reads actual 3DS settings through libctru
CFGU and caches the converted SDK record. It never maps a DS firmware image,
reads a DS bus address, or logs a nickname, birthday or comment.

Include `sm64ds/native_personal.hpp` and call
`bool sm64ds::native_personal_initialize()` after `native_platform_init` and
before the original boot. Check its return value. It obtains one service
reference, reads language/name/birthday, and calls `cfguExit` after any outcome
following successful initialization. Repeated initialization calls reuse the
original outcome, including errors. Changes to System Settings take effect on
the next application boot. There is no fallback profile when CFGU fails.

The dependencies are `cfguInit`, `cfguExit`, `CFGU_GetSystemLanguage`,
`CFGU_GetConfigInfoBlk2`, `native_platform_fail`, and the existing fatal
`sm64ds_native_missing` boundary for invalid calls of the original void ABI.
The namespace copy API returns failure without touching the destination.
The original ABI terminates explicitly if export fails, so an uninitialized
stack record cannot reach the menu's language switch.

The CFGU transaction reads the full 28-byte name block `0x000a0000` and the
2-byte birthday block `0x000a0001`. Language uses libctru's public helper;
its implementation reads one byte from `0x000a0002`. Their formats/access
flags are documented by [3dbrew's original CFG research](https://www.3dbrew.org/wiki/Config_Savegame).
The actual user-service request, result handling and service reference
management are in [libctru's CFGU source](https://github.com/devkitPro/libctru/blob/v2.6.2/libctru/source/services/cfgu.c).
The provider uses the installed libctru declarations rather than redeclaring
the service functions.

The ROM export writes every byte of exactly 80 bytes. The three observed
callers reserve 80 or 84 bytes; the extra four bytes remain unchanged.

| Offset | Bytes | SDK record | Native source/policy |
| --- | ---: | --- | --- |
| `0x00` | 1 | NTR language | CFGU with EU resource mapping |
| `0x01` | 1 | DS favourite colour | Explicit default `0` (grey) |
| `0x02` | 1 | Birthday month | CFGU birthday byte 0 |
| `0x03` | 1 | Birthday day | CFGU birthday byte 1 |
| `0x04` | 20 | Ten UTF-16 nickname units | First 20 bytes of CFGU name block |
| `0x18` | 2 | Nickname length | Bounded count, little endian, `0..10` |
| `0x1a` | 52 | Twenty-six UTF-16 comment units | Empty, all zero |
| `0x4e` | 2 | Comment length | Zero |

Only the first ten UTF-16 units are copied. A terminator in those units sets
the length; a full ten-unit name is valid without a terminator. Bytes after a
terminator are retained from those original twenty bytes, matching the SDK's
fixed-length copy. The additional CFGU unit, NGWord flag and version never
enter the record. Surrogate pairs must be complete within the bounded name;
unpaired high/low surrogates fail explicitly. The length counts UTF-16 units,
not Unicode scalar values. This does not assert that the original DS font can
render every valid 3DS character. A calendar-valid month/day is required;
February 29 is allowed because the exported record has no year.

The DS fields have the meanings recorded in
[GBATEK's firmware settings research](https://problemkaputt.de/gbatek.htm#dsfirmwareusersettings),
and the offsets/80-byte store footprint were checked against this project's
supplied EU ROM, CRC32 `29715dec`, and source pin
`6230b2996636e3fd0e03cb178161c94cb4b73cc9`. The compatibility colour/comment
values are declared application policy for fields not provided by these CFGU
reads. They are not presented as the console's Nintendo DS Profile settings.

The supplied EU game has English, French, German, Italian and Spanish resources.
CTR IDs `1..5` map directly to their corresponding NTR language IDs. Japanese
(`0`), Simplified Chinese (`6`), Korean (`7`), Dutch (`8`), Portuguese (`9`),
Russian (`10`) and Traditional Chinese (`11`) explicitly map to English (`1`).
This avoids truncating a CTR ID to the DS firmware's three-bit field. Values
outside `0..11` fail. The original menu also selects English for NTR `0` and
`1`; the provider records the effective language as English consistently for
other consumers. This mapping is scoped to the supplied EU ROM, not a claim
that other revisions have the same resources.

`func_ov007_020b6a84` uses only `buf[0]` to set its language selector: English
`1`, French `2`, German `3`, Italian `5`, Spanish `4`. Original callers
`func_0201fec8` and `func_0203db64` also consume this SDK record; the latter's
wireless setup reads colour/name/length. Providing its data record does not
make the native wireless backend available.

Run `/tmp/sm64ds-primitives-tests/bin/python tools/test_native_personal.py`.
The recorded result is `build/native-personal/report.json`:

- 79 host fixtures passed ASan/UBSan, including reference management, caching,
  positive nonzero successful Result codes, malformed settings and bounds.
- 201 valid synthetic profiles ran on the ARM11 CPU harness. Their five
  destination offsets produced 1005 exact comparisons with the actual ROM
  export, including output canaries and each complete 80-byte store footprint.
- 132 original menu tails executed from `0x020b6b78` through their real return,
  using the native export. The real jump table selected all five resources;
  CPU read tracing confirmed the tail consumed only offset 0, size 1.
- 272 explicit failure cases covered service failures, every invalid language
  byte `12..255`, bad birthdays/UTF-16, use before init, invalid capacities,
  alignment, DS addresses and address overflow. Destination bytes stayed intact.

The native CPU receives no DS firmware or MMIO mapping. A separate ROM oracle
gets synthetic firmware solely to verify the original export's contract.
These are host and bounded CPU/ABI proofs. Real console CFGU reads, complete
menu execution, gameplay, physical Old/New 3DS and FPS remain unverified by
this suite. It runs no Azahar cycle and reads no real personal data.
