#!/usr/bin/env python3
"""Local owner-only CIA for one compiled native-renderer milestone.
No download, upload, publication or ROM redistribution. Run after build_playable.
"""
from pathlib import Path
import hashlib,json,os,re,shutil,subprocess,sys
from verify_integrated_cia import verify
root=Path(__file__).resolve().parents[1]; base=root/'build/native-renderer/playable-r1'
number=int(sys.argv[1]); session=base/'sessions'/sys.argv[2]
# Optional experimental tag (build_experimental.sh): game-pN-TAG, buildN-TAG.
tag=sys.argv[3] if len(sys.argv)>3 else ''
if tag and not re.fullmatch(r'[a-z0-9-]+',tag):raise SystemExit('Invalid tag')
build_name=f'{number}-{tag}' if tag else str(number)
source=root/'build/native-renderer/game-runtime-20261026-r1/source'
version=(source/'port/platform/3ds/build_version.hpp').read_text()
assert int(re.search(r'^#define SM64DS_BUILD_NUMBER (\d+)$',version,re.M)[1])==number
plan=json.loads((session/'plan.json').read_text())
assert Path(plan['binary']).parent.name==f'game-p{build_name}'
assert hashlib.sha256(Path(plan['binary']).read_bytes()).hexdigest()==plan['sha256']
policy=root/'packaging/sm64ds-integrated.rsf'
rsf=policy.read_text()
for before,after in [('Title: SM64DS','Title: SM64DSNT'),('ProductCode: CTR-H-SM64','ProductCode: CTR-H-SMNT'),('UniqueId: 0xF5364','UniqueId: 0xF5365')]:
    assert rsf.count(before)==1,before
    rsf=rsf.replace(before,after,1)
rsf+='\nRomFs:\n  RootPath: "romfs"\n'
out=base/'hardware'/f'build{build_name}'
out.mkdir(parents=True,exist_ok=False)
env=os.environ.copy(); sdk=os.environ.get('DEVKITPRO','/opt/devkitpro')
env.update(DEVKITPRO=sdk,DEVKITARM=sdk+'/devkitARM');env['PATH']=sdk+'/devkitARM/bin:'+sdk+'/tools/bin:'+env['PATH']
def run(command,cwd=root):
    subprocess.run([str(x) for x in command],cwd=cwd,env=env,check=True)
# Isolate the hardware diagnostic entry from the tested production source.
# Keep fine per-object SVC timing out of the hardware play path. The
# native-test process entry still ignores env.txt and keeps manual controls.
main_source=source/'port/platform/3ds/game_main.cpp'
main_text=main_source.read_text()
anchor=' if(!sm64ds_fresh_native_startup())return 1;'
if main_text.count(anchor)!=1:raise ValueError('Native-test profiling boundary changed')
profile_source=out/'native-test-game-main.cpp'
profile_source.write_text(main_text.replace(anchor,
    ' setenv("SM64DS_FRESH_NATIVE_PROFILE","0",0);\n native_diagnostics::configure_verbose();\n setenv("SM64DS_NATIVE_LOAD_TRACE",native_diagnostics::verbose()?"1":"0",0);\n setenv("SM64DS_NATIVE_STAGE_PROFILE",native_diagnostics::verbose()?"1":"0",0);\n'+anchor,1))
run([sys.executable,source/'build3ds/build_game.py','--native-test','--source',profile_source,'--output',out])
# Use the exact banks of the matching tested 3DSX session, not an old CIA.
shutil.copytree(Path(plan['sd_directory'])/'native',out/'romfs/native')
(out/'native-test.rsf').write_text(f'# Private native-renderer BUILD {build_name}. Title 000400000F536500.\n'+rsf)
art=root/'build/integrated-native/package-cc51967a3eb54678'
for name in ['banner.bnr','icon.png']:shutil.copy2(art/name,out/name)
tool=root.parent/'MARIO KART 64/Tools/bin'
run([tool/'bannertool','makesmdh','-s',f'SM64DS Native {build_name}','-l',f'SM64DS Native Renderer BUILD {build_name}','-p','EstebanPdN','-i',out/'icon.png','-f','visible,allow3d,recordusage,nosavebackups','-o',out/'icon.smdh'])
cia=out/f'SM64DS-Native-BUILD{build_name}.cia'
run([tool/'makerom','-f','cia','-o',cia.name,'-target','t','-exefslogo','-elf','sm64ds.elf','-rsf','native-test.rsf','-icon','icon.smdh','-banner','banner.bnr'],out)
assert cia.stat().st_size>1024
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
checked=verify(cia,out/'sm64ds.elf',sha(out/'sm64ds.elf'),native_test=True)
(out/'integrity.json').write_text(json.dumps(checked,indent=2)+'\n')
receipt={'build':number,'tag':tag,'title_id':'000400000F536500','native_test_entry':True,'private_owner_only':True,'hardware_validated':False,
         'test_session':session.name,'tested_3dsx_sha256':plan['sha256'],
         'profiled_hardware_entry':False,'coarse_stage_interval_frames':240,'canonical_main_sha256':sha(main_source),'profile_main_sha256':sha(profile_source),
         'packaging_policy':str(policy.relative_to(root)),'packaging_policy_sha256':sha(policy),
         'files':{str(p.relative_to(out)):sha(p) for p in sorted(out.rglob('*')) if p.is_file()},
         'compiled_overlay':json.loads((source/'native-renderer-overlay.json').read_text())}
(out/'package-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
(out/'README.md').write_text(f'''# BUILD {number} — CIA privada de prueba

Título de prueba: `000400000F536500`, separado del juego principal.
Conserva el número BUILD {number} en el juego y el icono HOME.
Assets nativos incluidos en RomFS; conserva la configuración habitual y necesita
los datos del propietario ya preparados o `sdmc:/3ds/sm64ds/owner-rom.nds`.
Guardado separado: `sdmc:/3ds/sm64ds/native-test.sav`.
Log: `sdmc:/3ds/sm64ds/native-test.log`.\nPerfil fino desactivado, trazas de carga y una muestra de fases cada240frames; control manual, sin guion ni curso forzado.

Instalar esta CIA con el procedimiento habitual de la consola. Comprobar
estrella, título y laterales durante 30 segundos, Aventura, fondo animado del
selector, cinemática, diálogos, juego, HUD y pausa/reanudación.

Validación física pendiente. La ejecución en Azahar no la sustituye.
Este paquete local contiene datos del propietario; no es una release pública.
SHA-256 CIA: `{sha(cia)}`
''')
print(cia)
