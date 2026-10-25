#!/usr/bin/env python3
"""Read original title clocks/inputs from a completed SM64RAM1 capture.

This is a bounded observer, not a game runner. ELF addresses name globals and
callbacks. Heap objects require captured original root residency; the declared
static save record/BCA tables require exact aliases and original section views.
Residency does not establish object liveness. Missing bytes stay unavailable.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import tempfile

from test_native_title_fade_arm import Snapshot
from test_native_audio_deferred_arm import base


def decode(snapshot, names):
    unavailable = []
    roots = [v for v in snapshot.views if v['name'] == 'original-root-heap']
    root = roots[0] if len(roots) == 1 else None

    def child(address, size, label, static_aliases=()):
        in_heap = bool(root and address and Snapshot.contains(root,address,size))
        in_static = bool(address and any(address == alias and size <= limit and any(
            v['name'] == 'original-section' and v['module'] == module and
                Snapshot.contains(v,address,size) for v in snapshot.views)
            for alias,limit,module in static_aliases))
        if not in_heap and not in_static:
            unavailable.append({'field': label, 'address': hex(address), 'bytes': size,
                                'reason': 'outside captured original root heap and declared static aliases, or null'})
            return False
        try:
            snapshot.read(address, size)
        except (ValueError, AssertionError, KeyError) as error:
            unavailable.append({'field': label, 'address': hex(address), 'bytes': size,
                                'reason': str(error)})
            return False
        return True

    def signed_half(address):
        return struct.unpack('<h', snapshot.read(address, 2))[0]

    def state(address, label):
        if not child(address, 16, label): return None
        return {'address': hex(address), 'current': signed_half(address),
            'pending': signed_half(address+2), 'flags': snapshot.u32(address+4),
            'previous': signed_half(address+8), 'last_transition_from': signed_half(address+10),
            'ticks': snapshot.u32(address+12)}

    ctx = snapshot.u32(names['data_ov007_0210342c'])
    lock = snapshot.u32(names['data_ov007_02104bec'])
    result = {'context': hex(ctx), 'input_lock': lock, 'unavailable': unavailable,
        'root_heap': root, 'scope': 'Captured CPU-visible memory; heap objects require root residency. '
            'Save data and BCA tables may use exact declared ELF aliases inside captured original-section views. '
            'Pointer residency is not allocation liveness.'}
    if not child(ctx, 0x188, 'context'): return result
    main, sub, cursor = [state(snapshot.u32(ctx+offset), label)
                        for offset, label in [(4, 'main'), (8, 'sub'), (12, 'cursor')]]
    result.update(main=main, sub=sub, cursor=cursor, result_code=snapshot.s32(ctx+0x10),
                  exit_flag=snapshot.s32(ctx+0x14), control_mode=snapshot.s32(ctx+0x20),
                  ec=snapshot.s32(ctx+0xec), next_ec=snapshot.s32(ctx+0xf0))
    q_wrapper = snapshot.u32(ctx+0x28)
    if child(q_wrapper, 0x3c, 'save_wrapper'):
        q = snapshot.u32(q_wrapper)
        if child(q, 0xe, 'save_predicate', ((names.get('data_0209b340'),0xe,0xffffffff),)):
            slots = list(snapshot.read(q+0xb, 3))
            word4 = snapshot.u32(q+4)
            result['no_save_predicate'] = {'address': hex(q), 'slots': slots, 'word4': word4,
                                          'aebac': int(not any(slots) and not word4)}
    touch = snapshot.u32(ctx+0x50)
    if child(touch, 0x44, 'touch'):
        result['touch'] = {'address': hex(touch), 'x': snapshot.u16(touch+8),
            'y': snapshot.u16(touch+10), 'down': snapshot.u16(touch+12),
            'previous_down': snapshot.u16(touch+20), 'held_ticks': snapshot.u32(touch+32),
            'released_ticks': snapshot.u32(touch+36), 'counter': snapshot.u32(touch+40),
            'flags': snapshot.u32(touch+44)}
    keys = snapshot.u32(ctx+0x54)
    if child(keys, 0x70, 'keys'):
        held, prior = snapshot.u16(keys), snapshot.u16(keys+2)
        result['keys'] = {'address': hex(keys), 'held': held, 'previous': prior,
                          'fresh_mask': held & ~prior}
    element = snapshot.u32(ctx+0x130)
    if child(element, 0x1c, 'touch_to_start_element'):
        obj = snapshot.u32(element)
        if child(obj, 0x14, 'touch_to_start_object'):
            result['touch_to_start_opacity'] = snapshot.s32(obj+0x10)
    wrapper = snapshot.u32(ctx+0x100)
    if child(wrapper, 0x1c, 'intro_wrapper'):
        cb = snapshot.u32(wrapper+0x18)
        result['intro_wrapper'] = {'address': hex(wrapper), 'id': snapshot.read(wrapper, 1)[0],
            'variant': snapshot.read(wrapper+1, 1)[0], 'particle_slot': snapshot.read(wrapper+2, 1)[0],
            'callback': hex(cb), 'callback_name': next((n for n, a in names.items() if a == cb), None) if cb else None}
        model = snapshot.u32(wrapper+4)
        if child(model, 0xa0, 'intro_model'):
            result['intro_model'] = {'address': hex(model), 'dirty': snapshot.u32(model+0x98),
                'flags': snapshot.u32(model+0x9c), 'draw_flag': snapshot.u32(model+0x94),
                'bca_count': snapshot.u32(model+0x10)}
            animation = snapshot.u32(model+0x18)
            if child(animation, 0x1c, 'intro_animation'):
                record = {'address': hex(animation), 'pause': snapshot.s32(animation),
                    'flags': snapshot.u32(animation+4), 'dirty': snapshot.u32(animation+8),
                    'index': signed_half(animation+12), 'previous_index': signed_half(animation+14),
                    'frame': snapshot.u16(animation+16), 'total': snapshot.u16(animation+18),
                    'fixed_frame': snapshot.s32(animation+20), 'speed': snapshot.s32(animation+24)}
                result['intro_animation'] = record
                count, table = snapshot.u32(model+0x10), snapshot.u32(model+0x14)
                static_tables = ((names.get('data_ov007_02104bc4'),8,7),
                    (names.get('data_ov007_02104bc4',0)+8,8,7))
                if count <= 256 and child(table, count*4, 'intro_BCA_table', static_tables):
                    entries = []
                    for index in range(count):
                        bca = snapshot.u32(table+index*4)
                        entry = {'index': index, 'address': hex(bca)}
                        if bca and child(bca, 6, 'intro_BCA_header'):
                            entry.update(bones=snapshot.read(bca, 1)[0],
                                frames=snapshot.u16(bca+2), flags=snapshot.read(bca+4, 1)[0])
                        entries.append(entry)
                    result['intro_BCA_headers'] = entries
                elif count > 256:
                    unavailable.append({'field': 'intro_BCA_table', 'reason': 'count exceeds bounded observer limit'})
    for label, name in [('star_touch_counter', 'data_ov007_02103340'),
                        ('star_motion_counter', 'data_ov007_02103344')]:
        result[label] = snapshot.u32(names[name])
    readiness_fields = (main, sub, result.get('touch_to_start_opacity'))
    if all(field is not None for field in readiness_fields):
        result['touch_to_start_ready'] = (main['current'] == 0 and sub['current'] == 0 and
            result['touch_to_start_opacity'] == 4096 and lock == 0)
    if sub and 'intro_animation' in result and 'no_save_predicate' in result:
        anim, no_save = result['intro_animation'], result['no_save_predicate']['aebac']
        actual = result.get('touch')
        fresh_key = result.get('keys', {}).get('fresh_mask', 0)
        fresh_touch = bool(actual and actual['down'] and not actual['previous_down'])
        active = anim['index'] != -1
        in_intro = bool(main and main['current'] == 0 and sub['current'] == 11)
        callback_wait = result['intro_wrapper']['callback_name'] == 'func_ov007_020b0da0'
        result['intro_gates'] = {
            'in_intro': in_intro,
            'generic_skip_allowed_by_save': no_save == 0,
            'generic_skip_input': bool(fresh_key or fresh_touch),
            'generic_skip_now': bool(in_intro and (fresh_key or fresh_touch) and sub['ticks'] >= 4 and
                no_save == 0 and active and anim['frame'] <= anim['total']-31),
            'star_touch_inside_rectangle': bool(actual and actual['down'] and
                96 <= actual['x'] <= 160 and 64 <= actual['y'] <= 128),
            'star_touch_may_set_flag2': bool(in_intro and callback_wait and actual and actual['down'] and
                not sub['flags'] & 2 and sub['ticks'] >= 4 and
                96 <= actual['x'] <= 160 and 64 <= actual['y'] <= 128),
            'fade3_BCA_frame_gate': in_intro and active and anim['frame'] == anim['total']-31,
            'exit_to_sub10_BCA_frame_gate': in_intro and active and anim['frame'] == anim['total']-6,
            'model_alternates_advance': bool(result['intro_model']['flags'] & 2),
            'animation_fixed_step': anim['speed'],
            'callback_is_original_wait': callback_wait,
            'callback_clear_condition_now': bool(in_intro and callback_wait and
                sub['flags'] & 1 and sub['flags'] & 2 and sub['ticks'] % 20 == 0),
            'next_twenty_tick_boundary': (sub['ticks']//20+1)*20}
    return result


def selftest():
    """Synthetic records cover field ABI and child bounds without game bytes."""
    begin, extent = 0x10000000, 0x10000
    names = {'data_ov007_0210342c': begin+0x10, 'data_ov007_02104bec': begin+0x14,
        'data_ov007_02103340': begin+0x18, 'data_ov007_02103344': begin+0x1c,
        'func_ov007_020b0da0': 0x00123400}
    ctx, main, sub, cursor, save_wrapper, save, touch, keys, element, obj, wrapper, model, animation, table, bca = [
        begin+x for x in (0x2000,0x3000,0x3100,0x3200,0x4000,0x4100,0x5000,0x5100,
                         0x6000,0x6100,0x7000,0x8000,0x9000,0xa000,0xb000)]
    memory = bytearray([0xa5])*extent
    def u32(address, value): struct.pack_into('<I', memory, address-begin, value & 0xffffffff)
    def s16(address, value): struct.pack_into('<h', memory, address-begin, value)
    for address,size in [(ctx,0x188),(main,16),(sub,16),(cursor,16),(save_wrapper,0x3c),
        (save,0xe),(touch,0x44),(keys,0x70),(element,0x1c),(obj,0x14),(wrapper,0x1c),
        (model,0xa0),(animation,0x1c),(table,8),(bca,6)]:
        memory[address-begin:address-begin+size] = bytes(size)
    for address,value in [(names['data_ov007_0210342c'],ctx),(names['data_ov007_02104bec'],1),
        (names['data_ov007_02103340'],26),(names['data_ov007_02103344'],0),
        (ctx+4,main),(ctx+8,sub),(ctx+12,cursor),(ctx+0x28,save_wrapper),(save_wrapper,save),
        (ctx+0x50,touch),(ctx+0x54,keys),(ctx+0x130,element),(element,obj),(obj+0x10,4096),
        (ctx+0x100,wrapper),(wrapper+4,model),(wrapper+0x18,names['func_ov007_020b0da0']),
        (model+0x10,2),(model+0x14,table),(model+0x18,animation),(model+0x98,1),
        (model+0x9c,2),(animation+0x14,82<<12),(animation+0x18,4096),(table+4,bca),
        (sub+4,3),(sub+12,206),(touch+0x24,2)]: u32(address,value)
    for address,value in [(main+2,-1),(sub,11),(sub+2,-1),(cursor+2,-1),
        (touch+8,128),(touch+10,96),(touch+12,1),(keys,2),(animation+12,1),
        (animation+14,-1),(animation+16,82),(animation+18,157),(bca+2,157)]: s16(address,value)
    memory[bca-begin] = 1
    initial = bytes(memory)
    payload = 32+16+28
    span = dict(id=0,name='synthetic-records',address=begin,size=extent,file_offset=payload)
    root = dict(id=0,name='original-root-heap',address=begin+0x1000,size=0xf000,
        file_offset=payload+0x1000,span=0,module=0xffffffff,original_address=0,flags=0)
    index = dict(format='SM64RAM1',version=1,byte_order='little',io_status='complete',
        binary_bytes=payload+extent,spans=[span],views=[root])
    negative_cases = []
    with tempfile.TemporaryDirectory(prefix='sm64ds-title-ram-reader-') as directory:
        path = Path(directory)/'synthetic.ram.bin'
        def capture():
            views = index['views']
            current_payload = 32+16+28*len(views)
            span['file_offset'] = current_payload
            index['binary_bytes'] = current_payload+extent
            raw = (b'SM64RAM1'+struct.pack('<6I',1,current_payload,1,len(views),current_payload+extent,0)+
                struct.pack('<4I',begin,extent,current_payload,0))
            for view in views:
                view['file_offset'] = current_payload+view['address']-begin
                raw += struct.pack('<7I',*(view[k] for k in ('address','size','file_offset','span',
                                                          'module','original_address','flags')))
            raw += memory
            path.write_bytes(raw);path.with_suffix('.json').write_text(json.dumps(index))
            snap = Snapshot(path)
            before = bytes(snap.raw)
            result = decode(snap,names)
            assert snap.raw == before and path.read_bytes() == before
            return result
        result = capture()
        assert not result['unavailable'] and not result['touch_to_start_ready']
        assert result['sub'] == dict(address=hex(sub),current=11,pending=-1,flags=3,
            previous=0,last_transition_from=0,ticks=206)
        assert result['intro_animation']['frame'] == 82 and result['intro_animation']['total'] == 157
        assert result['intro_animation']['speed'] == 4096 and result['intro_model']['bca_count'] == 2
        assert result['intro_BCA_headers'][1]['frames'] == 157 and result['keys']['fresh_mask'] == 2
        assert result['intro_gates']['in_intro'] and not result['intro_gates']['generic_skip_now']
        assert not result['intro_gates']['star_touch_may_set_flag2']
        # A still-active original wait callback at tick26 accepts a held star touch.
        u32(sub+4,1);u32(sub+12,26);s16(touch+20,1)
        result = capture()
        assert result['intro_gates']['star_touch_may_set_flag2']
        assert result['intro_gates']['generic_skip_input']  # fresh B remains present.
        u32(wrapper+0x18,0)
        assert not capture()['intro_gates']['star_touch_may_set_flag2']
        memory[:] = initial;u32(sub+12,40)
        assert capture()['intro_gates']['callback_clear_condition_now']
        u32(sub+12,41)
        assert not capture()['intro_gates']['callback_clear_condition_now']
        memory[:] = initial;memory[save-begin+0xb] = 1
        assert capture()['intro_gates']['generic_skip_now']
        s16(keys+2,2);s16(touch+20,1)
        assert not capture()['intro_gates']['generic_skip_now']
        memory[:] = initial;s16(animation+16,126)
        assert capture()['intro_gates']['fade3_BCA_frame_gate']
        assert not capture()['intro_gates']['exit_to_sub10_BCA_frame_gate']
        s16(animation+16,151)
        assert capture()['intro_gates']['exit_to_sub10_BCA_frame_gate']
        assert not capture()['intro_gates']['fade3_BCA_frame_gate']
        # Bounds checks must reject even readable CPU memory outside the logical root.
        for label,slot,value in [('context_null',names['data_ov007_0210342c'],0),
            ('context_outside_root',names['data_ov007_0210342c'],begin+0x100),
            ('context_crosses_end',names['data_ov007_0210342c'],begin+extent-0x187),
            ('main_null',ctx+4,0),('sub_crosses_end',ctx+8,begin+extent-15),
            ('save_outside_root',save_wrapper,begin+0x100),
            ('touch_crosses_end',ctx+0x50,begin+extent-0x43),
            ('keys_crosses_end',ctx+0x54,begin+extent-0x6f),
            ('element_crosses_end',ctx+0x130,begin+extent-0x1b),
            ('object_crosses_end',element,begin+extent-0x13),
            ('model_crosses_end',wrapper+4,begin+extent-0x9f),
            ('animation_crosses_end',model+0x18,begin+extent-0x1b),
            ('table_crosses_end',model+0x14,begin+extent-7),('table_count_bound',model+0x10,257),
            ('BCA_crosses_end',table+4,begin+extent-5)]:
            memory[:] = initial;u32(slot,value)
            result = capture()
            assert result['unavailable'], label
            negative_cases.append(label)
        memory[:] = initial;s16(sub,0);u32(names['data_ov007_02104bec'],0)
        result = capture()
        assert result['touch_to_start_ready'] and not result['intro_gates']['in_intro']
        assert not result['intro_gates']['generic_skip_now']
        assert not result['intro_gates']['star_touch_may_set_flag2']
        # Original RAM uses a static save record and a two-pointer static table.
        # Exact aliases plus their original-section modules allow only those
        # declarations; another readable static pointer must stay unavailable.
        memory[:] = initial
        static_save,static_table = begin+0x100,begin+0x200
        memory[0x100:0x10e] = bytes(14)
        memory[0x200:0x208] = struct.pack('<2I',0,bca)
        names['data_0209b340'] = static_save
        names['data_ov007_02104bc4'] = static_table
        u32(save_wrapper,static_save);u32(model+0x14,static_table)
        index['views'].extend([
            dict(id=1,name='original-section',address=static_save,size=0x100,file_offset=0,
                span=0,module=0xffffffff,original_address=0x0209b340,flags=1),
            dict(id=2,name='original-section',address=static_table,size=0x100,file_offset=0,
                span=0,module=7,original_address=0x02104bc4,flags=1)])
        result = capture()
        assert not result['unavailable'] and result['no_save_predicate']['aebac'] == 1
        assert result['intro_BCA_headers'][1]['frames'] == 157
        u32(save_wrapper,static_save+4)
        assert any(f['field']=='save_predicate' for f in capture()['unavailable'])
        negative_cases.append('static_save_wrong_alias')
        u32(save_wrapper,static_save);index['views'][1]['module']=7
        assert any(f['field']=='save_predicate' for f in capture()['unavailable'])
        negative_cases.append('static_save_wrong_module')
        index['views'][1]['module']=0xffffffff;u32(model+0x10,3)
        assert any(f['field']=='intro_BCA_table' for f in capture()['unavailable'])
        negative_cases.append('static_table_exceeds_declared_extent')
        index['views'][:] = [root]
        memory[:] = initial;root['name']='unnamed-root'
        result = capture()
        assert result['unavailable'] and 'intro_animation' not in result
        negative_cases.append('missing_logical_root')
    return dict(status='passed',synthetic_fixture=True,negative_cases=negative_cases,
        fields_cover_context_states_save_input_model_animation_BCA=True,
        intro_and_ready_phase_gates_checked=True,reader_does_not_mutate_bytes=True,
        save_touch_callback_and_BCA_boundaries_checked=True,
        pointer_residency_does_not_prove_liveness=True,
        source_sha256={str(path):hashlib.sha256(path.read_bytes()).hexdigest()
            for path in (Path(__file__).resolve(),Path(__file__).with_name('test_native_title_fade_arm.py').resolve())})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--snapshot', type=Path)
    parser.add_argument('--index', type=Path)
    parser.add_argument('--elf', type=Path)
    parser.add_argument('--report', type=Path)
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        report = selftest()
        output = json.dumps(report,indent=2)+'\n'
        if args.report:
            args.report.parent.mkdir(parents=True,exist_ok=True)
            args.report.write_text(output)
        print(output)
        return
    if not args.snapshot or not args.elf:
        parser.error('--snapshot and --elf are required unless --self-test is used')
    snapshot = Snapshot(args.snapshot, args.index)
    report = {'snapshot': str(args.snapshot.resolve()),
        'snapshot_sha256': hashlib.sha256(args.snapshot.read_bytes()).hexdigest(),
        'elf': str(args.elf.resolve()), 'elf_sha256': hashlib.sha256(args.elf.read_bytes()).hexdigest(),
        'title': decode(snapshot, base.symbols(args.elf))}
    output = json.dumps(report, indent=2)+'\n'
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(output)
    print(output)


if __name__ == '__main__':
    main()
