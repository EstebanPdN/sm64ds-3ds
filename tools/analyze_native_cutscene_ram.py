#!/usr/bin/env python3
"""Read intro controller and camera admission fields from exact-ELF snapshots.

Requires the separately audited actor list. Reads resident owned fields only;
does not resume the game, patch state, establish complete initialization or
infer every cause from RAM. Binary payloads remain private.
"""
import argparse
import json
from pathlib import Path
import struct

from test_native_title_fade_arm import Snapshot
from test_native_menu_callbacks import symbols,sha


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--attempt',type=Path,required=True)
    p.add_argument('--actors',type=Path,required=True)
    p.add_argument('--report',type=Path,required=True)
    args=p.parse_args();assert not args.report.exists()
    result=json.loads((args.attempt/'result.json').read_text())
    elf=args.attempt/'sm64ds-native.elf';assert sha(elf.read_bytes())==result['elf_sha256']
    actors=json.loads(args.actors.read_text());assert actors['status']=='PASS'
    assert actors['ELF_sha256']==result['elf_sha256']
    names=symbols(elf);rows=[]
    for observation in actors['observations']:
        binary=args.attempt/observation['capture']
        assert sha(binary.read_bytes())==result['ram_snapshot_files'][binary.name]['sha256']
        snap=Snapshot(binary);root=next(v for v in snap.views if v['name']=='original-root-heap')
        def resident(address,size):assert address and Snapshot.contains(root,address,size)
        controllers=[]
        for actor in observation['actors']:
            if actor['vtable']!='_ZTV14CutsceneObject':continue
            entry=dict(address=actor['address'],unique_id=actor['unique_id'],parameter=actor['param1'],
                alive_state=actor['alive_state'],pause_flags=actor['pause_flags'])
            obj=int(actor['address'],16);resident(obj,0x104)
            entry['script_state_byte_100']=snap.read(obj+0x100,1)[0]
            if int(actor['param1'],16)==0x2f:
                at=snap.u32(obj+0xd4);resident(at,0x38)
                entry['intro_controller']=dict(address=hex(at),state=snap.read(at+0x34,1)[0],
                    fade_timer=snap.u16(at+0x2c),remaining_fade_steps=snap.u16(at+0x2e),
                    signed_scroll=struct.unpack('<4h',snap.read(at,8)),
                    alpha_pair=struct.unpack('<2H',snap.read(at+8,4)),
                    cloud_timers=struct.unpack('<3H',snap.read(at+0x24,6)),
                    active_clouds=list(snap.read(at+0x30,3)),sha256=sha(snap.read(at,0x38)))
            controllers.append(entry)
        registers=snap.video_view(0x04000000)['address']
        video=dict(DISPCNT_sub=hex(snap.u32(registers+0x1000)),
            BLDCNT_sub=hex(snap.u16(registers+0x1050)),
            BLDALPHA_sub=hex(snap.u16(registers+0x1052)),BLDY_sub=snap.u16(registers+0x1054),
            layer_flags=snap.read(names['data_0209d454'],1)[0])
        camera=snap.u32(names['data_0209f318']);camera_state=None
        if camera:
            resident(camera,0x1a2);target=snap.u32(camera+0x110)
            camera_state=dict(address=hex(camera),target=hex(target),
                original_state_pointer=hex(snap.u32(camera+0x138)),
                flags_154=hex(snap.u32(camera+0x154)),secondary_target=hex(snap.u32(camera+0x118)),
                transition_counter_1a0=snap.u16(camera+0x1a0),
                active_Kuppa_script_pointer=hex(snap.u32(names['data_0209fc48'])),
                global_modes=[snap.read(names[n],1)[0] for n in ['data_0209f294','data_0209f2c4','data_0209f20c']],
                global_camera_flags=hex(snap.u32(names['data_0209b454'])))
        script=snap.u32(names['data_0209fc48']);script_frame=snap.u16(names['data_0209b274'])
        script_info=None
        if script:
            cursor=script;commands=[]
            for _ in range(1024):
                length=snap.read(cursor,1)[0]
                if length==0:break
                assert length>=6,'invalid Kuppa command header'
                opcode=snap.read(cursor+1,1)[0]
                begin,end=struct.unpack('<2h',snap.read(cursor+2,4))
                snap.read(cursor,length) # Verify complete residency, not just header.
                row=dict(address=hex(cursor),bytes=length,opcode=opcode,begin_frame=begin,end_frame=end,
                    schedule='future' if begin>script_frame else
                        ('active' if end<0 or end>=script_frame else 'elapsed'))
                if opcode==6:
                    assert length>=10;row['next_script_pointer']=hex(snap.u32(cursor+6))
                commands.append(row);cursor+=length
            else:raise AssertionError('Kuppa command list lacks bounded terminator')
            finite=[c['end_frame'] for c in commands if c['end_frame']>=0]
            script_info=dict(address=hex(script),frame=script_frame,
                bytes_including_terminator=cursor+1-script,
                sha256=sha(snap.read(script,cursor+1-script)),commands=commands,
                latest_finite_end_frame=max(finite) if finite else None,
                limitations='Scheduled bounds do not prove future execution or termination; transitions/input commands may alter flow')
        rows.append(dict(capture=binary.name,tick=snap.metadata['tick'],
            presentation=snap.metadata.get('presentation'),controllers=controllers,
            video=video,camera_admission_fields=camera_state,Kuppa_script=script_info,
            original_pause_word=hex(snap.u32(names['data_020a0db0']))))
    report=dict(status='read_only_observations_complete',ELF_sha256=result['elf_sha256'],
        audited_actor_report_sha256=sha(args.actors.read_bytes()),observations=rows,
        emulator_first_fault=result['first_emulator_fault'],game_executed=False,RAM_modified=False,
        limitations=['captured checkpoints only','not full controller/camera/player acceptance',
                     'no live PC/GPR capture','no hardware or FPS proof'])
    args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(status=report['status'],snapshots=len(rows),report=str(args.report))))


if __name__=='__main__':main()
