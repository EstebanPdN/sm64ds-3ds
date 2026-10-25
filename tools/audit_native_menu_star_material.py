#!/usr/bin/env python3
"""Trace the preserved first-menu TEXGEN2 binding to its original BMD material.

Verifies the original material, complete display lists and captured live upload
bytes. A bounded ROM/ELF CPU comparison then runs model relocation, uploads,
material preparation, binding and one display-list producer. Controlled upload
cursors and identity transforms isolate these contracts from prior allocations
and animation. GX/SDK service boundaries are observers. No emulator is run and
no game frame, physical console or performance result follows from this audit.
Cartridge bytes stay in memory; the report contains offsets, metadata and hashes.
"""
import argparse
import json
from pathlib import Path
import re
import struct
import subprocess

import ndspy.lz10
import test_native_menu_constructors as constructors
from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_PC, UC_ARM_REG_LR

base, ROOT = constructors.base, constructors.ROOT
PIN = '6230b2996636e3fd0e03cb178161c94cb4b73cc9'
ASSET = 'data/DSMT/face_demo_mariostar.bmd'
PACKED_SHA = '603e26695f9bd8a637c31bce5658b67b18a8311639fc4e3f4c6a5e1dd56b8f45'
MODEL_SHA = '628ae2a8880b27d68471edf93463f5627bb2f73ce42086033acd6af26faf34f7'
MODEL, COMPONENTS, BUFFER, VIEW = 0x12300000, 0x12500000, 0x12510000, 0x12520000
IDENTITY43 = struct.pack('<12i', 4096, 0, 0, 0, 4096, 0, 0, 0, 4096, 0, 0, 0)
TRANSITIONS = ['_ZN2GX12BeginLoadTexEv', '_ZN2GX10EndLoadTexEv',
               '_ZN2GX16BeginLoadTexPlttEv', '_ZN2GX14EndLoadTexPlttEv']
UPLOADS = [('texture', '_ZN2GX7LoadTexEPKvjj'), ('palette', '_ZN2GX11LoadTexPlttEPKvjj')]
SOURCE_FILES = ['src/func_020462d0.c', 'src/func_02044b30.c',
                'src/func_0204488c.c', 'src/func_02055998.c',
                'src/_ZN5Model17UpdateFileOffsetsER8BMD_File.cpp',
                'src/_ZN5Model13LoadTexAndPalER8BMD_File.cpp',
                'src/_ZN5Model27LoadCompressedTextureToVramEPcjS0_.cpp']


def rd32(data, offset):
    assert 0 <= offset <= len(data)-4
    return struct.unpack_from('<I', data, offset)[0]


def span(data, offset, size):
    assert 0 <= offset <= len(data) and 0 <= size <= len(data)-offset
    return data[offset:offset+size]


def bounded_cpu(elf, model, material, texture, palette, body_list):
    harness = constructors.Harness(elf)
    itcm_symbols = (ROOT/'upstream/config/arm9/itcm/symbols.txt').read_text()
    match = re.search(r'^func_01ffde98 kind:function\(arm,size=0xa4\) addr:0x([0-9a-f]+)$',
                      itcm_symbols, re.M)
    assert match and int(match[1], 16) == 0x01ffde98
    harness.original.names['func_01ffde98'] = int(match[1], 16)
    # This SDK boundary is an explicit observer; no original DMA is executed.
    harness.original.cpu.mem_map(0x01ffd000, 0x1000)
    snapshots = []
    for probe in (harness.original, harness.native):
        probe.reset()
        probe.cpu.mem_map(0x04000000, 0x2000)
        probe.cpu.mem_write(MODEL, model)
        probe.cpu.mem_write(COMPONENTS, bytes([0xa5])*0x40)
        probe.cpu.mem_write(BUFFER, bytes([0xa5])*0x200)
        probe.cpu.mem_write(VIEW, IDENTITY43)
        probe.uploads, probe.words, probe.display_lists = [], [], []

        def mmio(cpu, access, address, size, value, _, p=probe):
            if 0x04000400 <= address <= 0x040005c8:
                assert size == 4 and address % 4 == 0
                port = 0 if address < 0x04000440 else (address-0x04000400)//4
                p.words.append((port, value & 0xffffffff))

        probe.cpu.hook_add(UC_HOOK_MEM_WRITE, mmio)
        if probe is harness.native:
            def draw(cpu, address, size, _, p=probe):
                if address == p.names['sm64ds_draw_port']:
                    p.words.append((p.reg(0), p.reg(1)))
                elif address == p.names['sm64ds_draw_fifo']:
                    p.words.append((0, p.reg(0)))
                else:
                    return
                cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))
            probe.cpu.hook_add(UC_HOOK_CODE, draw)
        for name in TRANSITIONS:
            probe.seams[probe.names[name]] = (
                lambda name=name, p=probe: p.uploads.append({'transition': name}))
        for kind, name in UPLOADS:
            def upload(kind=kind, p=probe):
                source, offset, size = p.reg(0), p.reg(1), p.reg(2)
                assert MODEL <= source <= MODEL+len(model)
                assert size <= MODEL+len(model)-source
                p.uploads.append({'kind': kind, 'file_offset': hex(source-MODEL),
                                  'destination': hex(offset), 'bytes': size,
                                  'sha256': base.sha(bytes(p.cpu.mem_read(source, size)))})
            probe.seams[probe.names[name]] = upload

        def display_list(p=probe):
            channel, source, size = p.reg(0), p.reg(1), p.reg(2)
            assert (channel, source, size) == (3, MODEL+body_list[0], body_list[1])
            payload = bytes(p.cpu.mem_read(source, size))
            assert payload == span(model, *body_list)
            p.display_lists.append({'channel': channel, 'file_offset': hex(source-MODEL),
                                    'bytes': size, 'sha256': base.sha(payload)})
            p.words.extend((0, word[0]) for word in struct.iter_unpack('<I', payload))

        probe.seams[probe.names['func_01ffde98']] = display_list
        probe.call('_ZN5Model17UpdateFileOffsetsER8BMD_File', [MODEL])
        # Upload cursors are controlled. The palette top follows exactly from
        # observed 0x3b00 plus this original palette's 0x200 bytes.
        for name, value in [('data_020a4bc8', 0), ('data_020a4be8', 0x20000),
                            ('data_020a4be0', 0x20000), ('data_020a4be4', 0),
                            ('data_020a4bcc', 0), ('data_020a4bd8', 0x3d00),
                            ('data_020a4bdc', 0x40000)]:
            probe.cpu.mem_write(probe.names[name], struct.pack('<I', value))
        probe.call('_ZN5Model13LoadTexAndPalER8BMD_File', [MODEL])
        probe.call('func_020462d0', [COMPONENTS, MODEL, BUFFER])
        runtime_material = probe.u32(COMPONENTS+4)
        prepared = bytes(probe.cpu.mem_read(runtime_material, 0x30))
        assert rd32(prepared, 0x1c) == 0x95230000 and rd32(prepared, 0x20) == 0x3b0
        probe.call('func_02044b30', [COMPONENTS, 0])
        binding_words = list(probe.words)
        assert binding_words == [(0x29, rd32(model, material+0x24)),
                                 (0x30, rd32(model, material+0x28)),
                                 (0x31, rd32(model, material+0x2c)),
                                 (0x2b, 0x3b0), (0x2a, 0x95230000),
                                 (0x22, 0x01600060)]
        assert struct.unpack('<h', probe.cpu.mem_read(probe.names['data_020a4bc0'], 2))[0] == 16
        assert struct.unpack('<h', probe.cpu.mem_read(probe.names['data_020a4bc4'], 2))[0] == -16
        bone_matrices = probe.u32(COMPONENTS+0xc)
        probe.cpu.mem_write(bone_matrices, IDENTITY43)
        for name, value in [('data_020a4bd0', VIEW), ('data_020a4bd4', 4096)]:
            probe.cpu.mem_write(probe.names[name], struct.pack('<I', value))
        probe.call('func_0204488c', [COMPONENTS, 0, 0])
        snapshots.append({'uploads': probe.uploads, 'material_sha256': base.sha(prepared),
                          'binding_words': binding_words, 'words': probe.words,
                          'display_lists': probe.display_lists})
    assert snapshots[0] == snapshots[1], 'ROM and native model producer differ'
    result = snapshots[0]
    result['gx_word_count'] = len(result['words'])
    result['word_stream_sha256'] = base.sha(b''.join(struct.pack('<II', *w) for w in result['words']))
    # Preserve the non-asset setup immediately before the original display list.
    prefix = result['words'][:-body_list[1]//4]
    result['texture_matrix_tail'] = [(hex(p), hex(w)) for p, w in prefix[-18:]]
    assert prefix[-18][0:2] == (0x10, 3)
    assert all(port == 0x16 for port, value in prefix[-17:-1])
    assert prefix[-1] == (0x13, 0)
    del result['words']
    result['binding_words'] = [(hex(p), hex(w)) for p, w in result['binding_words']]
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--snapshot-dir', type=Path,
                        default=ROOT/'build/native-runtime-azahar/attempt-1791596592223444000')
    parser.add_argument('--elf', type=Path)
    parser.add_argument('--report', type=Path,
                        default=ROOT/'build/native-runtime/menu-star-material-audit.json')
    args = parser.parse_args()
    assert subprocess.check_output(['git', '-C', str(ROOT/'upstream'), 'rev-parse', 'HEAD'],
                                   text=True).strip() == PIN
    source_hashes = {name: base.sha((ROOT/'upstream'/name).read_bytes()) for name in SOURCE_FILES}
    recorded = json.loads((args.snapshot_dir/'result.json').read_text())
    files = {kind: args.snapshot_dir/('scene-1-first-'+kind+extension)
             for kind, extension in [('gx', '.bin'), ('textures', '.bin'),
                                     ('palettes', '.bin'), ('geometry', '.json')]}
    inputs, data = {}, {}
    for kind, path in files.items():
        data[kind] = path.read_bytes()
        saved = recorded['first_scene_frame_files'][path.name]
        assert len(data[kind]) == saved['bytes'] and base.sha(data[kind]) == saved['sha256']
        inputs[kind] = {'path': str(path), 'bytes': len(data[kind]), 'sha256': saved['sha256']}
    elf = args.elf or args.snapshot_dir/'sm64ds-native.elf'
    elf_sha = base.sha(elf.read_bytes())
    assert elf_sha == recorded['elf_sha256']
    geometry = json.loads(data['geometry'])
    assert geometry['texture_capacity'] == 0x40000 and geometry['palette_capacity'] == 0x4000
    assert not geometry['texture_transfer'] and not geometry['palette_transfer']
    cart = base.Cartridge(ROOT/'build/europe.pak')
    _, indices = cart.handles()
    assert cart.entries[indices[649]][4] == ASSET
    packed = cart.file(ASSET)
    assert packed[:5] == b'LZ77\x10' and base.sha(packed) == PACKED_SHA
    model = ndspy.lz10.decompress(packed[4:])
    assert len(model) == 2556 and base.sha(model) == MODEL_SHA
    assert rd32(model, 0x14) == rd32(model, 0x1c) == 1 and rd32(model, 0x24) == 2
    texture, palette, material = (rd32(model, offset) for offset in (0x18, 0x20, 0x28))
    assert (texture, palette, material) == (0x5c8, 0x5e8, 0x608)
    material_flags, texture_flags = rd32(model, material+0x20), rd32(model, texture+0x10)
    assert (material_flags, texture_flags) == (0x80030000, 0x15200000)
    assert rd32(model, material+4) == rd32(model, material+8) == 0
    image = material_flags | texture_flags
    assert image == 0x95230000 and image >> 30 == 2
    words = list(struct.iter_unpack('<II', data['gx']))
    hits = [i for i, row in enumerate(words) if row == (0x2a, image)]
    assert hits == [1007]
    assert words[1006] == (0x2b, 0x3b0) and words[1008] == (0x22, 0x01600060)
    payloads = []
    tex_offset, tex_size = rd32(model, texture+4), rd32(model, texture+8)
    pal_offset, pal_size = rd32(model, palette+4), rd32(model, palette+8)
    for kind, file_offset, size, snapshot_offset, snapshot in [
            ('compressed_texels', tex_offset, tex_size, 0, data['textures']),
            ('compression_indices', tex_offset+tex_size, tex_size//2, 0x20000, data['textures']),
            ('palette', pal_offset, pal_size, 0x3b00, data['palettes'])]:
        expected, actual = span(model, file_offset, size), span(snapshot, snapshot_offset, size)
        assert expected == actual, kind+' differs from original BMD'
        payloads.append({'kind': kind, 'file_offset': hex(file_offset), 'bytes': size,
                         'snapshot_offset': hex(snapshot_offset), 'sha256': base.sha(expected),
                         'identical_to_rom': True})
    lists = []
    for group in range(rd32(model, 0xc)):
        row = rd32(model, 0x10)+group*8
        count, records = rd32(model, row), rd32(model, row+4)
        assert count == 1
        for index in range(count):
            record = records+index*0x10
            size, offset = rd32(model, record+8), rd32(model, record+0xc)
            payload = span(model, offset, size)
            expected = [(0, value[0]) for value in struct.iter_unpack('<I', payload)]
            starts = [i for i in range(len(words)-len(expected)+1)
                      if words[i:i+len(expected)] == expected]
            assert len(starts) == 1, (group, starts)
            lists.append({'group': group, 'list': index, 'record_offset': hex(record),
                          'file_offset': hex(offset), 'bytes': size, 'sha256': base.sha(payload),
                          'stream_entry': starts[0], 'fifo_words': len(expected),
                          'entire_fifo_payload_identical_to_rom': True})
    assert lists[0]['stream_entry'] == 1083 and lists[1]['stream_entry'] == 1412
    tail = words[1083-18:1083]
    assert tail[0] == (0x10, 3) and all(port == 0x16 for port, value in tail[1:17])
    assert tail[17] == (0x13, 0)
    # Negative controls mutate only in-memory evidence; original inputs remain
    # untouched. Each exact-equality contract must detect its altered input.
    assert ((material_flags ^ (1 << 30)) | texture_flags) != image
    altered_texels = bytearray(span(model, tex_offset, tex_size)); altered_texels[0] ^= 1
    assert bytes(altered_texels) != span(data['textures'], 0, tex_size)
    altered_list = bytearray(span(model, 0x60, 0x430)); altered_list[0] ^= 1
    assert [(0, value[0]) for value in struct.iter_unpack('<I', altered_list)] != words[1083:1351]
    cpu = bounded_cpu(elf, model, material, texture, palette, (0x60, 0x430))
    assert base.sha(elf.read_bytes()) == elf_sha, 'Snapshot ELF changed during audit'
    assert source_hashes == {name: base.sha((ROOT/'upstream'/name).read_bytes()) for name in SOURCE_FILES}
    report = {'status': 'authentic TEXGEN2 material and intact uploads confirmed',
              'scope': 'Preserved emulator stream/data evidence plus bounded host ROM/ARM11 producer comparison; no emulator executed, rendered frame, gameplay, physical console or FPS proof',
              'asset': {'path': ASSET, 'handle': 649, 'packed_bytes': len(packed),
                        'packed_sha256': PACKED_SHA, 'unpacked_bytes': len(model), 'unpacked_sha256': MODEL_SHA},
              'elf': str(elf), 'elf_sha256': elf_sha, 'inputs': inputs, 'geometry': geometry,
              'binding': {'stream_entry': 1007, 'material': 'mat_body',
                          'material_offset': hex(material), 'material_flags_offset': hex(material+0x20),
                          'material_flags': hex(material_flags), 'texture': 'star_body04',
                          'texture_record_offset': hex(texture), 'texture_flags': hex(texture_flags),
                          'image': hex(image), 'palette': '0x3b0', 'format': 5, 'texgen': 2,
                          'width': 32, 'height': 32, 'texture_offset': 0, 'palette_offset': '0x3b00',
                          'texcoord': '0x01600060'},
              'actual_upload_payloads': payloads, 'original_display_lists': lists,
              'observed_texture_matrix_tail': [(hex(p), hex(w)) for p, w in tail],
              'producer_chain': ['Model::UpdateFileOffsets', 'Model::LoadTexAndPal',
                                 'func_020462d0: material flags OR relocated texture flags',
                                 'func_02044b30: bind original material and publish TEXGEN2 size/offset',
                                 'func_0204488c -> func_02055998 -> MTX_MODE3/LOAD4x4/STORE0 -> original FIFO list'],
              'rom_native_cpu': cpu,
              'controlled_cpu_seams': ['SDK upload and FIFO-display-list observers',
                                      'native GX draw-port/FIFO observers',
                                      'upload cursors derived from actual snapshot',
                                      'identity bone/view matrices, no animation claim'],
              'negative_controls_detected': 3, 'cartridge_bytes_written_to_disk': False,
              'upstream_pin': PIN, 'source_sha256': source_hashes}
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({'status': report['status'], 'report': str(args.report),
                      'elf_sha256': elf_sha, 'binding_entry': 1007,
                      'matched_upload_ranges': len(payloads), 'matched_display_lists': len(lists),
                      'cpu_gx_words': cpu['gx_word_count']}, indent=2))


if __name__ == '__main__':
    main()
