#!/usr/bin/env python3
"""Read captured PPU dispatch, using each exact ELF's DWARF; never touch live RAM."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
from elftools.elf.elffile import ELFFile
from inspect_integrated_vm import VM

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def name(die):
    attr = die.attributes.get('DW_AT_name')
    return attr.value.decode(errors='replace') if attr and isinstance(attr.value, bytes) else ''


def bare(die):
    while die.tag in ('DW_TAG_typedef', 'DW_TAG_const_type', 'DW_TAG_volatile_type'):
        die = die.get_DIE_from_attribute('DW_AT_type')
    return die


def declared_type(die):
    while 'DW_AT_type' not in die.attributes:
        die = die.get_DIE_from_attribute('DW_AT_specification')
    return die.get_DIE_from_attribute('DW_AT_type')


def member(die, wanted):
    die = bare(die)
    for child in die.iter_children():
        if child.tag == 'DW_TAG_member' and name(child) == wanted:
            attr = child.attributes.get('DW_AT_data_member_location')
            assert attr is not None or die.tag == 'DW_TAG_union_type'
            return (attr.value if attr else 0), child.get_DIE_from_attribute('DW_AT_type')
    for child in die.iter_children():
        if child.tag == 'DW_TAG_inheritance':
            try:
                offset, result = member(child.get_DIE_from_attribute('DW_AT_type'), wanted)
                return child.attributes['DW_AT_data_member_location'].value + offset, result
            except KeyError:
                pass
    raise KeyError((name(die), wanted))


def path(die, *names):
    offset = 0
    for wanted in names:
        step, die = member(die, wanted)
        assert isinstance(step, int)
        offset += step
    return offset, bare(die)


def layout(die):
    die = bare(die)
    return dict(name=name(die), bytes=die.attributes['DW_AT_byte_size'].value,
                members={name(c): c.attributes['DW_AT_data_member_location'].value
                         for c in die.iter_children() if c.tag == 'DW_TAG_member'
                         and 'DW_AT_data_member_location' in c.attributes})


def first_obj_rejection(display, oam):
    """The pinned obj_admission preflight, preserving its visibility/order rules."""
    if (display >> 16) & 3 > 1:
        return dict(reason='DisplayMode')
    if not display & (1 << 12) or display & 0x80 or not (display >> 16) & 3:
        return None
    if display & 0xe000:
        return dict(reason='Windows')
    sizes = (((8, 8), (16, 16), (32, 32), (64, 64)),
             ((16, 8), (32, 8), (32, 16), (64, 32)),
             ((8, 16), (8, 32), (16, 32), (32, 64)))
    for index in range(128):
        a0, a1, a2 = struct.unpack_from('<3H', oam, index * 8)
        affine = bool(a0 & 0x100)
        if not affine and a0 & 0x200:
            continue
        shape, size = a0 >> 14, a1 >> 14
        if shape == 3:
            return dict(reason='Shape', index=index)
        width, height = sizes[shape][size]
        bw, bh = (width * 2, height * 2) if affine and a0 & 0x200 else (width, height)
        x, y = a1 & 511, a0 & 255
        if x >= 256:
            x -= 512
        if y >= 192 and y >= 256 - bh:
            y -= 256
        if x >= 256 or x + bw <= 0 or y >= 192 or y + bh <= 0:
            continue
        mode = (a0 >> 10) & 3
        reason = ('Mosaic' if a0 & 0x1000 else
                  {1: 'Alpha', 2: 'Windows', 3: 'Bitmap'}.get(mode))
        if not reason and a0 & 0x2000 and display & 0x80000000:
            reason = 'ExtendedPalette'
        if reason:
            return dict(reason=reason, index=index, a0=hex(a0), a1=hex(a1), a2=hex(a2),
                        x=x, y=y, width=width, height=height, mode=mode, affine=affine)
    return None


def inspect(vm_path, elf_path, expected='cpu'):
    assert expected in ('cpu', 'affine-alpha')
    vm = VM(vm_path)
    anchor_path = vm_path.with_suffix('.anchors.json')
    metadata = json.loads(anchor_path.read_text())
    assert metadata['worker_drained'] and metadata['word_bytes'] == 4
    assert (metadata['scene'], metadata['epoch']) == (vm.header['scene'], vm.header['epoch'])
    with elf_path.open('rb') as stream:
        elf = ELFFile(stream)
        assert elf.elfclass == 32 and elf.little_endian
        symbols = {s.name: s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
        aliases = {'render60_interpolation': '_ZN8render6013interpolationE'}
        biases = {key: address - symbols[aliases.get(key, key)]
                  for key, address in metadata['anchors'].items()}
        assert len(set(biases.values())) == 1
        bias = next(iter(biases.values()))
        # This is a linked executable; its debug values are already relocated.
        dwarf = elf.get_dwarf_info(relocate_dwarf_sections=False)
        dies = []
        for cu in dwarf.iter_CUs():
            units = ('/ppu.cpp', '/player_present.cpp')
            if expected == 'affine-alpha':
                units += ('/viewer_3ds.cpp',)
            if name(cu.get_top_DIE()).endswith(units):
                dies.extend(cu.iter_DIEs())

        def variable(address):
            found = []
            for die in dies:
                loc = die.attributes.get('DW_AT_location')
                if die.tag == 'DW_TAG_variable' and loc and loc.form == 'DW_FORM_exprloc':
                    code = bytes(loc.value)
                    if len(code) == 5 and code[0] == 3 and int.from_bytes(code[1:], 'little') == address:
                        found.append(declared_type(die))
            assert len(found) == 1, hex(address)
            return found[0]

        def symbol(part, suffix=''):
            matches = [v for k, v in symbols.items() if part in k and k.endswith(suffix)
                       and not k.startswith(('_ZGV', '__tcf'))]
            assert len(matches) == 1, (part, suffix)
            return matches[0]

        def value(address, die, *members, fmt='I'):
            offset, _ = path(die, *members)
            return struct.unpack('<' + fmt, vm.read(address + offset, struct.calcsize(fmt)))[0]

        def vector_info(address, die):
            offsets = [path(die, '_M_impl', field) for field in ('_M_start', '_M_finish', '_M_end_of_storage')]
            start, finish, end = [vm.u32(address + offset) for offset, _ in offsets]
            pointer = offsets[0][1]
            assert pointer.tag == 'DW_TAG_pointer_type' and pointer.attributes['DW_AT_byte_size'].value == 4
            element = bare(pointer.get_DIE_from_attribute('DW_AT_type'))
            size = element.attributes['DW_AT_byte_size'].value
            assert start <= finish <= end and (finish - start) % size == 0 and (end - start) % size == 0
            return dict(start=start, finish=finish, storage_end=end, element_bytes=size,
                        size_elements=(finish - start) // size, capacity_elements=(end - start) // size,
                        size_bytes=finish - start, capacity_bytes=end - start), element

        def vector(address, die):
            info, element = vector_info(address, die)
            return info['start'], info['size_elements'], element

        def array_info(die):
            die = bare(die)
            assert die.tag == 'DW_TAG_array_type'
            ranges = [d for d in die.iter_children() if d.tag == 'DW_TAG_subrange_type']
            assert len(ranges) == 1
            lower = ranges[0].attributes.get('DW_AT_lower_bound')
            assert not lower or lower.value == 0
            count_attr = ranges[0].attributes.get('DW_AT_count')
            count = count_attr.value if count_attr else ranges[0].attributes['DW_AT_upper_bound'].value + 1
            element = bare(die.get_DIE_from_attribute('DW_AT_type'))
            return count, element, element.attributes['DW_AT_byte_size'].value

        def buffer_record(address, die):
            info, _ = vector_info(address, die)
            info['used_sha256'] = hashlib.sha256(vm.read(info['start'], info['size_bytes'])).hexdigest()
            return {key: hex(val) if key in ('start', 'finish', 'storage_end') else val
                    for key, val in info.items()}

        job_symbol = symbol('_ZN16present_pipeline3jobE')
        job_type = variable(job_symbol)
        job = job_symbol + bias
        b_offset, b_optional = path(job_type, 'b')
        b_value_offset, candidate_type = path(b_optional, '_M_payload', '_M_payload', '_M_value')
        b = job + b_offset + b_value_offset
        helper_symbol = symbol('_ZN16present_pipeline13helper_resultE')
        helper_type = variable(helper_symbol)
        job_b = dict(semantics='Retained presentation Job after worker drain; not an in-flight worker.',
                     engaged=bool(value(job + b_offset, b_optional, '_M_payload', '_M_engaged', fmt='B')),
                     gpu_affine=hex(value(b, candidate_type, 'gpu_affine')),
                     background_identity=hex(value(b, candidate_type, 'background_identity')),
                     background_generation=value(b, candidate_type, 'background_generation', fmt='Q'))
        objects_offset, objects_type = path(job_type, 'objects')
        pointer_type = bare(objects_type.get_DIE_from_attribute('DW_AT_type'))
        pointer_bytes = pointer_type.attributes['DW_AT_byte_size'].value
        job_b['objects_pointer'] = hex(vm.u32(job + objects_offset + pointer_bytes))
        state_offset, composition_type = path(candidate_type, 'state')
        job_b['state'] = {key: value(b + state_offset, composition_type, key, fmt=fmt)
                          for key, fmt in [('resolved', 'B'), ('has_obj_alpha', 'B'), ('blend', 'H')]}
        helper = dict(engaged=bool(value(helper_symbol + bias, helper_type, '_M_payload', '_M_engaged', fmt='B')),
                      pending=bool(vm.read(symbol('_ZN16present_pipeline14helper_pendingE') + bias, 1)[0]))
        states_symbol = symbol('gpu_affine_candidates', '6states')
        start, count, state_type = vector(states_symbol + bias, variable(states_symbol))
        assert count == 2
        state = start + layout(state_type)['bytes']
        key_offset, key_array = path(state_type, 'overlay_key')
        key_type = bare(key_array.get_DIE_from_attribute('DW_AT_type'))
        key = state + key_offset + value(state, state_type, 'overlay_current') * layout(key_type)['bytes']
        historical = dict(generation=value(state, state_type, 'generation', fmt='Q'),
                          overlay_generation=value(state, state_type, 'overlay_generation', fmt='Q'),
                          overlay_count=value(key, key_type, 'n'),
                          overlay_bgs=list(struct.unpack('<3i', vm.read(key + path(key_type, 'bg')[0], 12))),
                          blend=bool(value(key, key_type, 'blend', fmt='B')),
                          interpretation='Retained historical cache; not evidence of current dispatch.')
        region_address = symbols['_ZN3ntr8g_regionE'] + bias
        regions = struct.unpack('<8I', vm.read(region_address, 32))
        io = regions[4] + 0x1000
        display = vm.u32(io)
        rejection = first_obj_rejection(display, vm.read(regions[7] + 0x400, 1024))
        assert rejection and rejection['reason'] == 'Alpha'
        assert job_b['engaged'] and job_b['objects_pointer'] == '0x0'
        assert not helper['engaged'] and not helper['pending']
        affine_alpha = None
        if expected == 'cpu':
            assert rejection['index'] == 16 and job_b['gpu_affine'] == '0x0'
            assert job_b['state']['resolved']
        else:
            assert job_b['gpu_affine'] != '0x0', 'Expected affine-alpha dispatch, but captured job.b is CPU.'
            storage_symbol = symbol('ppu_prepare_affine_alpha_objects', '7storage')
            storage_type = variable(storage_symbol)
            storage = storage_symbol + bias
            current = value(storage, storage_type, 'current')
            layer_offset, layer_array = path(storage_type, 'layer')
            layer_count, layer_type, layer_size = array_info(layer_array)
            assert 0 <= current < layer_count
            descriptor_address = storage + layer_offset + current * layer_size
            assert int(job_b['gpu_affine'], 16) == descriptor_address
            assert int(job_b['background_identity'], 16) == descriptor_address
            generation = value(storage, storage_type, 'generation', fmt='Q')
            assert generation > 0 and job_b['background_generation'] == generation
            descriptor = {}
            for field, fmt in [('raster', 'I'), ('overlay', 'I'), ('width', 'i'), ('height', 'i'),
                               ('raster_generation', 'Q'), ('overlay_generation', 'Q'), ('base_generation', 'Q'),
                               ('ref_x', 'i'), ('ref_y', 'i'), ('pa', 'h'), ('pb', 'h'), ('pc', 'h'), ('pd', 'h'),
                               ('wrap', 'B'), ('backdrop', 'I'), ('layer', 'B'), ('priority', 'B'),
                               ('effect_mode', 'B'), ('effect_factor', 'B'), ('effect_layer', 'B'), ('effect_backdrop', 'B')]:
                number = value(descriptor_address, layer_type, field, fmt=fmt)
                descriptor[field] = hex(number) if field in ('raster', 'overlay') else number
            assert descriptor['overlay_generation'] == generation and descriptor['width'] > 0 and descriptor['height'] > 0
            buffers = {}
            for field in ('objects', 'priority'):
                offset, vector_type = path(storage_type, field)
                buffers[field] = buffer_record(storage + offset, vector_type)
                assert buffers[field]['size_elements'] == 256 * 192
            overlay_offset, overlay_array = path(storage_type, 'overlay')
            overlay_count, overlay_type, overlay_size = array_info(overlay_array)
            assert overlay_count == layer_count
            buffers['overlays'] = [buffer_record(storage + overlay_offset + index * overlay_size, overlay_type)
                                   for index in range(overlay_count)]
            selected = buffers['overlays'][current]
            assert selected['start'] == descriptor['overlay'] and selected['size_elements'] == 256 * 192
            raster_offset, raster_array = path(state_type, 'raster')
            raster_count, raster_type, raster_size = array_info(raster_array)
            borrowed = [buffer_record(state + raster_offset + index * raster_size, raster_type)
                        for index in range(raster_count)]
            raster_matches = [index for index, item in enumerate(borrowed) if item['start'] == descriptor['raster']]
            assert len(raster_matches) == 1
            assert borrowed[raster_matches[0]]['size_elements'] == descriptor['width'] * descriptor['height']
            # Confirm the worker consumed this exact source/version, not merely
            # that the producer created it and stored a pointer in a Job.
            compositor_symbol = symbol('g_resident_compositor')
            compositor_count, compositor_type, compositor_size = array_info(variable(compositor_symbol))
            assert compositor_count == 2
            compositor = compositor_symbol + bias + compositor_size
            consumer_offset, consumer_type = path(compositor_type, 'affine_overlay_key_')
            consumer = dict(source=hex(value(compositor + consumer_offset, consumer_type, 'source')),
                            generation=value(compositor + consumer_offset, consumer_type, 'generation', fmt='Q'),
                            valid=bool(value(compositor + consumer_offset, consumer_type, 'valid', fmt='B')))
            assert consumer['valid'] and consumer['source'] == descriptor['overlay'] and consumer['generation'] == generation
            worker = dict(busy=bool(vm.read(symbol('_ZN16present_pipeline4busyE') + bias, 1)[0]),
                          present_ok=bool(vm.read(symbol('_ZN16present_pipeline10present_okE') + bias, 1)[0]),
                          job_present=bool(value(job, job_type, 'present', fmt='B')))
            assert not worker['busy'] and not worker['job_present'] and worker['present_ok']
            affine_alpha = dict(storage=hex(storage), current=current, generation=generation,
                                descriptor_address=hex(descriptor_address), descriptor=descriptor, buffers=buffers,
                                borrowed_BG_rasters=borrowed, matching_BG_raster=raster_matches[0],
                                compositor_overlay_key=consumer, worker_result=worker,
                                owned_buffer_capacity_bytes=sum(item['capacity_bytes'] for item in
                                    [buffers['objects'], buffers['priority'], *buffers['overlays']]),
                                interpretation='Job selects current Storage descriptor; worker returned success and '
                                               'the compositor upload key matches the selected overlay. LCD delivery is not measured.')
        layouts = {label: layout(die) for label, die in [('Job', job_type), ('PpuPreparedCandidates', candidate_type),
                   ('PpuCompositionState', composition_type), ('AffineState', state_type), ('OverlayKey', key_type)]}
        layouts['optional_offsets'] = dict(job_b=b_offset, optional_value=b_value_offset,
                                          job_engaged=path(b_optional, '_M_payload', '_M_engaged')[0],
                                          helper_engaged=path(helper_type, '_M_payload', '_M_engaged')[0],
                                          object_pointer_bytes=pointer_bytes)
        if affine_alpha:
            layouts.update(AffineAlphaStorage=layout(storage_type), AffineAlphaDescriptor=layout(layer_type),
                           Composite=layout(compositor_type), AffineOverlayKey=layout(consumer_type))
    result = dict(provenance=dict(ELF=str(elf_path), ELF_sha256=sha(elf_path), VM=str(vm_path),
                               VM_sha256=hashlib.sha256(vm.raw).hexdigest(), anchors=str(anchor_path),
                               anchors_sha256=sha(anchor_path), load_bias=bias, anchor_biases=biases,
                               worker_drained=True, scene=vm.header['scene'], epoch=vm.header['epoch']),
                dwarf_layouts=layouts, job_b=job_b, helper_result=helper, historical_affine_cache=historical,
                EngineB=dict(DISPCNT=hex(display), BLDCNT=hex(struct.unpack('<H', vm.read(io + 0x50, 2))[0]),
                             BLDALPHA=hex(struct.unpack('<H', vm.read(io + 0x52, 2))[0]),
                             first_native_OBJ_rejection=rejection))
    if affine_alpha:
        result['affine_alpha_dispatch'] = affine_alpha
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for label in ('before', 'after'):
        parser.add_argument('--' + label + '-vm', required=True, type=Path)
        parser.add_argument('--' + label + '-elf', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--expect-after', choices=('cpu', 'affine-alpha'), default='cpu',
                        help='Default preserves the two historical CPU receipts; affine-alpha requires the new producer and consumer.')
    args = parser.parse_args()
    assert not args.output.exists(), 'This diagnostic only creates new reports.'
    sources = ['tools/inspect_integrated_ppu_dispatch.py', 'tools/inspect_integrated_vm.py',
               'reference-native.lock.json',
               'third_party/sm64ds-3ds/port/platform/3ds/native_obj_plan.cpp',
               'third_party/sm64ds-3ds/port/platform/3ds/native_obj_input_views.cpp']
    if args.expect_after == 'affine-alpha':
        sources += ['source/integrated_native/affine_alpha_objects.inc',
                    'source/integrated_native/affine_overlay_key.hpp', 'tools/prepare_integrated_render60.py']
    lock = json.loads((ROOT / 'reference-native.lock.json').read_text())
    for source in sources:
        if source.startswith('third_party/sm64ds-3ds/'):
            assert sha(ROOT / source) == lock['vendored_files_sha256'][source.removeprefix('third_party/sm64ds-3ds/')]
    report = dict(status='captured_CPU_dispatch_confirmed_by_DWARF_and_OAM',
                  sources={name: sha(ROOT / name) for name in sources},
                  source_commit=lock['commit'],
                  before=inspect(args.before_vm.resolve(), args.before_elf.resolve()),
                  after=inspect(args.after_vm.resolve(), args.after_elf.resolve(), args.expect_after),
                  finding='Visible semi-transparent OBJ rejects resident OBJ admission before GPU-affine preparation; '
                          'the retained presentation Job contains a CPU-resolved EngineB candidate.',
                  limits=['Read-only analysis of two historical process captures; no live process access or RAM writes.',
                          'Worker was drained at capture; GPU, OS and DSP state is not an atomic physical FCRAM snapshot.',
                          'Retained affine cache generations describe prior successful preparation, not current use.',
                          'No causal performance comparison, physical-console FPS claim, or attribution of all trace spikes.',
                          'OBJ admission replay is the pinned source predicate over captured DISPCNT/OAM; '
                          'it does not execute game code or manufacture gameplay state.'])
    if args.expect_after == 'affine-alpha':
        report['status'] = 'captured_CPU_to_affine_alpha_dispatch_confirmed'
        report['mode'] = 'before_CPU_after_affine_alpha'
        report['finding'] = ('Before retains a CPU-resolved EngineB job. After selects the current affine-alpha Storage '
                             'descriptor and its overlay is the compositor consumer key, with successful drained worker result.')
        report['limits'].append('Compositor key and worker success show consumed dispatch, not physical LCD presentation or timing. '
                                'Buffer capacities exclude allocator metadata, shared BG ownership, textures, GPU and DSP allocations.')
    with args.output.open('x') as stream:
        json.dump(report, stream, indent=2)
        stream.write('\n')
    print(args.output.resolve())


if __name__ == '__main__':
    main()
