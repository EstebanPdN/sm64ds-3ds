#!/usr/bin/env python3
"""Test actual headless N64 instance contacts and stable-storage tick ownership.

The modified headless TU, C fixture and C++ ownership layer are sanitizer
instrumented. Other imported N64 archive members are the normal host build;
this gate does not claim whole-core sanitizer coverage or live 3DS gameplay.
"""
from pathlib import Path
import hashlib,json,re,subprocess,time
ROOT=Path(__file__).resolve().parents[2];CORE=ROOT/'build/movement-lab/core'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    out=ROOT/'build/movement-adapter'/('solver-contacts-'+str(time.time_ns()));out.mkdir(parents=True)
    for mode in ['host','arm']:
        manifest=json.loads((CORE/mode/'sources.json').read_text())
        for name,digest in manifest.items():assert sha(Path(name))==digest,name
    for name in ['solver_contacts.h','solver_contacts.inc','collision_provider.c','collision_provider.h']:
        assert (CORE/'source/src'/name).read_bytes()==(ROOT/'source/integrated_native/movement'/name).read_bytes()
    commands=[]
    def run(command):
        commands.append(command);r=subprocess.run(command,capture_output=True,text=True)
        with (out/'run.log').open('a') as f:f.write(r.stdout+r.stderr)
        r.check_returncode();return r.stdout.strip()
    includes=['-I'+str(ROOT/'source/integrated_native/movement'),'-I'+str(CORE/'source/src'),'-I'+str(CORE/'source/src/decomp/include'),'-I'+str(ROOT/'build/movement-lab')]
    sanitizer=['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']
    defines=['-DGBI_FLOATS','-DVERSION_US','-DNO_SEGMENTED_MEMORY']
    fixture=ROOT/'tests/movement_lab/solver_contacts_fixture.c';test=ROOT/'tests/movement_lab/test_solver_contacts.cpp'
    run(['clang','-std=gnu11',*sanitizer,*defines,*includes,'-c',str(fixture),'-o',str(out/'fixture.o')])
    run(['clang','-std=gnu11',*sanitizer,*defines,*includes,'-Wno-incompatible-pointer-types','-Wno-int-conversion','-c',str(CORE/'headless.c'),'-o',str(out/'headless.o')])
    platform_source=CORE/'source/src/decomp/game/platform_displacement.c'
    run(['clang','-std=gnu11',*sanitizer,*defines,*includes,'-c',str(platform_source),'-o',str(out/'platform.o')])
    # Headless.o provides the engine front door before the archive is scanned;
    # its ordinary archive counterpart is not selected by the linker.
    run(['clang++','-std=c++17',*sanitizer,*includes,str(test),str(out/'fixture.o'),str(out/'headless.o'),str(out/'platform.o'),str(CORE/'host/libmotion64.a'),'-lm','-o',str(out/'test')])
    output=run([str(out/'test')])
    peak=float(re.search(r'max Y ([0-9.]+)',output)[1])
    session_test=ROOT/'tests/movement_lab/test_world_session.cpp'
    run(['clang++','-std=c++17',*sanitizer,*includes,str(session_test),str(out/'fixture.o'),str(out/'headless.o'),str(out/'platform.o'),str(CORE/'host/libmotion64.a'),'-lm','-o',str(out/'test-session')])
    session_output=run([str(out/'test-session')])
    run(['/opt/devkitpro/devkitARM/bin/arm-none-eabi-g++','-std=gnu++17','-Os','-march=armv6k','-mfloat-abi=hard','-mfpu=vfp',*includes,'-c',str(session_test),'-o',str(out/'session-arm.o')])
    stable_platform_test=ROOT/'tests/movement_lab/test_stable_platforms.cpp'
    run(['clang++','-std=c++17',*sanitizer,*includes,str(stable_platform_test),'-o',str(out/'test-stable-platforms')])
    stable_platform_output=run([str(out/'test-stable-platforms')])
    run(['/opt/devkitpro/devkitARM/bin/arm-none-eabi-g++','-std=gnu++17','-Os','-march=armv6k','-mfloat-abi=hard','-mfpu=vfp',*includes,'-c',str(stable_platform_test),'-o',str(out/'stable-platform-arm.o')])
    prepared_wall_test=ROOT/'tests/movement_lab/test_prepared_walls.cpp'
    run(['clang++','-std=c++17',*sanitizer,*includes,str(prepared_wall_test),'-o',str(out/'test-prepared-walls')])
    prepared_wall_output=run([str(out/'test-prepared-walls')])
    run(['/opt/devkitpro/devkitARM/bin/arm-none-eabi-g++','-std=gnu++17','-Os','-march=armv6k','-mfloat-abi=hard','-mfpu=vfp',*includes,'-c',str(prepared_wall_test),'-o',str(out/'prepared-wall-arm.o')])
    world_source_test=ROOT/'tests/movement_lab/ds_world_source_compile.cpp'
    run(['clang++','-std=c++17',*sanitizer,*includes,'-c',str(world_source_test),'-o',str(out/'world-source-host.o')])
    run(['/opt/devkitpro/devkitARM/bin/arm-none-eabi-g++','-std=gnu++17','-Os','-march=armv6k','-mfloat-abi=hard','-mfpu=vfp',*includes,'-c',str(world_source_test),'-o',str(out/'world-source-arm.o')])
    storage_symbols=run(['/opt/devkitpro/devkitARM/bin/arm-none-eabi-nm','-S',str(out/'world-source-arm.o')])
    storage={parts[3]:int(parts[1],16) for line in storage_symbols.splitlines() if len(parts:=line.split())==4 and parts[3] in ['fixture_native_world_source_storage','fixture_native_platform_owner_storage']}
    provider_test=ROOT/'tests/movement_lab/test_collision_provider.c'
    run(['clang','-std=gnu11',*sanitizer,*includes,str(provider_test),str(ROOT/'source/integrated_native/movement/collision_provider.c'),'-lm','-o',str(out/'test-providers')])
    run([str(out/'test-providers')])
    nm=run(['/opt/devkitpro/devkitARM/bin/arm-none-eabi-nm','-g','--defined-only',str(CORE/'arm/libmotion64.a')])
    for name in ['motion64_get_contacts','motion64_visit_contacts','motion64_permanent_surface','motion64_query_walls','motion64_set_wall_provider','motion64_set_platform_provider','motion64_query_platform','motion64_seed_buttons']:assert any(line.endswith(' T '+name) for line in nm.splitlines())
    files=[fixture,test,session_test,provider_test,platform_source,stable_platform_test,prepared_wall_test,world_source_test,Path(__file__),ROOT/'tools/movement_lab/build_core.py']+[ROOT/'source/integrated_native/movement'/name for name in ['solver_contacts.h','solver_contacts.inc','solver_contact_pins.hpp','stable_surfaces.hpp','collision_provider.c','collision_provider.h','world_session.hpp','ds_world_query.h','wall_contact.hpp','world_triangle.hpp','kcl_prism.hpp','stable_platforms.hpp','ds_world_source.hpp','world_walls.hpp','surface_description.hpp','material_profile.hpp']]
    report={'status':'passed','actual_mario_instances':2,'stationary_and_jump_ticks':360,'peak_fixture_y':peak,'ceiling_entry_routed_to_provider':True,'ceiling_miss_never_falls_back':True,'ceiling_unregister_restores_original_mesh':True,'all_four_surface_fields_enumerated':True,'platform_transform_enumerated_and_deduplicated':True,'water_pseudo_floor_recognized':True,'foreign_mesh_rejected':True,'global_binding_unchanged_by_contact_reads':True,'deleted_instances_excluded':True,'headless_platform_TU_and_ownership_sanitizers':['address','undefined'],'output':output,'source_sha256':{str(p.relative_to(ROOT)):sha(p) for p in files},'host_core_sha256':sha(CORE/'host/libmotion64.a'),'arm_core_sha256':sha(CORE/'arm/libmotion64.a'),'host_manifest_sha256':sha(CORE/'host/sources.json'),'arm_manifest_sha256':sha(CORE/'arm/sources.json'),'instrumented_headless_object_sha256':sha(out/'headless.o'),'directory':str(out),'limits':'CPU contact-lifetime, provider routing and synthetic-world solver fixtures. Modified headless/platform-displacement TUs plus fixture/ownership code are sanitizer-instrumented; remaining imported archive members are not. Unified DS backend compiles but has not yet run end to end. No main-game movement, emulator or physical FPS acceptance.'}
    report['world_session_solver_ticks']=1022;report['world_session_circle_pad_ticks']=120;report['world_session_output']=session_output
    report['world_session_fault_stages_output_and_owns_teardown']=True
    report['retained_surface_nested_platforms_pinned_without_object_platform']=True
    report['platform_carry_provider_hit_error_unregister_original_path']='passed with instrumented original platform TU'
    report['stable_platform_owner_output']=stable_platform_output
    report['world_session_platform_ticks']=120
    report['button_history_seed_ticks']=62
    report['held_seed_suppresses_phantom_press_and_release_new_press_jumps']=True
    report['prepared_wall_snapshot_output']=prepared_wall_output
    report['native_backend_compile']='host and ARM full instantiation passed; no runtime acceptance'
    report['native_backend_ARM_storage_bytes']=storage['fixture_native_world_source_storage']
    report['native_platform_owner_ARM_storage_bytes']=storage['fixture_native_platform_owner_storage']
    report['wall_provider_malformed_result_tests']='passed under ASan/UBSan'
    (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n');(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');(ROOT/'build/movement-adapter/solver-contacts-gate-latest.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='source_sha256'},indent=2))
if __name__=='__main__':main()
