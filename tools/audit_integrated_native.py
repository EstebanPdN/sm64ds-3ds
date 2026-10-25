#!/usr/bin/env python3
"""Audit the integrated native backend before any emulator acquisition.

Checks every packed-script byte, all 61 typed pointer fields, linked feature
providers and the ARM ELF/3DSX pair. Cartridge payloads stay local. This does
not establish playable gameplay, audible audio or physical-console FPS.
"""
import hashlib
import json
from pathlib import Path
from devkitpro_tools import arm_tool, sdk_tool
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def audit(source, elf, image):
    from elftools.elf.elffile import ELFFile
    with elf.open('rb') as stream:
        binary = ELFFile(stream)
        assert binary['e_machine'] == 'EM_ARM' and binary.elfclass == 32
        symtab = binary.get_section_by_name('.symtab')
        sizes = {s.name:s['st_size'] for s in symtab.iter_symbols() if s.name}
        names = {s.name: s['st_value'] for s in symtab.iter_symbols()
                 if s.name and s['st_shndx'] != 'SHN_UNDEF'}
        segments = [(s['p_vaddr'], s.data()) for s in binary.iter_segments()
                    if s['p_type'] == 'PT_LOAD']
    def read(address, size):
        matches = [data[address-base:address-base+size] for base, data in segments
                   if base <= address and address+size <= base+len(data)]
        assert len(matches) == 1, (hex(address), size)
        return matches[0]
    decomp = source/'sm64ds-decomp'
    rom = (decomp/'extracted/arm9_dec.bin').read_bytes()
    start, end, base = 0x0208751c, 0x0208a0e0, 0x02004000
    native = names['data_0208751c']
    original = rom[start-base:end-base]
    normalized = bytearray(read(native, end-start))
    fields = [*range(start+4, 0x020876d4, 8), *range(0x020876e4, 0x020876fc, 4)]
    assert len(fields) == 61
    pointers = []
    for address in fields:
        target = struct.unpack_from('<I', original, address-start)[0]
        expected = names[f'data_{target:08x}']
        actual = struct.unpack_from('<I', normalized, address-start)[0]
        assert actual == expected, ('typed script pointer', hex(address), hex(actual), hex(expected))
        struct.pack_into('<I', normalized, address-start, target)
        pointers.append(dict(source=hex(address),original_target=hex(target),native_target=hex(actual)))
    assert bytes(normalized) == original, 'Packed script numeric/header bytes changed'
    # Check all 64 commands in the script whose six false pointer fixups were
    # observed in our previous RAM. Native bytewise initializer writes remain
    # owned by sm64ds_init_level_scripts, present in the linked program.
    at, commands = 0x02089af8, []
    while original[at-start]:
        size = original[at-start]
        assert size >= 6 and at+size < end
        opcode = original[at+1-start]
        begin, finish = struct.unpack_from('<2h', original, at+2-start)
        commands.append(dict(address=hex(at),bytes=size,opcode=opcode,begin=begin,end=finish))
        at += size
        assert len(commands) <= 1024
    assert len(commands) == 64 and at+1-0x02089af8 == 724
    sites = [0x02089ba4,0x02089bd0,0x02089c4c,0x02089cb0,0x02089cf0,0x02089d78]
    for address in sites:
        assert read(native+address-start,4) == rom[address-base:address-base+4]
    required = ['ProcessKuppaScript','sm64ds_init_level_scripts',
        'sm64ds_cstick_cam_step','sm64ds_cstick_cam_height','sm64ds_cstick_cam_orbit',
        'sm64ds_cstick_cam_direct','sm64ds_cstick_cam_follow','sm64ds_cstick_cam_offset',
        '_Z31sm64ds_native_sound_output_pumpv','_ZN6Player8BehaviorEv',
        '_ZN5Stage8BehaviorEv','_ZN6Camera8BehaviorEv']
    for name in required:
        assert names.get(name), 'Missing linked provider: '+name
    retired_camera_hud=None
    if (source/'render60-overlay.json').is_file():
        name='_ZN8dMeter_c19RenderCameraButtonsEv'
        # This object belongs to sm64ds_map, whose real compile definitions
        # omit SM64DS_PLATFORM_PC. Check linked ARM behavior, not the presence
        # of a preprocessor branch in a source file.
        assert sizes.get(name)==4 and read(names[name],4)==bytes.fromhex('1eff2fe1'), \
            'Retail camera HUD is still executable in the linked map provider'
        retired_camera_hud=dict(symbol=name,bytes=4,instruction='bx lr')
    # This backend uses 3dsxtool on its ordinary ELF directly. The legacy
    # experimental runtime's overlay-ID relocation filter does not apply.
    with tempfile.TemporaryDirectory(prefix='sm64ds-integrated-pair-') as temp:
        expected=Path(temp)/'expected.3dsx'
        subprocess.run([sdk_tool('3dsxtool'),str(elf),str(expected)],check=True)
        assert digest(expected)==digest(image), 'Integrated ELF/3DSX mismatch'
    pair=dict(status='PASS',scope='Independent direct 3dsxtool repack of this exact integrated ELF',
              ELF_sha256=digest(elf),image_sha256=digest(image),game_executed=False)
    return dict(status='passed_before_emulator',ELF_sha256=digest(elf),image_sha256=digest(image),
        ELF_3DSX_pair=pair,packed_script_bytes_compared=end-start,
        canonical_script_sha256=hashlib.sha256(original).hexdigest(),typed_pointers=pointers,
        intro_commands=commands,six_previously_corrupted_words_preserved=True,
        linked_feature_providers={n:hex(names[n]) for n in required},
        retired_camera_hud=retired_camera_hud,
        limitations=['Static/data/link proof only','No gameplay/audio/camera acceptance',
                     'No physical hardware or FPS evidence'])


if __name__ == '__main__':
    import argparse
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,required=True)
    parser.add_argument('--elf',type=Path,required=True)
    parser.add_argument('--image',type=Path,required=True)
    parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args()
    report=audit(args.source.resolve(),args.elf.resolve(),args.image.resolve())
    args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ['status','packed_script_bytes_compared','ELF_sha256']}))
