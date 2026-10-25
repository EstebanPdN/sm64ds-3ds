#!/usr/bin/env python3
"""Acquire one fully built integrated candidate in an independent Azahar.

Requires data/link/control gates before launch. Copies only candidate SD files
and the locally available DSP firmware into a new private emulator profile.
No personal saves/settings, global process kill, emulated overclock or FPS claim.
"""
import argparse
import fcntl
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import time

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/integrated-native'


def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def copy_verified_cache(prior,candidate,sd):
    """Exercise renamed-ROM reuse without repeating a proven first extraction."""
    prior=prior.resolve();closed=json.loads((prior/'result.json').read_text())
    assert closed['status']=='acquisition_closed' and closed['first_emulator_fault'] is None
    manifest=json.loads((candidate/'source-evidence/rom-install.json').read_text())
    prior_build=json.loads((Path(closed['candidate'])/'BUILD.json').read_text())
    prior_root=prior_build.get('rom_install',{}).get('directory','sdmc:/sm64ds').removeprefix('sdmc:/')
    relative=Path('user/sdmc')/prior_root/'cache'/manifest['schema_id']/'repo'
    source=prior/relative;assert source.is_dir() and not source.is_symlink()
    expected={item['path']:item for item in manifest['files']}
    assert {p.relative_to(source).as_posix() for p in source.rglob('*') if p.is_file()}==set(expected)|{'.ready'}
    for name,item in expected.items():
        file=source/name;assert file.is_file() and not file.is_symlink()
        assert all(not parent.is_symlink() for parent in file.parents if parent.is_relative_to(source))
        actual=digest(file);record=closed['files'][(relative/name).as_posix()]
        assert file.stat().st_size==item['size'] and actual==item['sha256']==record['sha256']
    marker=source/'.ready';assert not marker.is_symlink()
    assert marker.read_text()=='SM64DS-ROM-CACHE-1\n'+manifest['schema_id']+'\n'+manifest['metadata_sha256']+'\n'
    assert digest(marker)==closed['files'][(relative/'.ready').as_posix()]['sha256']
    current_build=json.loads((candidate/'BUILD.json').read_text())
    current_root=current_build['rom_install'].get('directory','sdmc:/sm64ds').removeprefix('sdmc:/')
    destination=sd/current_root/'cache'/manifest['schema_id']/'repo'
    assert not destination.exists();destination.parent.mkdir(parents=True,exist_ok=True)
    shutil.copytree(source,destination)
    return dict(source_attempt=str(prior),source_result_sha256=digest(prior/'result.json'),
        files_checked=len(expected),bytes_checked=sum(x['size'] for x in expected.values()),
        scope='Existing cache copied only after exact frozen per-file hashes and complete marker checks; this run tests reuse, not first extraction')


def stop_owned(runner,process):
    """Retain the vendor identity checks; resolve Darwin's empty-group EPERM."""
    try:runner.stop_owned(process,process.pid)
    except PermissionError as error:
        if sys.platform!='darwin' or not isinstance(process,runner.OwnedOrphan):raise
        # A missing leader alone says nothing about its remaining children.
        # Never accept EPERM while the exact group still contains a process.
        current=runner.process_identity(process.pid)
        table=subprocess.check_output(['ps','-axo','pid=,pgid='],text=True)
        members=[int(fields[0]) for line in table.splitlines()
                 if len(fields:=line.split())==2 and int(fields[1])==process.pid]
        if current is not None or members:raise
        return dict(error=repr(error),resolution='Kernel leader identity absent and ps reports no owned-group members')
    return None


def input_recovery(prior,build,model):
    """Permit a documented UI-delivery recovery, never a repeated gameplay run."""
    result=json.loads((prior/'result.json').read_text())
    assert result['ELF_sha256']==build['ELF_sha256'] and result['model']==model
    assert result['status']=='acquisition_closed' and result['first_emulator_fault'] is None
    relative='user/sdmc/sm64ds/user-test.log';logfile=prior/relative
    assert digest(logfile)==result['files'][relative]['sha256']
    log=logfile.read_text(errors='replace')
    frames=re.findall(r'^Game vblank=.* scene=(\d+)$',log,re.M)
    assert frames and set(frames)=={'1'}, 'Recovery is restricted to a title-only session'
    assert not re.search(r'^Game HID\b|^Game scene transition\b|^FATAL\b',log,re.M)
    inputs=[line for line in log.splitlines() if line.startswith('Game menu input ')]
    assert inputs==['Game menu input update=2 touch=255,255,0,0 keys=0000'], 'Input was already consumed'
    note=prior/'observations.json'
    assert digest(note)==result['files']['observations.json']['sha256']
    status=json.loads(note.read_text())['status']
    reasons={
        'title_observed_input_not_received_runtime_comparison_pending':
            'Title-only acquisition recorded no consumed input; recover UI delivery with unchanged game binary',
        'title_logged_UI_locked_no_consumed_input':
            'macOS was locked during the title-only acquisition; exercise normal game input after unlock with unchanged binary'}
    assert status in reasons, 'No verified title-input limitation in this acquisition'
    return dict(attempt=str(prior.resolve()),result_sha256=digest(prior/'result.json'),
                reason=reasons[status])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app',type=Path,required=True)
    parser.add_argument('--model',choices=['old','new'],default='old')
    parser.add_argument('--seconds',type=int,default=600)
    parser.add_argument('--candidate',type=Path)
    parser.add_argument('--rom',type=Path,default=ROOT/'build/roms/europe.nds',help='Private ROM input for a ROM-only candidate; copied only into the fresh isolated profile')
    parser.add_argument('--asset-package',type=Path,help='Private prepared .sm64ds file; starts this isolated profile without ROM/cache')
    parser.add_argument('--rom-name',help='Arbitrary .nds basename used only inside this new isolated SD profile')
    parser.add_argument('--add-corrupt-rom',action='store_true',help='Add a same-header damaged copy before the valid name to exercise full-checksum candidate rejection')
    parser.add_argument('--verified-cache-from',type=Path,help='Explicitly test cache reuse using exact bytes rechecked against a closed prior acquisition')
    parser.add_argument('--cia',type=Path,help='Verified CIA paired with --cxi; run its exact content instead of the 3DSX')
    parser.add_argument('--cxi',type=Path,help='The exact NCCH content of --cia, with the integrated Old profile')
    parser.add_argument('--dsp-firmware',type=Path,default=Path.home()/'Library/Application Support/Azahar/sdmc/3ds/dspfirm.cdc')
    parser.add_argument('--prepare-only',action='store_true')
    parser.add_argument('--native-diagnostics',action='store_true',help='Record timing and one initial-title RAM capture; L+R+A or Developer dumps process memory')
    parser.add_argument('--capture-hitches',action='store_true',help='Diagnostic-only: at most two automatic RAM/render captures after sustained slow gameplay handoffs')
    parser.add_argument('--benchmark-profile',choices=['castle-yoshi','bob-mario','whomp-luigi','bay-wario','clock-wario'],help='Diagnostic-only original-game scenario, three complete RAM captures; implies diagnostics')
    parser.add_argument('--silent',action='store_true',help='Mute this isolated emulator profile while preserving game audio execution')
    parser.add_argument('--diagnostic-dump-modifier',action='store_true',help='Isolated test profile only: map both L/R to Shift, so real Shift+A emits L+R+A without interrupting gameplay via menus')
    parser.add_argument('--interactive-benchmark',action='store_true',help='Benchmark-only: allow300s for real manual dialogue input; no dialogue or game input is synthesized')
    parser.add_argument('--physics-mode',choices=['original','hybrid','n64'],default='original',help='Fresh benchmark profile only: select the real persisted production physics setting before boot')
    parser.add_argument('--buttons-mode',choices=['standard','touch','dual'],default='standard',help='Fresh benchmark profile only: select the real DS button mode independently')
    parser.add_argument('--inspect-after-benchmark',type=int,default=0,help='Benchmark-only: retain this owned session for up to300s after three captures for directed UI checks; inspection.done ends it early')
    parser.add_argument('--recover-input-from',type=Path,help='One recovery of a closed title-only acquisition with verified zero consumed input')
    parser.add_argument('--directed-retest',help='Explicit purpose of a user-requested regression reproduction; preserves prior acquisitions and records why the same scenario is repeated')
    args=parser.parse_args();args.native_diagnostics=args.native_diagnostics or bool(args.benchmark_profile) or args.capture_hitches;assert 1<=args.seconds<=1200
    assert not args.diagnostic_dump_modifier or args.native_diagnostics
    assert not args.interactive_benchmark or args.benchmark_profile
    assert args.benchmark_profile or (args.physics_mode=='original' and args.buttons_mode=='standard')
    assert 0<=args.inspect_after_benchmark<=300 and (not args.inspect_after_benchmark or args.benchmark_profile)
    latest=json.loads((OUT/'latest.json').read_text())
    candidate=args.candidate.resolve() if args.candidate else Path(latest['candidate'])
    build=json.loads((candidate/'BUILD.json').read_text())
    assert build['status']=='integrated_built_and_audited_emulator_pending'
    audit=json.loads((candidate/'pre-emulator-audit.json').read_text())
    assert audit['status']=='passed_before_emulator' and audit['ELF_sha256']==build['ELF_sha256']
    controls=json.loads((OUT/'controls-gate.json').read_text());assert controls['status']=='passed'
    if build.get('render60_experimental'):
        ready=json.loads((OUT/('render60-readiness-'+build['ELF_sha256'][:16]+'.json')).read_text())
        assert ready['status']=='passed_before_emulator' and ready['ELF_sha256']==build['ELF_sha256']
        for name,expected in ready['sources'].items():assert digest(ROOT/name)==expected
    for name,expected in build['files'].items():
        assert digest(candidate/name)==expected['sha256'], 'Closed candidate changed: '+name
    for name,expected in controls['sources'].items():assert digest(ROOT/name)==expected
    rom_only=build.get('rom_only',False)
    if rom_only:
        assert hashlib.sha1(args.rom.read_bytes()).hexdigest()==build['rom_install']['ROM_SHA1']
        assert not (candidate/'sd/sm64ds/repo').exists(), 'ROM-only candidate includes extracted assets'
    assert rom_only or not (args.rom_name or args.add_corrupt_rom or args.verified_cache_from)
    rom_root=Path(build.get('rom_install',{}).get('directory','sdmc:/sm64ds').removeprefix('sdmc:/'))
    flexible_name=rom_only and build['rom_install'].get('filename')=='any'
    rom_name=args.rom_name or (args.rom.name if flexible_name else 'rom.nds')
    assert rom_name and Path(rom_name).name==rom_name and rom_name.lower().endswith('.nds')
    assert all(ord(c)>=32 and c not in '<>:"/\\|?*' for c in rom_name), 'Expected a FAT-compatible basename'
    assert flexible_name or rom_name=='rom.nds', 'This closed candidate still requires its old fixed filename'
    if args.add_corrupt_rom:
        assert flexible_name and '000 - Damaged copy.nds'<rom_name
    assert bool(args.cia)==bool(args.cxi), '--cia and --cxi must be used together'
    binary=candidate/'sm64ds-integrated.3dsx';package=None
    if args.cia:
        assert rom_only,'Integrated CIA acquisition requires a ROM-only candidate'
        from verify_integrated_cia import verify
        package=verify(args.cia,candidate/'sm64ds-integrated.elf',build['ELF_sha256'])
        assert digest(args.cxi)==package['content_sha256'], 'CXI differs from exact verified CIA content'
        binary=args.cxi.resolve()
    assert args.dsp_firmware.is_file(), 'Local DSP firmware is required for the audio-enabled candidate'
    directory=OUT/'acquisitions';directory.mkdir(exist_ok=True)
    recovery=input_recovery(args.recover_input_from.resolve(),build,args.model) if args.recover_input_from else None
    repeated=[]
    for p in directory.glob('*/result.json'):
        prior=json.loads(p.read_text())
        if prior['ELF_sha256']==build['ELF_sha256'] and prior['model']==args.model:
            prior_plan=json.loads((p.parent/'plan.json').read_text())
            prior_profile=prior_plan.get('benchmark',{}).get('profile')
            # Distinct, enumerated gameplay scenarios are required by the
            # ten-run campaign. The same binary/model/scenario is still refused.
            if prior_profile==args.benchmark_profile:repeated.append(p.parent.resolve())
    if repeated:
        assert (args.directed_retest and args.benchmark_profile and len(args.directed_retest.strip())>=20) or (recovery and repeated==[args.recover_input_from.resolve()]), \
            'This exact candidate/model/scenario was already acquired; only one verified title-input recovery is allowed'
    if recovery:assert repeated, 'No matching closed acquisition to recover'
    tools=ROOT/'third_party/sm64ds-3ds/build3ds'
    sys.path.insert(0,str(tools))
    spec=importlib.util.spec_from_file_location('integrated_azahar_instance',tools/'run_isolated.py')
    runner=importlib.util.module_from_spec(spec);spec.loader.exec_module(runner)
    instance=directory/('attempt-'+str(time.time_ns()))
    plan=runner.prepare(binary,instance,args.app,[],args.model,
                        save_name='user-test.sav')
    if recovery:plan['input_recovery']=recovery
    if args.directed_retest:
        plan['directed_retest']=dict(purpose=args.directed_retest,prior_acquisitions=[str(p) for p in repeated],scope='Directed regression; does not add a distinct campaign scenario')
    user=Path(plan['user_directory']);sd=user/'sdmc'
    shutil.copytree(candidate/'sd',sd,dirs_exist_ok=True)
    if args.asset_package:
        assert rom_only and not (args.add_corrupt_rom or args.verified_cache_from or args.rom_name)
        assert args.asset_package.is_file() and args.asset_package.suffix.lower()=='.sm64ds'
        target=sd/rom_root/args.asset_package.name;target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(args.asset_package,target)
        plan['rom_bootstrap']=dict(input='prepared .sm64ds only; no ROM or cache',package_sha256=digest(target),package_filename=target.name,cache_initially_present=False,console_DSP_bootstrap_exercised=False)
    elif rom_only:
        target=sd/rom_root/rom_name;target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(args.rom,target)
        assert digest(target)==digest(args.rom)
        seed=copy_verified_cache(args.verified_cache_from,candidate,sd) if args.verified_cache_from else None
        decoy=None
        if args.add_corrupt_rom:
            decoy=sd/rom_root/'000 - Damaged copy.nds'
            changed=bytearray(args.rom.read_bytes());changed[-1]^=1;decoy.write_bytes(changed)
        plan['rom_bootstrap']=dict(input='ROM plus explicitly verified prior cache' if seed else 'ROM only; no extracted tree or settings',
            ROM_sha256=digest(args.rom),ROM_filename=rom_name,cache_initially_present=(sd/rom_root/'cache').exists(),
            verified_cache_seed=seed,corrupted_candidate=decoy.name if decoy else None,
            corrupt_candidate_sha256=digest(decoy) if decoy else None,
            console_DSP_bootstrap_exercised=False,
            DSP_test_environment='Isolated emulator uses its preexisting local component; physical system extraction is a separate proof tier')
    if package:plan['CIA_verification']=package
    if args.native_diagnostics:
        (sd/rom_root/'native-diagnostics.enabled').write_text('SM64DS_NATIVE_DIAGNOSTICS=1\n')
        if not args.benchmark_profile:(sd/rom_root/'native-title-capture.once').write_text('SM64DS_TITLE_CAPTURE=1\n')
    if args.capture_hitches:
        (sd/rom_root/'native-hitch-capture.enabled').write_text('SM64DS_HITCH_CAPTURE_V1\n')
        plan['automatic_hitch_capture']=dict(maximum=2,threshold_ms=25,threshold_ms_by_lock={'30':50,'60_or_off':25},sustained_samples=3,
            single_handoff_ms=50,cooldown_seconds=10,settling_handoffs=120)
    if args.benchmark_profile:
        (sd/rom_root/'benchmark-profile.txt').write_text('SM64DS_BENCHMARK_V1\nscenario='+args.benchmark_profile+'\nmodel='+args.model+'\n')
        physics=['original','hybrid','n64'].index(args.physics_mode)
        buttons=['standard','touch','dual'].index(args.buttons_mode)
        (sd/rom_root/'settings.ini').write_text('[3d]\nresolution=native\n[display]\nmode=wide\n[debug]\ndumps=1\nshow_fps=1\n[input]\nphysics='+str(physics)+'\nmode='+str(buttons)+'\n')
        plan['benchmark']=dict(profile=args.benchmark_profile,model=args.model,captures_requested=3,provenance='Original StartFile and SetRealCharacter APIs; no RAM pokes or synthetic game input; private isolated save')
        plan['benchmark']['post_capture_inspection_seconds']=args.inspect_after_benchmark
        plan['benchmark']['production_settings']=dict(physics=args.physics_mode,buttons=args.buttons_mode,source='fresh isolated settings.ini; no live memory modification')
        if args.interactive_benchmark:
            (sd/rom_root/'benchmark-interactive.enabled').write_text('SM64DS_BENCHMARK_INTERACTIVE_V1\n')
            plan['benchmark']['manual_dialogue_watchdog_seconds']=300
    (sd/'3ds').mkdir(exist_ok=True)
    shutil.copyfile(args.dsp_firmware,sd/'3ds/dspfirm.cdc')
    shutil.copyfile(candidate/'sm64ds-integrated.elf',instance/'sm64ds-integrated.elf')
    profile=Path(plan['config'])
    if args.diagnostic_dump_modifier:
        plan['diagnostic_keyboard_mapping']='Both emulated L/R use physical Shift; Shift+A produces ordinary HID L+R+A. This is not physical-console shortcut acceptance.'
    screenshots=instance/'screenshots';screenshots.mkdir()
    # Profile-local E produces a normal lower-panel centre touch. Game state,
    # scripts and consumed inputs are never synthesized by the supervisor.
    with profile.open('a') as stream:
        if args.silent:stream.write('\n[Audio]\nvolume=0\nvolume\\default=false\n')
        stream.write('\n[Controls]\n'
            'profiles\\size=1\n'
            'profiles\\1\\name=Integrated native\n'
            'profiles\\1\\use_touch_from_button=true\n'
            'profiles\\1\\use_touch_from_button\\default=false\n'
            'profiles\\1\\touch_from_button_map=0\n'
            'profiles\\1\\touch_from_button_map\\default=false\n'
            'touch_from_button_maps\\size=1\n'
            'touch_from_button_maps\\1\\name=integrated-screen-centre\n'
            'touch_from_button_maps\\1\\entries\\size=1\n'
            'touch_from_button_maps\\1\\entries\\1\\bind="code:69,engine:keyboard,x:160,y:120"\n'
            'touch_from_button_maps\\1\\entries\\1\\bind\\default=false\n')
        if 'port/platform/3ds/native_pause_menu.cpp' in build.get('render60_overlay',{}).get('files',{}):
            # Profile-local keyboard pen positions for manual CUA inspection.
            # These produce ordinary HID touch samples, never game-state writes.
            # Coordinates below are DS logical pixels; Qt maps the 320x240 LCD.
            points=[(69,128,96),(70,128,180),(71,64,100),(72,128,120),
                    (79,128,40),(80,128,80),(84,128,168),(85,232,96),(86,24,96),
                    (67,25,165),(82,231,165),(89,231,133)] # C/R/Y: real held camera pen samples
            stream.write('touch_from_button_maps\\1\\entries\\size='+str(len(points))+'\n')
            for i,(code,x,y) in enumerate(points,1):
                prefix='touch_from_button_maps\\1\\entries\\'+str(i)+'\\bind'
                stream.write(prefix+'="code:'+str(code)+',engine:keyboard,x:'+str(round(x*1.25))+',y:'+str(round(y*1.25))+'"\n'+prefix+'\\default=false\n')
        stream.write('\n[UI]\nPaths\\screenshotPath='+str(screenshots)+
            '\nPaths\\screenshotPath\\default=false\n')
        if args.diagnostic_dump_modifier:
            stream.write('\n[Controls]\n')
            for button in ('l','r'):
                stream.write('profiles\\1\\button_'+button+'="code:16777248,engine:keyboard"\n'
                    'profiles\\1\\button_'+button+'\\default=false\n')
        stream.write('\n[Miscellaneous]\ncheck_for_update_on_start=false\n'
            'check_for_update_on_start\\default=false\n')
    (instance/'plan.json').write_text(json.dumps(plan,indent=2)+'\n')
    if args.prepare_only:print(json.dumps(plan,indent=2));return
    with (OUT/'acquisition.lock').open('w') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        began=time.monotonic();reason='time_bound';fault=None;benchmark_completed_at=None
        stop_signal=[]
        for sig in (signal.SIGINT,signal.SIGTERM):
            signal.signal(sig,lambda number,frame:stop_signal.append(number))
        with (instance/'launcher.log').open('w') as launcher:
            process=runner.launch_owned(plan,launcher)
            (instance/'process.json').write_text(json.dumps(dict(pid=process.pid,
                pgid=process.pid,start_identity=getattr(process,'identity',None)),indent=2)+'\n')
            print(json.dumps(dict(pid=process.pid,instance=str(instance),app=plan['app'],model=args.model)),flush=True)
            try:
                while time.monotonic()-began<args.seconds:
                    if stop_signal:reason='supervisor_signal_'+str(stop_signal[-1]);break
                    stop_file=instance/'stop.request'
                    if stop_file.is_file() and not stop_file.is_symlink() and stop_file.read_text()=='SM64DS_STOP_V1\n':
                        reason='directed_inspection_complete';break
                    log=sd/rom_root/'user-test.log'
                    app_log=log.read_text(errors='replace') if log.exists() else ''
                    emulator=user/'log/azahar_log.txt'
                    emu=emulator.read_text(errors='replace') if emulator.exists() else ''
                    invalid=re.search(r'^.*(?:UnmappedAccess:|Exception Type:|Undefined instruction).*$',''+emu,re.M)
                    if invalid:fault=invalid[0];reason='emulator_memory_or_CPU_fault';break
                    if re.search(r'^FATAL\b',app_log,re.M):reason='application_fatal';break
                    if args.benchmark_profile:
                        # Completion is logged only after all three VM+GPU
                        # captures have closed successfully. Do not spend the
                        # rest of the safety timeout repeating an idle scene.
                        if re.search(r'^BENCHMARK event=complete-three-dumps\b',app_log,re.M):
                            reason='benchmark_three_complete_captures'
                            if benchmark_completed_at is None:benchmark_completed_at=time.monotonic()
                            done=instance/'inspection.done'
                            requested_stop=done.is_file() and not done.is_symlink() and done.read_text()=='SM64DS_INSPECTION_DONE_V1\n'
                            if not args.inspect_after_benchmark or requested_stop or time.monotonic()-benchmark_completed_at>=args.inspect_after_benchmark:break
                        aborted=re.search(r'^BENCHMARK event=aborted-diagnostic-capture-not-campaign\b',app_log,re.M)
                        if aborted and re.search(r'^Render diagnostic .* complete=1$',app_log[aborted.end():],re.M):
                            reason='benchmark_aborted_diagnostic_preserved';break
                    if process.poll() is not None:reason='process_exit';break
                    time.sleep(.2)
            finally:
                # Observe before our cleanup signal so a spontaneous exit is
                # distinguishable from a planned supervisor termination.
                observed_returncode=process.poll()
                cleanup_note=stop_owned(runner,process)
        files={str(p.relative_to(instance)):{'bytes':p.stat().st_size,'sha256':digest(p)}
               for p in instance.rglob('*') if p.is_file() and 'Azahar.app' not in p.parts}
        result=dict(status='acquisition_closed',candidate=str(candidate),model=args.model,
            ELF_sha256=build['ELF_sha256'],image_sha256=build['image_sha256'],
            elapsed_seconds=time.monotonic()-began,stop_reason=reason,first_emulator_fault=fault,
            observed_process_returncode=observed_returncode,
            files=files,physical_hardware=False,FPS_proof=False,
            input_recovery=recovery,
            supervisor_cleanup_note=cleanup_note,
            gameplay_acceptance='Requires observation/input evidence; not inferred from process survival')
        (instance/'result.json').write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps({k:result[k] for k in ['status','elapsed_seconds','stop_reason','first_emulator_fault']},indent=2))


if __name__=='__main__':main()
