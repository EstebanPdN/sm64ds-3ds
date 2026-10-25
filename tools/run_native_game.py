#!/usr/bin/env python3
"""One bounded run of the integrated original game in an isolated Old Azahar."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time
from verify_native_3dsx import verify as verify_image_pair


def configure_keyboard_touch(profile):
    """Bind E to ordinary emulator HID touch at the lower panel's center.

    Azahar 2126.1.2 reads these QSettings arrays in QtConfig::ReadControlValues
    and TouchFromButtonDevice. This changes only the isolated emulator profile;
    no game variable, save, scene, animation or input log is synthesized.
    """
    settings = {
        r'profiles\1\use_touch_from_button': 'true',
        r'profiles\1\use_touch_from_button\default': 'false',
        r'profiles\1\touch_from_button_map': '0',
        r'profiles\1\touch_from_button_map\default': 'false',
        r'touch_from_button_maps\size': '1',
        r'touch_from_button_maps\1\name': 'diagnostic-screen-center',
        r'touch_from_button_maps\1\name\default': 'false',
        r'touch_from_button_maps\1\entries\size': '1',
        r'touch_from_button_maps\1\entries\1\bind': '"code:69,engine:keyboard,x:160,y:120"',
        r'touch_from_button_maps\1\entries\1\bind\default': 'false',
    }
    source = profile.read_text()
    controls = re.search(r'(?ms)^\[Controls\]\n(.*?)(?=^\[|\Z)', source)
    body = controls[1] if controls else ''
    # Replace only this profile's explicit keys, preserving unrelated settings.
    for key, value in settings.items():
        pattern = r'(?m)^' + re.escape(key) + r'=.*$'
        if re.search(pattern, body):
            body = re.sub(pattern, lambda _: key + '=' + value, body)
        else:
            body = body.rstrip('\n') + '\n' + key + '=' + value + '\n'
    if controls:
        source = source[:controls.start(1)] + body + source[controls.end(1):]
    else:
        source = source.rstrip('\n') + '\n\n[Controls]\n' + body.lstrip('\n')
    profile.write_text(source)
    return {'key': 'E', 'qt_key_code': 69, 'HID_touch_3DS': [160, 120],
            'original_DS_touch': [128, 96], 'delivered_input_verified': False}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--azahar',type=Path,required=True)
    parser.add_argument('--seconds',type=int,default=30)
    parser.add_argument('--startup-seconds',type=int,default=20,
                        help='Separate bounded allowance for emulator startup/dialogs')
    parser.add_argument('--snapshot-grace-seconds',type=int,default=30,
                        help='Bounded extra allowance only for an in-progress RAM write')
    parser.add_argument('--keyboard-touch',action='store_true',
                        help='Bind E to normal HID touch at the visible lower-screen center in the isolated profile')
    args=parser.parse_args()
    if not 1<=args.seconds<=360:parser.error('Bound each acquisition to 1..360 seconds')
    if not 1<=args.startup_seconds<=60:parser.error('Bound startup to 1..60 seconds')
    if not 0<=args.snapshot_grace_seconds<=60:parser.error('Bound snapshot I/O grace to 0..60 seconds')
    root=Path(__file__).resolve().parents[1]
    # The ELF and packaging are separate CMake targets. Verify their actual
    # bytes before clearing evidence or launching, so RAM has the exact ELF.
    image_pair=verify_image_pair(root/'build/3ds/source/ctr/sm64ds-native.elf',
        root/'build/3ds/source/ctr/sm64ds-native.3dsx')
    runtime=root/'build/native-runtime-azahar'
    config=runtime/'config/azahar-emu';data=runtime/'data/azahar-emu'
    sd=data/'sdmc/3ds/sm64ds/native'
    for path in (config,sd,runtime/'cache'):path.mkdir(parents=True,exist_ok=True)
    profile=config/'qt-config.ini'
    if not profile.exists():
        profile.write_text('[System]\nis_new_3ds=false\nis_new_3ds\\default=false\n'
            '[UI]\nfirstStart=false\ncalloutFlags=4294967295\n'
            '[Miscellaneous]\ncheck_for_update_on_start=false\ncheck_for_update_on_start\\default=false\n'
            '[Layout]\nrender_3d=0\nrender_3d\\default=false\nfactor_3d=75\nfactor_3d\\default=false\n'
            '[WebService]\nenable_telemetry=false\n')
    touch_binding=configure_keyboard_touch(profile) if args.keyboard_touch else None
    pack=root/'build/europe.pak';target=sd/'assets.pak'
    if not target.exists() or target.stat().st_size!=pack.stat().st_size:shutil.copyfile(pack,target)
    attempt=runtime/('attempt-'+str(time.time_ns()));attempt.mkdir()
    shutil.copyfile(profile,attempt/'azahar-profile.ini')
    profile_digest=hashlib.sha256((attempt/'azahar-profile.ini').read_bytes()).hexdigest()
    executable=attempt/'sm64ds-native.3dsx'
    shutil.copyfile(root/'build/3ds/source/ctr/sm64ds-native.3dsx',executable)
    shutil.copyfile(root/'build/3ds/source/ctr/sm64ds-native.elf',attempt/'sm64ds-native.elf')
    digest=hashlib.sha256(executable.read_bytes()).hexdigest()
    elf_digest=hashlib.sha256((attempt/'sm64ds-native.elf').read_bytes()).hexdigest()
    previous=runtime/'latest.json'
    startup_retry_of=None
    if previous.exists():
        prior=json.loads(previous.read_text())
        if prior['image_sha256']==digest:
            # A bounded launch that never produced the first game record is
            # not a game acquisition. Permit that launch to be recovered,
            # while retaining the no-repeat guard for every actual run.
            # The terminated process can flush its three platform headers
            # during shutdown, after the startup deadline. Those headers do
            # not establish ROM publication, a game tick or a RAM acquisition.
            # Retain the whole failed attempt and reject every later record.
            startup_headers=(
                'SM64DS general native integration; original boot/scene/actor loop',
                'Old3DS baseline; stereo=disabled audio=disabled wireless=disabled physical_hardware=unverified',
                'native platform: single context; audio unavailable; physical verification pending')
            recorded_lines=prior.get('application_log','').splitlines()
            only_startup_headers=(not recorded_lines or
                recorded_lines==list(startup_headers[:len(recorded_lines)]))
            if (prior.get('stop_reason')=='startup_time_bound' and
                    prior.get('game_seconds') is None and
                    only_startup_headers and
                    not prior.get('first_emulator_fault') and
                    not prior.get('ram_snapshot_files')):
                startup_retry_of=prior['attempt']
            else:
                raise SystemExit('This exact executable was already attempted; change the identified blocker before rerunning.')
    logfile=sd/'runtime.log'
    if logfile.exists():logfile.unlink()
    scanout=sd/'boot-scanout.bin'
    if scanout.exists():scanout.unlink()
    video_dump=sd/'boot-video.bin'
    if video_dump.exists():video_dump.unlink()
    capture_files=('capture-first.rgba','capture-first-vram.bin','capture-first.json')
    scene_files=tuple(prefix+suffix for prefix in ('scene-1-first','scene-1-visible','scene-1-ready','scene-3-requested')
        for suffix in ('-gx.bin','-scanout.bin','-video.bin','.json',
                       '-textures.bin','-palettes.bin','-geometry.json'))
    for name in capture_files+scene_files+('input.log','title-state.log'):
        if (sd/name).exists():(sd/name).unlink()
    # Each prior attempt owns its archived RAM files. Clear only generated
    # acquisition files in this isolated diagnostic SD folder before launch.
    for snapshot in sd.glob('ram-*.bin'):
        snapshot.unlink()
    for snapshot in sd.glob('ram-*.json'):
        snapshot.unlink()
    snapshot_request=sd/'ram-snapshot-request'
    if snapshot_request.exists():snapshot_request.unlink()
    emulator_log=data/'log/azahar_log.txt'
    if emulator_log.exists():emulator_log.unlink()
    emulator_offset=0;fault=None
    env={**os.environ,'XDG_CONFIG_HOME':str(runtime/'config'),
         'XDG_DATA_HOME':str(runtime/'data'),'XDG_CACHE_HOME':str(runtime/'cache')}
    began=time.monotonic();game_began=None;reason='process_exit';final_snapshot_requested=False
    with (attempt/'process.log').open('w') as output:
        child=subprocess.Popen([str(args.azahar.resolve()),'-w',str(executable)],cwd=runtime,
                               env=env,stdout=output,stderr=subprocess.STDOUT)
        (runtime/'pid').write_text(str(child.pid))
        print(f'Integrated game attempt PID {child.pid}: {attempt}',flush=True)
        while child.poll() is None:
            app_log=logfile.read_text(errors='replace') if logfile.exists() else ''
            now=time.monotonic()
            if game_began is None and app_log:
                game_began=now
            if 'STOP:' in app_log or 'exit=' in app_log:
                reason='application_terminal_record';break
            if emulator_log.exists():
                with emulator_log.open('r',errors='replace') as stream:
                    stream.seek(emulator_offset);new=stream.read(1024*1024);emulator_offset=stream.tell()
                for line in new.splitlines():
                    if 'UnmappedAccess:' in line or 'Exception Type:' in line:
                        fault=line;break
                if fault:reason='emulator_memory_or_cpu_fault';break
            if game_began is None and now-began>=args.startup_seconds:
                reason='startup_time_bound';break
            # Ask the instrumented target for one final synchronous snapshot
            # while it still has time to finish, without synthesizing game input.
            if (game_began is not None and not final_snapshot_requested and
                    now-game_began>=max(0,args.seconds-20)):
                snapshot_request.write_text('final acquisition checkpoint\n')
                final_snapshot_requested=True
            if game_began is not None and now-game_began>=args.seconds:
                writing_ram=app_log.count('ram snapshot begin ')>app_log.count('ram snapshot end ')
                if writing_ram and now-game_began<args.seconds+args.snapshot_grace_seconds:
                    time.sleep(.2);continue
                reason='time_bound';break
            time.sleep(.2)
        observed_until=time.monotonic()
        if child.poll() is None:
            if fault:
                # A faulting guest can emit millions of repeated accesses
                # while Qt waits for a graceful close. Preserve the first
                # fault and stop the diagnostic process immediately.
                child.kill();child.wait()
            else:
                child.terminate()
                try:child.wait(timeout=3)
                except subprocess.TimeoutExpired:child.kill();child.wait()
    result={'scope':'Integrated original game boot on Old-profile Azahar',
        'image_pair_verification':image_pair,
        'image_sha256':digest,'elf_sha256':elf_digest,'model':'old','physical_hardware':False,
        'startup_retry_of':startup_retry_of,
        'azahar_profile_sha256':profile_digest,'requested_touch_binding':touch_binding,
        'visual_verified':False,'playability_verified':False,
        'elapsed_seconds':time.monotonic()-began,'stop_reason':reason,
        'startup_seconds':None if game_began is None else game_began-began,
        'game_seconds':None if game_began is None else observed_until-game_began,
        'shutdown_seconds':time.monotonic()-observed_until,
        'startup_bound_seconds':args.startup_seconds,'game_bound_seconds':args.seconds,
        'snapshot_grace_bound_seconds':args.snapshot_grace_seconds,
        'final_ram_snapshot_requested':final_snapshot_requested,
        'first_emulator_fault':fault,
        'process_exit_code':child.returncode,'attempt':str(attempt),
        'application_log':logfile.read_text(errors='replace') if logfile.exists() else None}
    if logfile.exists():shutil.copyfile(logfile,attempt/'runtime.log')
    if scanout.exists():
        shutil.copyfile(scanout,attempt/'boot-scanout.bin')
        result['cpu_scanout_bytes']=scanout.stat().st_size
    if video_dump.exists():shutil.copyfile(video_dump,attempt/'boot-video.bin')
    captured={}
    for name in capture_files:
        if (sd/name).exists():
            shutil.copyfile(sd/name,attempt/name)
            captured[name]={'bytes':(sd/name).stat().st_size,
                           'sha256':hashlib.sha256((sd/name).read_bytes()).hexdigest()}
    if captured:result['original_capture_files']=captured
    scene_evidence={}
    for name in scene_files:
        if (sd/name).exists():
            shutil.copyfile(sd/name,attempt/name)
            scene_evidence[name]={'bytes':(sd/name).stat().st_size,
                                 'sha256':hashlib.sha256((sd/name).read_bytes()).hexdigest()}
    first_evidence={name:value for name,value in scene_evidence.items()
                    if name.startswith('scene-1-first')}
    visible_evidence={name:value for name,value in scene_evidence.items()
                      if name.startswith('scene-1-visible')}
    ready_evidence={name:value for name,value in scene_evidence.items()
                    if name.startswith('scene-1-ready')}
    stage_evidence={name:value for name,value in scene_evidence.items()
                    if name.startswith('scene-3-requested')}
    if first_evidence:result['first_scene_frame_files']=first_evidence
    if visible_evidence:result['visible_scene_frame_files']=visible_evidence
    if ready_evidence:result['ready_scene_frame_files']=ready_evidence
    if stage_evidence:result['stage_requested_frame_files']=stage_evidence
    if (sd/'input.log').exists():
        shutil.copyfile(sd/'input.log',attempt/'input.log')
        result['consumed_input_log']=(sd/'input.log').read_text(errors='replace')
    if (sd/'title-state.log').exists():
        shutil.copyfile(sd/'title-state.log',attempt/'title-state.log')
        result['title_state_log']=(sd/'title-state.log').read_text(errors='replace')
    ram_files={}
    for snapshot in sorted((*sd.glob('ram-*.bin'),*sd.glob('ram-*.json'))):
        shutil.copyfile(snapshot,attempt/snapshot.name)
        checksum=hashlib.sha256()
        with snapshot.open('rb') as memory_file:
            for chunk in iter(lambda:memory_file.read(1024*1024),b''):
                checksum.update(chunk)
        ram_files[snapshot.name]={'bytes':snapshot.stat().st_size,'sha256':checksum.hexdigest()}
        if snapshot.suffix=='.json':
            try:
                ram_files[snapshot.name]['manifest']=json.loads(snapshot.read_text())
            except (ValueError,UnicodeError):
                ram_files[snapshot.name]['manifest_parse_error']=True
    if ram_files:result['ram_snapshot_files']=ram_files
    if emulator_log.exists():shutil.copyfile(emulator_log,attempt/'azahar_log.txt')
    text=json.dumps(result,indent=2)+'\n'
    (attempt/'result.json').write_text(text);previous.write_text(text)
    # Full manifests and logs remain in the immutable result. Keep terminal
    # output small even when four broad memory acquisitions index 226 sections.
    summary={key:result[key] for key in ('attempt','image_sha256','elf_sha256','stop_reason',
        'startup_seconds','game_seconds','shutdown_seconds','first_emulator_fault',
        'process_exit_code','final_ram_snapshot_requested')}
    summary['report']=str(attempt/'result.json')
    summary['ram_snapshots']={name:{'bytes':entry['bytes'],
        'status':entry.get('manifest',{}).get('io_status'),
        'tick':entry.get('manifest',{}).get('tick'),
        'spans':len(entry.get('manifest',{}).get('spans',[]))}
        for name,entry in ram_files.items() if name.endswith('.json')}
    summary['application_tail']=(result['application_log'] or '').splitlines()[-12:]
    print(json.dumps(summary,indent=2))
    return 0 if result['application_log'] else 1


if __name__=='__main__':raise SystemExit(main())
