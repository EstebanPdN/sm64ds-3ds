"""Relocate BUILD 142's linked asset sections into writable storage and split data.

Input objects and the populated ELF are private. The public recipe contains
source identities, offsets, lengths and a digest, with no captured bytes.
"""
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

from elftools.elf.elffile import ELFFile
from tools.devkitpro_tools import arm_tool

ROOT = Path(__file__).resolve().parents[2]
LAYOUT_SIZE = 40 + 8 * 8192
ART = re.compile(r'(?:rom_art_embedded|retail_pause_art|native_lower_art|model_\d+_skin_vertices|rom_install_manifest\d+generated_\d+)')
ASSETS = re.compile(r'^\.rodata\.(?:_Z.*' + ART.pattern + r'.*E|anim_[\dA-F_]+_(?:values|indices))$')
ANIM = re.compile(r'anim_[\dA-F_]+_(?:values|indices)$')
PLACEMENT = re.compile(r'^ (\.[\w.$-]+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(.+)$')
SDK_SCRIPT = 'arm-none-eabi/lib/3dsx.ld'


def link_source_map(path):
    """Read allocated input sections, excluding the discarded section list."""
    rows = []
    active = ''
    previous = ''
    for line in path.read_text().splitlines():
        if line.startswith('.'):
            active = line.split()[0]
        if active != '.rodata':
            continue
        if not line.strip().startswith('.') and previous:
            line = previous + line
        previous = line if re.fullmatch(r' \.[\w.$-]+', line) else ''
        match = PLACEMENT.match(line)
        if match:
            section, address, size, filename = match.groups()
            if int(address, 16) and int(size, 16) and filename.endswith(('.o', '.o)')):
                rows.append((section, int(address, 16), int(size, 16), filename))
    return rows


def native_symbols(elf):
    return {s.name: s.entry for s in elf.get_section_by_name('.symtab').iter_symbols()
            if s['st_value'] and s['st_shndx'] != 'SHN_UNDEF'}


def art_symbol(name):
    return bool(ART.search(name)) and name.startswith('_ZN')


def patch_entry(source):
    """Enable the loader before settings, ROM artwork and the retail heap."""
    include = '#include "rom_setup_3ds.hpp"'
    directory = ' chdir("sdmc:/3ds/sm64ds");'
    graphics = ' gfxInitDefault();sm64ds_configure_cpu_mode();'
    settings = ' enhance::boot_apply();'
    for anchor in (include, directory, graphics, settings):
        if source.count(anchor) != 1:
            raise ValueError('BUILD 142 entry template changed')
    source = source.replace(include, include + '\n#include "public_game_data.hpp"')
    source = source.replace(directory, directory + """
 gfxInitDefault();
 if (!public_game_data::prepare()) { gfxExit(); return 1; }
""")
    source = source.replace(graphics, ' sm64ds_configure_cpu_mode();')
    return source.replace(settings, ' std::puts("Verified BUILD 142 external data");\n' + settings)


def sdk_linker_script(output):
    toolchain = Path(arm_tool('gcc')).parents[1]
    script = (toolchain / SDK_SCRIPT).read_text()
    left = '\t\t*(.data)'
    right = '\t\t*(.gnu.linkonce.d*)'
    if script.count(left) != 1 or script.count(right) != 1:
        raise ValueError('Unsupported devkitARM 3DSX linker script')
    script = script.replace(left, '\t\tsm64ds_external_begin = .;\n' + left)
    script = script.replace(right, right + '\n\t\tsm64ds_external_end = .;')
    (output / '3dsx.ld').write_text(script)


def writable_objects(command, cwd, output, readonly):
    objcopy = arm_tool('objcopy')
    inputs = {str((cwd / x).resolve()): i for i, x in enumerate(command)
              if x.endswith('.o') and not x.startswith('-')}
    archives = {}
    ar = arm_tool('ar')
    for index, (filename, sections) in enumerate(sorted(readonly.items())):
        archive, member = (None, None)
        if filename.endswith(')'):
            archive_name, member = filename[:-1].rsplit('(', 1)
            archive = (cwd / archive_name).resolve()
            path = output / member
            path.write_bytes(subprocess.check_output([ar, 'p', archive, member]))
            renamed = output / 'members' / member
            renamed.parent.mkdir(exist_ok=True)
        else:
            path = (cwd / filename).resolve()
            renamed = output / f'data-{index}.o'
        options = [f'--rename-section={section}=.data.sm64ds_external.{section.removeprefix(".rodata.")},alloc,load,data,contents'
                   for section in sorted(sections)]
        subprocess.run([objcopy, *options, path, renamed], check=True)
        if archive is None:
            command[inputs[str(path)]] = str(renamed)
        else:
            if archive not in archives:
                copy = output / archive.name
                copy.write_bytes(archive.read_bytes())
                archive_index = next(i for i, arg in enumerate(command)
                                     if not arg.startswith('-') and (cwd / arg).resolve() == archive)
                command[archive_index] = str(copy)
                archives[archive] = copy
            subprocess.run([ar, 'r', archives[archive], renamed], check=True)


def patch_elf(initial, output, hits, companion_sources, local_path):
    """Split exact DS runs and known compiled asset arrays without touching relocations."""
    with initial.open('rb') as stream:
        elf = ELFFile(stream)
        section = elf.get_section_by_name('.data')
        symbols = native_symbols(elf)
        base = symbols['sm64ds_external_begin']['st_value']
        end = symbols['sm64ds_external_end']['st_value']
        values = section.data()
        if section['sh_addr'] != base or end - base > len(values):
            raise ValueError('Wrong writable data span in linked executable')
        if not any(seg['p_type'] == 'PT_LOAD' and seg['p_flags'] == 6 and
                   seg['p_vaddr'] <= base < end <= seg['p_vaddr'] + seg['p_filesz'] for seg in elf.iter_segments()):
            raise ValueError('External data is outside the file-backed writable segment')
        relocs = {rel['r_offset'] - base
                  for rel in elf.get_section_by_name('.rel.data').iter_relocations()}
        data_offset = section['sh_offset']
        layout = symbols['sm64ds_external_layout']
        layout_section = elf.get_section(layout['st_shndx'])
        layout_address = layout_section['sh_offset'] + layout['st_value'] - layout_section['sh_addr']

    raw = bytearray(initial.read_bytes())
    if layout['st_size'] != LAYOUT_SIZE or any(raw[layout_address:layout_address+LAYOUT_SIZE]):
        raise ValueError('Invalid reserved data layout')

    selected = []
    for name, entry in symbols.items():
        address, size = entry['st_value'], entry['st_size']
        if (art_symbol(name) or ANIM.fullmatch(name)) and size:
            selected.append((address - base, size, name, 0))
    if len(selected) < 15:
        raise ValueError('Expected compiled art, model and manifest assets')
    for name, pos, length in companion_sources:
        selected.append((symbols[name]['st_value'] - base, length, 'arm9', pos))
    # Explicit symbol groups take priority over the exact-run scanner, which can
    # return ambiguous overlapping spans for repetitive data.
    occupied = bytearray(end - base)
    for offset in relocs:
        if 0 <= offset <= len(occupied) - 4:
            occupied[offset:offset+4] = b'\x01' * 4
    entries = []
    for offset, size, source, start in selected + [
        (addr - base, size, source, start) for sec, addr, source, start, size in hits if sec == '.data'
    ]:
        if offset < 0 or offset + size > len(occupied):
            raise ValueError(f'Asset data did not link into the writable segment: {source}')
        cursor = offset
        while cursor < offset + size:
            stop = occupied.find(1, cursor, offset + size)
            stop = offset + size if stop < 0 else stop
            if cursor < stop:
                entries.append((cursor, stop - cursor, source, start + cursor - offset))
                occupied[cursor:stop] = b'\x01' * (stop - cursor)
            cursor = stop + 1

    entries.sort()
    payload = bytearray()
    spans = []
    records = []
    for offset, size, source, start in entries:
        first = data_offset + offset
        payload.extend(raw[first:first+size])
        raw[first:first+size] = bytes(size)
        if spans and spans[-1][0] + spans[-1][1] == offset:
            spans[-1][1] += size
        else:
            spans.append([offset, size])
        if records and records[-1][0] == source and records[-1][1] + records[-1][2] == start:
            records[-1][2] += size
        else:
            records.append([source, start, size])
    digest = hashlib.sha256(payload).digest()
    metadata = struct.pack('<II32s', len(payload), len(spans), digest)
    metadata += b''.join(struct.pack('<II', *span) for span in spans)
    if len(metadata) > LAYOUT_SIZE:
        raise ValueError('External layout exceeded reserved span capacity')
    raw[layout_address:layout_address+len(metadata)] = metadata
    # One development path survives in an assertion string in the baseline.
    # Keep its size and remove the checkout prefix without changing addresses.
    for segment in segment_file_regions(initial):
        start, stop = segment
        at = raw.find(local_path, start, stop)
        while at >= 0:
            raw[at:at+len(local_path)] = b'.' + b'_' * (len(local_path) - 1)
            at = raw.find(local_path, at+len(local_path), stop)
        if b'/Users/' in raw[start:stop]:
            raise ValueError('An unmapped development path remains in a load segment')

    output.mkdir(exist_ok=True)
    (output / 'sm64ds.elf').write_bytes(raw)
    (output / 'build142-data.bin').write_bytes(payload)
    (output / 'beta142-layout.json').write_text(json.dumps(
        {'build': 142, 'bytes': len(payload), 'sha256': digest.hex(), 'rows': records},
        indent=2) + '\n')
    return len(payload), len(spans)


def segment_file_regions(path):
    with path.open('rb') as stream:
        for segment in ELFFile(stream).iter_segments():
            if segment['p_type'] == 'PT_LOAD':
                off = segment['p_offset']
                yield off, off + segment['p_filesz']
