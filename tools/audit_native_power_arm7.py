#!/usr/bin/env python3
"""Read-only identification of the original shared VBlank counter writer.

Only hashes, addresses and a decoded four-instruction contract are exported.
The original ARM7 payload stays in the private cartridge and is not executed.
"""
import hashlib
import json
import struct
from pathlib import Path

from test_native_menu_callbacks import Cartridge

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/native-stage-plan/power/arm7-vblank-writer.json'


def main():
    cartridge = Cartridge(ROOT / 'build/europe.pak')
    header = cartridge.file('__rom/header.bin')
    payload = cartridge.file('__rom/arm7.bin')
    entry, base, declared_size = struct.unpack_from('<3I', header, 0x34)
    assert len(payload) == declared_size

    def word(address):
        offset = address - base
        assert 0 <= offset <= len(payload) - 4
        return struct.unpack_from('<I', payload, offset)[0]

    # The literal and its consuming instruction are checked together.
    instructions = [
        (0x02380724, 0xe59f1038, 'load r1 from literal at 0x02380764'),
        (0x02380728, 0xe5910000, 'load current u32 counter into r0'),
        (0x0238072c, 0xe2800001, 'add 1 to r0 with u32 wrap'),
        (0x02380730, 0xe5810000, 'store r0 back to the counter'),
    ]
    for address, expected, _ in instructions:
        assert word(address) == expected
    literal = 0x02380724 + 8 + (word(0x02380724) & 0xfff)
    assert literal == 0x02380764 and word(literal) == 0x027ffc3c
    report = dict(
        status='pass', scope=__doc__, payload_CRC_checked=True,
        ARM7_entry=hex(entry), ARM7_base=hex(base), ARM7_size=declared_size,
        header_sha256=hashlib.sha256(header).hexdigest(),
        ARM7_sha256=hashlib.sha256(payload).hexdigest(),
        cartridge_sha256=hashlib.sha256(cartridge.raw).hexdigest(),
        counter_address='0x027ffc3c', literal_address=hex(literal),
        writer=[dict(address=hex(address), operation=operation)
                for address, _, operation in instructions],
        ARM7_executed=False, raw_payload_exported=False,
        production_changed=False, UI_launched=False,
        limit='Static writer identity; no claim that native GSP/APT is DS ARM7 execution.')
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(dict(status='pass', report=str(OUT), ARM7_sha256=report['ARM7_sha256'])))


if __name__ == '__main__':
    main()
