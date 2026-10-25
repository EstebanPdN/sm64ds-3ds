#!/usr/bin/env python3
"""Read-only ROM/ARM11 virtual dispatch audit of four Stage lifecycle families.

Does not run factories, game functions, emulator, or captured RAM. Private ROM
code is read into memory and disassembled through a deleted temporary file;
only instruction text, identities and hashes are persisted. This is static ABI
evidence, not a lifecycle or resource initialization success test.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'build/game-data/python'))
import test_native_menu_callbacks as base

TOOL = base.TOOL
FROZEN_SHA = '396ac3ff7f20af01d411357d97da9cdc4dccdee70f6fc53d1d8305a5804361c8'
FAMILIES = ['5Stage', '6Camera', '3HUD', '7Minimap']
METHODS = ['13InitResources', '8Behavior', '6Render',
           '16CleanupResources', '16OnPendingDestroy']
TABLES = [('_ZTV' + cls, 18) for cls in FAMILIES] + [
    ('_ZTV5Model', 6), ('_ZTV18TextureTransformer', 2),
    ('_ZTV15FaderBrightness', 10), ('_ZTV10FaderColor', 10),
    ('_ZTV10dFdDummy_c', 10), ('_ZTV9FaderWipe', 10)]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def record(path):
    return dict(path=str(path.relative_to(ROOT)), sha256=sha(path.read_bytes()))


class Elf:
    def __init__(self, path):
        self.path = path
        self.raw = path.read_bytes()
        assert self.raw[:7] == b'\x7fELF\x01\x01\x01'
        offset = struct.unpack_from('<I', self.raw, 28)[0]
        size, count = struct.unpack_from('<HH', self.raw, 42)
        self.loads = [struct.unpack_from('<8I', self.raw, offset + i * size)
                      for i in range(count)]
        self.loads = [row for row in self.loads if row[0] == 1]
        self.names = {}
        for line in subprocess.check_output(
                [TOOL + 'nm', '-S', '--defined-only', path], text=True).splitlines():
            parts = line.split()
            if len(parts) == 4:
                self.names[parts[3]] = dict(address=int(parts[0], 16),
                    bytes=int(parts[1], 16), kind=parts[2])
            elif len(parts) == 3:
                self.names[parts[2]] = dict(address=int(parts[0], 16),
                    bytes=0, kind=parts[1])

    def read(self, address, size):
        for _, offset, begin, _, filesz, memsz, _, _ in self.loads:
            if begin <= address and address + size <= begin + memsz:
                at = address - begin
                present = max(0, min(size, filesz - at))
                return self.raw[offset + at:offset + at + present] + bytes(size - present)
        raise ValueError(('outside ELF loads', hex(address), size))

    def words(self, address, count):
        return list(struct.unpack('<' + 'I' * count, self.read(address, count * 4)))


class Original:
    def __init__(self, pack):
        self.cart = base.Cartridge(pack)
        self.names, self.addresses, self.images, self.config = {}, {}, {}, []
        paths = [ROOT / 'upstream/config/arm9/symbols.txt', *sorted(
            (ROOT / 'upstream/config/arm9/overlays').glob('ov*/symbols.txt'))]
        for path in paths:
            module = 0xffffffff if path.parent.name == 'arm9' else int(path.parent.name[2:])
            self.config.append(record(path))
            for line in path.read_text().splitlines():
                match = re.match(r'(\S+) kind:(\S+) addr:0x([0-9a-f]+)', line)
                if not match:
                    continue
                name, kind, address = match[1], match[2], int(match[3], 16)
                self.names[name] = dict(module=module, address=address, kind=kind)
                self.addresses.setdefault((module, address), []).append(name)

    def read(self, module, address, size):
        if module not in self.images:
            self.images[module] = self.cart.image(module)
        begin, _, data = self.images[module]
        at = address - begin
        assert 0 <= at and at + size <= len(data)
        return data[at:at + size]

    def target_names(self, module, address):
        return sorted(self.addresses.get((module, address),
            self.addresses.get((0xffffffff, address), [])))

    def code(self, name):
        row = self.names[name]
        count = int(re.search(r'size=0x([0-9a-f]+)', row['kind'])[1], 16)
        return row, self.read(row['module'], row['address'], count)


def disassemble_original(original, name):
    row, code = original.code(name)
    with tempfile.TemporaryDirectory(prefix='sm64ds-private-abi-') as directory:
        binary = Path(directory) / 'code.bin'
        binary.write_bytes(code)
        text = subprocess.check_output([TOOL + 'objdump', '-D', '-b', 'binary',
            '-m', 'arm', '--adjust-vma=' + str(row['address']), binary], text=True)
    return text, sha(code), len(code)


def disassemble_native(path, name):
    return subprocess.check_output([TOOL + 'objdump', '-dr',
        '--disassemble=' + name, path], text=True)


def instructions(text):
    result = []
    for line in text.splitlines():
        match = re.match(r'\s*([0-9a-f]+):\s+[0-9a-f]{8}\s+([^\t]+)\t?(.*)', line)
        if match:
            result.append(dict(address=int(match[1], 16),
                instruction=(match[2].strip() + ' ' + match[3].strip()).strip(),
                text=line.strip()))
    return result


def indirect_calls(text):
    rows = instructions(text)
    result = []
    for i, row in enumerate(rows):
        expression = row['instruction']
        branch = re.match(r'(?:blx|bx)\s+(r\d+|ip|sl|fp)\b', expression)
        if not branch:
            continue
        register = branch[1]
        previous = rows[max(0, i - 10):i]
        load = next((item for item in reversed(previous) if re.match(
            r'ldr\s+' + register + r',\s*\[' + register + r',\s*#\d+\]',
            item['instruction'])), None)
        # Some compilers select a target into a register different from vptr.
        if load is None:
            load = next((item for item in reversed(previous) if re.match(
                r'ldr\s+' + register + r',\s*\[\w+,\s*#\d+\]',
                item['instruction'])), None)
        offset = int(re.search(r'#(\d+)', load['instruction'])[1]) if load else None
        result.append(dict(branch_address=hex(row['address']),
            slot_bytes=offset, slot_index=offset // 4 if offset is not None else None,
            target_register=register, instruction=row['text'],
            context=[item['text'] for item in rows[max(0, i - 6):i + 2]]))
    return result


def other_nonreturn_indirect_pc_transfers(text):
    # The inspected ARM code uses BLX/BX register dispatch. Refuse to silently
    # miss a future load-to-PC or MOV-to-PC dispatch in this fixed scope.
    result = []
    for row in instructions(text):
        if re.match(r'ldr\w*\s+pc\b', row['instruction']) or re.match(
                r'mov\w*\s+pc,\s*(?!lr\b)\w+', row['instruction']):
            result.append(row['text'])
    return result


def tables(original, elf):
    result = []
    for symbol, count in TABLES:
        old = original.names[symbol]
        native = elf.names[symbol]
        assert native['kind'] in 'RV' and native['bytes'] == (count + 2) * 4
        prefix = elf.words(native['address'], 2)
        assert prefix == [0, elf.names['_ZTI' + symbol[4:]]['address']]
        raw = original.read(old['module'], old['address'], count * 4)
        values = struct.unpack('<' + 'I' * count, raw)
        native_values = elf.words(native['address'] + 8, count)
        cases = []
        for i, (address, target) in enumerate(zip(values, native_values)):
            names = original.target_names(old['module'], address)
            exact = [name for name in names if name in elf.names and
                     elf.names[name]['address'] == target]
            assert exact, (symbol, i, names, hex(target))
            cases.append(dict(slot=i, byte_offset=i * 4,
                original_address=hex(address), expected_names=names,
                native_address=hex(target), matching_native_names=exact,
                method_identity_equal=True))
        result.append(dict(symbol=symbol, original_module=hex(old['module']),
            original_address_point=hex(old['address']), original_slots_sha256=sha(raw),
            native_header_address=hex(native['address']),
            native_address_point=hex(native['address'] + 8),
            prefix=list(map(hex, prefix)), native_has_two_word_GCC_prefix=True,
            native_table_sha256=sha(elf.read(native['address'], native['bytes'])),
            slots=cases, status='passed'))
    return result


def bare_fader_fixup_evidence(original, elf):
    # Static read of the exact ELF fixup recipe. No runtime or restoration call.
    begin = elf.names['sm64ds_runtime_sections']['address']
    end = elf.names['sm64ds_runtime_sections_end']['address']
    sections = [elf.words(at, 5) for at in range(begin, end, 20)]
    begin = elf.names['sm64ds_runtime_fixups']['address']
    end = elf.names['sm64ds_runtime_fixups_end']['address']
    fixups = {tuple(elf.words(at, 4)[:2]): elf.words(at, 4)[2:]
              for at in range(begin, end, 16)}
    result = []
    for alias in ['data_0208eacc', 'data_0208eb2c', 'data_0208ea6c']:
        address = elf.names[alias]['address']
        index, section = next((i, row) for i, row in enumerate(sections)
            if row[0] <= address and address + 40 <= row[0] + row[3])
        old = original.names.get(alias)
        if old is None:
            old = dict(module=0xffffffff, address=int(alias[-8:], 16))
        slots = []
        for i in range(10):
            offset = address - section[0] + i * 4
            expected, replacement = fixups[(index, offset)]
            raw = struct.unpack('<I', original.read(old['module'], old['address'] + i * 4, 4))[0]
            assert raw == expected and replacement
            names = original.target_names(old['module'], raw)
            assert any(elf.names.get(name, {}).get('address') == replacement for name in names)
            slots.append(dict(slot=i, original_names=names,
                native_fixup_target=hex(replacement), byte_offset=i * 4,
                method_identity_equal=True))
        result.append(dict(alias=alias, native_address=hex(address),
            kind=elf.names[alias]['kind'], address_point_has_no_GCC_header=True,
            evidence='Original ten-slot ROM array plus the same ELF section/fixup recipe; '
                'static expected restoration, not initialized RAM or Boot execution.',
            slots=slots, status='passed'))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, default=ROOT /
        'build/native-integration-check/candidate-vptr-file-camera-tls/sm64ds-native.elf')
    parser.add_argument('--report', type=Path, default=ROOT /
        'build/native-title-video/family-virtual-dispatch-audit.json')
    args = parser.parse_args()
    args.elf = args.elf.resolve()
    elf = Elf(args.elf)
    assert sha(elf.raw) == FROZEN_SHA
    original = Original(ROOT / 'build/europe.pak')
    table_rows = tables(original, elf)
    bare_rows = bare_fader_fixup_evidence(original, elf)
    core_path = ROOT / 'build/native-core/report.json'
    scene_path = ROOT / 'build/native-runtime/scene-video/report.json'
    core = {row['source']: row for row in json.loads(core_path.read_text())['entries']}
    scene = {row['source']: row for row in json.loads(scene_path.read_text())['entries']}
    bodies, observed = [], {}
    expected_slots = {'_ZN5Stage8BehaviorEv': [5], '_ZN5Stage6RenderEv': [5],
        '_ZN5Stage16CleanupResourcesEv': [1, 1], '_ZN6Camera13InitResourcesEv': [9],
        '_ZN3HUD8BehaviorEv': [5], '_ZN7Minimap8BehaviorEv': [5]}
    helper_text = disassemble_native(args.elf, 'DestroyVirt')
    helper_calls = indirect_calls(helper_text)
    assert [row['slot_index'] for row in helper_calls] == [1]
    for cls in FAMILIES:
        for method in METHODS:
            symbol = '_ZN' + cls + method + 'Ev'
            relative = 'src/' + symbol + '.cpp'
            path = ROOT / 'upstream' / relative
            text = path.read_text()
            enrolled = core[relative]
            assert sha(path.read_bytes()) == enrolled['sha256_original']
            assert enrolled['compiled']
            old_text, old_hash, old_bytes = disassemble_original(original, symbol)
            new_text = disassemble_native(args.elf, symbol)
            old_calls, new_calls = indirect_calls(old_text), indirect_calls(new_text)
            selected = scene.get(relative, enrolled)
            obj = ROOT / selected['object']
            obj_text = disassemble_native(obj, symbol)
            obj_calls = indirect_calls(obj_text)
            assert not other_nonreturn_indirect_pc_transfers(old_text)
            assert not other_nonreturn_indirect_pc_transfers(new_text)
            assert not other_nonreturn_indirect_pc_transfers(obj_text)
            slots = expected_slots.get(symbol, [])
            assert [row['slot_index'] for row in old_calls] == slots
            if symbol == '_ZN5Stage16CleanupResourcesEv':
                # GCC outlined the shared local destructor dispatch helper.
                assert not new_calls and not obj_calls
                assert new_text.count('<DestroyVirt>') == 2
                object_helper = indirect_calls(disassemble_native(obj, 'DestroyVirt'))
                assert [row['slot_index'] for row in object_helper] == [1]
                effective = helper_calls * 2
                effective_obj = object_helper * 2
            else:
                assert [row['slot_index'] for row in new_calls] == slots
                assert [row['slot_index'] for row in obj_calls] == slots
                effective, effective_obj = new_calls, obj_calls
            row = dict(symbol=symbol, family=cls[1:], method=method.lstrip('0123456789'),
                source=record(path), original=dict(**original.names[symbol],
                    code_bytes=old_bytes, code_sha256=old_hash, virtual_calls=old_calls),
                frozen_ELF=dict(address=hex(elf.names[symbol]['address']),
                    bytes=elf.names[symbol]['bytes'], virtual_calls_in_body=new_calls,
                    effective_virtual_calls=effective),
                current_object=dict(**record(obj), compiled=True,
                    selected_provider='native_scene_video' if relative in scene else 'native_core',
                    effective_virtual_calls=effective_obj),
                virtual_offsets_equal=True, status='passed')
            row['unclassified_nonreturn_indirect_pc_transfers'] = []
            if relative in scene:
                generated = ROOT / selected['generated_source']
                row['current_generated_source'] = record(generated)
                assert row['current_generated_source']['sha256'] == selected['sha256_generated']
                assert row['current_object']['sha256'] == selected['sha256_object']
            if symbol == '_ZN6Camera6RenderEv':
                target = original.names['_ZN4View6RenderEv']['address']
                assert re.search(r'\bbl\s+0x0*' + format(target, 'x') + r'\b', old_text)
                assert '<_ZN4View6RenderEv>' in new_text
                assert '_ZN4View6RenderEv' in obj_text
                row['qualified_base_call'] = dict(expression='View::Render()',
                    direct_original_and_frozen_ELF_and_current_object=True,
                    target='_ZN4View6RenderEv', source_line=179,
                    no_recursive_virtual_dispatch=True)
            bodies.append(row)
            observed[symbol] = row
    cases = [
        ('_ZN6Camera13InitResourcesEv', 64, 'this->v9()', 'this: Camera', 9,
         ['_ZTV6Camera'], '_ZN6Camera6RenderEv', 'int(Camera*)', 'int(Camera*)'),
        ('_ZN5Stage8BehaviorEv', 113, 'data_0209f5bc->v5()', 'installed FaderBrightness-family', 5,
         ['_ZTV15FaderBrightness', '_ZTV10FaderColor', '_ZTV10dFdDummy_c', '_ZTV9FaderWipe'],
         '_ZN15FaderBrightness9IsAtStartEv', 'int(void*)', 'int(FaderBrightness*)'),
        ('_ZN3HUD8BehaviorEv', 31, 'data_0209f5bc->v5()', 'installed FaderBrightness-family', 5,
         ['_ZTV15FaderBrightness', '_ZTV10FaderColor', '_ZTV10dFdDummy_c', '_ZTV9FaderWipe'],
         '_ZN15FaderBrightness9IsAtStartEv', 'int(void*)', 'int(FaderBrightness*)'),
        ('_ZN7Minimap8BehaviorEv', 116, 'data_0209f5bc->vt->f[5](data_0209f5bc)',
         'installed FaderBrightness-family', 5,
         ['_ZTV15FaderBrightness', '_ZTV10FaderColor', '_ZTV10dFdDummy_c', '_ZTV9FaderWipe'],
         '_ZN15FaderBrightness9IsAtStartEv', 'int(void*)', 'int(FaderBrightness*)'),
        ('_ZN5Stage6RenderEv', 106, '(*(Renderable**)((char*)this + 0x9bc))->Draw(0)',
         'Stage+0x9bc mSkyboxModel: Model* from LoadSkybox', 5,
         ['_ZTV5Model'], '_ZN5Model6RenderEPK7Vector3', 'void(void*, int=0)',
         'void(Model*, const Vector3*=nullptr)'),
        ('_ZN5Stage16CleanupResourcesEv', 74, 'DestroyVirt((void**)mSkyboxModel)',
         'Stage+0x9bc mSkyboxModel: Model* from LoadSkybox', 1,
         ['_ZTV5Model'], '_ZN5ModelD0Ev', 'void(void*)', 'void(Model*)'),
        ('_ZN5Stage16CleanupResourcesEv', 83, 'DestroyVirt((void**)e->mTransformer)',
         'Stage+0x8bc + i*0xc: TextureTransformer* from LoadTextureTransformers', 1,
         ['_ZTV18TextureTransformer'], '_ZN18TextureTransformerD0Ev',
         'void(void*)', 'void(TextureTransformer*)')]
    resolved = []
    table_by_symbol = {row['symbol']: row for row in table_rows}
    for symbol, line, expression, receiver, slot, tables_, target, caller_abi, target_abi in cases:
        for table in tables_:
            assert target in table_by_symbol[table]['slots'][slot]['matching_native_names']
        source = ROOT / 'upstream/src' / (symbol + '.cpp')
        resolved.append(dict(caller=symbol, source=str(source.relative_to(ROOT)),
            line=line, expression=expression, receiver_provenance=receiver,
            slot_index=slot, slot_bytes=slot * 4, original_expected_method=target,
            eligible_tables=tables_, selected_method_after_GCC_header_plus8=target,
            methods_identical=True, ARM32_caller_signature=caller_abi,
            ARM32_target_signature=target_abi,
            ARM32_signature_compatible_for_actual_call=True,
            receiver_is_live_or_initialized_not_proved=True, status='passed'))
    support_paths = [
        'upstream/include/fBase_c.h', 'upstream/include/dBase_c.h',
        'upstream/include/dScene_c.h', 'upstream/include/Stage.h',
        'upstream/include/View.h', 'upstream/include/Camera.h',
        'upstream/include/HUD.h', 'upstream/include/Minimap.h',
        'upstream/include/ModelBase.h', 'upstream/include/Model.h',
        'upstream/include/Animation.h', 'upstream/include/TextureTransformer.h',
        'upstream/include/Fader.h',
        'upstream/src/_ZN8dScene_c9SetFadersEP15FaderBrightness.cpp',
        'upstream/src/_ZN8dScene_c19ResetFadersAndSoundEv.cpp',
        'upstream/src/_ZN5Stage10LoadSkyboxEv.cpp',
        'upstream/src/_ZN5Stage23LoadTextureTransformersEv.cpp',
        'upstream/src/_ZN5ModelC1Ev.cpp',
        'upstream/src/_ZN18TextureTransformerC1Ev.cpp',
        'upstream/src/func_02017278.c']
    report = dict(status='passed', scope='Read-only static virtual ABI audit of 20 original '
        'lifecycle bodies, frozen ARM11 ELF and current selected object files. No factory '
        'replay, game execution, old RAM installation, resource success, Azahar or hardware claim.',
        ELF=record(args.elf), source_reports=[record(core_path), record(scene_path)],
        tool=record(Path(__file__).resolve()), original_symbol_configs=original.config,
        support_sources=[record(ROOT / p) for p in support_paths],
        source_generated_objects_may_postdate_frozen_ELF=True,
        current_object_offsets_checked_independently=True,
        lifecycle_bodies=bodies, GCC_tables=table_rows, bare_fader_fixup_recipe=bare_rows,
        virtual_call_cases=resolved, native_outlined_helper=dict(name='DestroyVirt',
            frozen_ELF_calls=helper_calls, shared_by_two_original_cleanup_sites=True),
        summary=dict(lifecycle_bodies=20, original_virtual_calls=7,
            resolved_call_cases=7, method_identity_equal_cases=7,
            GCC_table_slots_compared=sum(len(row['slots']) for row in table_rows),
            bare_fader_fixup_slots_compared=30, observed_slot_mismatches=0,
            unknown_static_method_targets=0),
        explicit_unknowns=[
            'No live Stage RAM is used: allocation success, receiver validity and actual '
            'installed fader/skybox/transformer identity remain runtime conditions.',
            'Bare alias table targets are checked against ROM and exact ELF fixup recipes; '
            'this read-only audit does not establish that Boot restored/activated them.',
            'Current object hashes cover post-candidate Camera Render video adaptation; '
            'the frozen ELF has a distinct fixed hash and is not claimed to contain later edits.',
            'Only calls inside the named 20 bodies and their single outlined local helper '
            'are covered; transitive nonvirtual services and Stage resource completion are outside scope.'],
        conclusions=[
            'No vN or explicit slot in these bodies points to a different original method '
            'after publishing GCC address point symbol+8.',
            'Camera Init slot9 intentionally calls Camera Render, and Camera Render directly '
            'calls qualified View Render. Neither becomes a different lifecycle callback.',
            'The three data_0209f5bc slot5 gates read FaderBrightness IsAtStart; this global '
            'is installed by dScene SetFaders, not a scene actor vptr.',
            'Skybox Draw(0) is ABI-equivalent to Model Render(nullptr); Cleanup slot1 selects '
            'the original deleting destructors. Animation Advance is nonvirtual.'])
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(dict(status=report['status'], report=str(args.report),
        **report['summary'])))


if __name__ == '__main__':
    main()
