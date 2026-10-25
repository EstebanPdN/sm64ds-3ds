#!/usr/bin/env python3
"""Package a closed ROM-only integrated candidate; no build, ROM or publication.

All outputs go to new, exclusive work and delivery directories. Historical
preview tools/dist, candidate inputs, ROM, DSP, caches, catalogs, dumps and saves
are never copied or changed. Pillow is required only for actual packaging.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
import wave

from verify_integrated_cia import TITLE_ID, require, verify
from verify_native_3dsx import verify as verify_image

ROOT = Path(__file__).resolve().parents[1]
RSF = ROOT / 'packaging/sm64ds-integrated.rsf'
ELF_NAME, IMAGE_NAME = 'sm64ds-integrated.elf', 'sm64ds-integrated.3dsx'
SD_IMAGE = 'sd/3ds/sm64ds/sm64ds.3dsx'
ROM_DIRECTORY = 'sdmc:/3ds/sm64ds'
SOURCE_NAMES = ('game_entry.cpp', 'rom_setup_3ds.hpp', 'boot_allocator.hpp', 'rom_bootstrap.hpp',
                'dsp_bootstrap.hpp', 'rom_discovery.hpp', 'loading_screen.hpp', 'loading_screen_3ds.hpp',
                'loading_art.hpp','loading_locale.hpp','loading_style.hpp','rom_art.hpp','rom_art_manifest.hpp','rom_art_embedded.hpp','packed_assets.hpp','packed_assets_3ds.hpp','packed_builder.hpp','setup_power.hpp',
                'rom_install_manifest.hpp', 'native_diagnostics.hpp', 'enhance_settings.h')


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def safe_input(candidate, name):
    rel = PurePosixPath(name)
    require(name == rel.as_posix() and not rel.is_absolute() and '..' not in rel.parts,
            'Invalid closed candidate path')
    path = candidate.joinpath(*rel.parts)
    require(not any(parent.is_symlink() for parent in (path, *path.parents)
                    if parent.is_relative_to(candidate)), 'Candidate symlink is not a frozen input')
    require(path.is_file() and path.resolve().is_relative_to(candidate), 'Missing closed candidate input: ' + name)
    return path


def candidate_check(candidate):
    candidate = Path(candidate).resolve()
    build_path = safe_input(candidate, 'BUILD.json')
    build_sha = digest(build_path)
    build = json.loads(build_path.read_text())
    require(build.get('status') == 'integrated_built_and_audited_emulator_pending'
            and build.get('rom_only') is True, 'Expected a closed ROM-only candidate; earlier builds are not installable with only a ROM')
    install = build.get('rom_install') or {}
    require(install.get('directory') == ROM_DIRECTORY and install.get('extension') == '.nds'
            and install.get('filename') == 'any' and install.get('preparation')=='single_SM64DSP1_container'
            and isinstance(install.get('files'), int)
            and install['files'] > 0 and isinstance(install.get('bytes'), int) and install['bytes'] > 0,
            'Missing ROM-only install contract')
    schema = install.get('schema_id')
    require(isinstance(schema, str) and re.fullmatch(r'[A-Za-z0-9_.-]+', schema)
            and schema not in ('.', '..'), 'Invalid ROM cache schema')
    require(build.get('render60_experimental') is True, 'Expected the audited integrated experiment')
    overlay = build.get('render60_overlay') or {}
    require(overlay.get('rom_only') is True, 'Derived source overlay is not ROM-only')
    files = build.get('files')
    require(isinstance(files, dict), 'Missing closed candidate file manifest')
    # Reject old private asset trees from metadata before opening any SD file.
    require({name for name in files if name.startswith('sd/')} == {SD_IMAGE},
            'Candidate SD manifest must contain only the 3DSX, without ROM/resources/DSP/settings/diagnostics')
    sd = candidate / 'sd'
    require(sd.is_dir() and not sd.is_symlink(), 'Missing candidate SD directory')
    actual_sd = set()
    for path in sd.rglob('*'):
        require(not path.is_symlink(), 'Candidate SD symlink is not allowed')
        if path.is_file():
            actual_sd.add(path.relative_to(candidate).as_posix())
    require(actual_sd == {SD_IMAGE}, 'Candidate SD contains unlisted files')
    verified = {}
    for name, record in files.items():
        require(isinstance(record, dict), 'Invalid candidate file record')
        path = safe_input(candidate, name)
        require(path.stat().st_size == record.get('bytes') and digest(path) == record.get('sha256'),
                'Closed candidate changed: ' + name)
        verified[name] = record['sha256']
    for name, key in ((ELF_NAME, 'ELF_sha256'), (IMAGE_NAME, 'image_sha256')):
        require(verified.get(name) == build.get(key), 'Candidate executable hash mismatch: ' + name)
    require(verified.get(SD_IMAGE) == build['image_sha256'], 'SD 3DSX differs from the audited image')
    audit = json.loads(safe_input(candidate, 'pre-emulator-audit.json').read_text())
    require(verified.get('pre-emulator-audit.json') and audit.get('status') == 'passed_before_emulator'
            and audit.get('ELF_sha256') == build['ELF_sha256']
            and audit.get('image_sha256') == build['image_sha256'], 'Pre-emulator audit does not match candidate')
    sources = {}
    for name in SOURCE_NAMES:
        relative = 'port/platform/3ds/' + name
        closed = 'source-evidence/overlay/' + relative
        path = safe_input(candidate, closed)
        require(verified.get(closed) == overlay.get('files', {}).get(relative),
                'ROM-only source is not covered by the closed overlay: ' + name)
        sources[name] = path.read_text()
    require('rom_setup_3ds::prepare()' in sources['game_entry.cpp']
            and '#include "rom_setup_3ds.hpp"' in sources['game_entry.cpp'], 'ROM-only entry hook is absent')
    setup = sources['rom_setup_3ds.hpp']
    require('packed_builder::ensure(' in setup and 'dsp_bootstrap::ensure()' in setup
            and 'rom_discovery::next(' in setup and ROM_DIRECTORY in setup
            and 'selected.path.c_str()' in setup and 'sdmc:/3ds/sm64ds/prepared.sm64ds' in setup
            and 'setenv("SM64DS_ASSET_ROOT",assets.root.c_str(),1)' in setup,
            'ROM/cache/DSP setup contract is absent')
    # Full RAM dumps default on (2026-10-24, requested by the owner); periodic
    # SD telemetry stays off unless the diagnostics marker file exists.
    require('enhance::live().developer_dumps.load' in sources['native_diagnostics.hpp']
            and 'bool developer_dumps = true;' in sources['enhance_settings.h']
            and 'std::atomic<bool> developer_dumps{true};' in sources['enhance_settings.h']
            and 'inline bool verbose()' in sources['native_diagnostics.hpp'],
            'Expected saved Developer dumps on by default and marker-only telemetry')
    require('Copyright (C) 2017 zoogie' in sources['dsp_bootstrap.hpp'], 'Missing DSP1 attribution')
    require('LICENSE' in verified, 'Missing closed vendor license')
    # Independent repackaging compares actual ELF/3DSX contents, not just receipt hashes.
    pair = verify_image(candidate / ELF_NAME, candidate / IMAGE_NAME)
    require(digest(build_path) == build_sha, 'Candidate receipt changed during verification')
    return build, dict(BUILD_sha256=build_sha, checked_files=len(verified),
                       ELF_3DSX_pair=pair, rom_directory=ROM_DIRECTORY,rom_extension='.nds',rom_filename='any',
                       cache_path='sdmc:/3ds/sm64ds/prepared.sm64ds',
                       cache_files=install['files'], cache_bytes=install['bytes'],
                       schema_id=schema, diagnostics_enabled_by_package=False), sources


def find_tool(explicit, variable, name):
    fallback = ROOT.parent / 'MARIO KART 64/Tools/bin' / name
    found = explicit or os.environ.get(variable) or shutil.which(name) or fallback
    path = Path(found).expanduser().resolve()
    require(path.is_file() and os.access(path, os.X_OK), 'Missing executable packaging tool: ' + name)
    return path


APPROVED_ICON = ROOT / 'assets/icons/mario-yellow-approved.png'
APPROVED_BANNER = ROOT / 'assets/banners/approved-logo-v2/ready-to-use/sm64ds-logo.cgfx'
APPROVED_BANNER_PREVIEW = ROOT / 'assets/banners/approved-logo-v2/previews/HOME-400x240.png'


def artwork():
    from PIL import Image
    require(APPROVED_ICON.is_file() and APPROVED_BANNER.is_file(), 'Approved HOME artwork is missing')
    require(APPROVED_BANNER.read_bytes()[:4] == b'CGFX', 'Approved banner is not a native CGFX model')
    # Owner-selected pixel artwork: preserve its square framing and palette.
    icon = Image.open(APPROVED_ICON).convert('RGB').resize((48,48),Image.Resampling.NEAREST)
    banner = Image.open(APPROVED_BANNER_PREVIEW).convert('RGB')
    return icon, banner


def dsp_license(source):
    start = source.index('// Copyright (C) 2017 zoogie')
    end = source.index('// THE SOFTWARE.', start) + len('// THE SOFTWARE.')
    return '\n'.join(line.removeprefix('//').lstrip() for line in source[start:end].splitlines()) + '\n'


def instructions(prefix, checked, build_number=None):
    return f'''SM64DS Native - experimental ROM-only
Version visible: {('BUILD '+str(build_number)) if build_number is not None else 'candidato anterior sin contador'}
Candidato ELF: {prefix}
Title ID: {TITLE_ID:016X}; version interna: 0

INSTALACION EN OLD Y NEW 3DS
1. Instala sm64ds-native-{prefix}.cia con FBI.
2. Crea la carpeta /3ds/sm64ds en la tarjeta SD.
3. Copia tu ROM Europa ASMP revision 0 dentro de SD:/3ds/sm64ds/.
   El nombre es libre: basta que termine en .nds (tambien .NDS).
   Ejemplo: SD:/3ds/sm64ds/Super Mario 64 DS (Europa).nds
   Si hay varias copias, se busca una compatible y se comprueba su contenido.
   Alternativa rapida: arrastra la ROM a SM64DS Packer en tu ordenador y copia
   el unico archivo .sm64ds resultante a SD:/3ds/sm64ds/. No necesita la ROM
   ni crea la cache: se verifica y se lee directamente en modo de solo lectura.
   Se prioriza un .sm64ds valido; si esta danado, se intenta otra copia o la ROM.
4. Abre SM64DS Native. La primera preparacion verifica la ROM y muestra progreso.
   El contenedor privado se genera en {checked['cache_path'].replace('sdmc:', 'SD:')}.
   Mantiene la ROM original y verifica el contenedor al volver a abrir.
   Conserva cualquier cache antigua; esta version prepara un unico archivo.
   Deja al menos 64 MiB libres ademas de la ROM para cache, temporales y partida.

Los graficos originales del menu estan incluidos para mostrar la interfaz incluso
sin una ROM legible. No incluye la ROM ni los datos extraidos para jugar.
Puedes cerrar la tapa durante la preparacion: la extraccion continua con las
pantallas apagadas. Al completar archivos y audio, la luz de notificacion
parpadea en verde; abre la tapa para entrar al juego. Al salir se restaura
la suspension anterior. Esto requiere comprobacion en consola fisica.

La pantalla de preparacion sigue el idioma de la consola: ingles, espanol,
portugues, frances, italiano o aleman. Otros idiomas o un fallo de consulta
usan ingles. No requiere un archivo de configuracion de idioma.

El audio obtiene su componente de la propia consola cuando hace falta.
No tienes que copiar dspfirm.cdc manualmente para este flujo.
La lectura del HOME Menu y el arranque en tu consola quedan por validar.
El paquete incluye tambien SD/3ds/sm64ds/sm64ds.3dsx como alternativa para
Homebrew Launcher; el CIA instalado solo necesita la ROM en la carpeta indicada.

ESTADO EXPERIMENTAL
La presentacion de 60 Hz es un experimento. No acredita 60 FPS sostenidos,
compatibilidad completa ni instalacion validada en una Old 3DS fisica.
El guardado se implementa en SD:/3ds/sm64ds/user-test.sav y el log del juego en
SD:/3ds/sm64ds/user-test.log. La persistencia real debe comprobarse en consola.
El C-Stick controla la camara libre en los modelos que tienen C-Stick.

DIAGNOSTICO OPCIONAL Y MANUAL
El paquete deja los volcados de RAM desactivados.
Entra en pausa > Options > Developer y activa Full RAM dumps.
L + R + A captura RAM legible del proceso y traza de tiempos; tambien puedes
usar Dump RAM desde ese menu. Delete dumps pide confirmacion para borrarlos.
Show FPS dibuja el contador arriba. Select overlay permite abrir y cerrar el
panel de diagnostico mediante Select; Select por si solo no genera un dump.
Los archivos native-*.vm, *.trace y *.anchors.json permanecen en tu SD.
La captura incluye memoria del proceso; conservarla como evidencia privada.
Desactiva Full RAM dumps desde Developer para dejar de capturar.

El icono y el modelo 3D del banner fueron seleccionados por el propietario para esta entrega. El audio del banner permanece silencioso.
Consulta CREDITS.txt, LICENSE-vendor.txt y LICENSE-DSP1.txt.
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--candidate', type=Path, required=True)
    parser.add_argument('--work', type=Path, help='New exclusive work directory')
    parser.add_argument('--output', type=Path, help='New exclusive delivery directory')
    parser.add_argument('--makerom', type=Path)
    parser.add_argument('--bannertool', type=Path)
    parser.add_argument('--check-only', action='store_true', help='Verify candidate without packaging or output directories')
    args = parser.parse_args()
    try:
        candidate = args.candidate.resolve()
        build, checked, sources = candidate_check(candidate)
        prefix = build['ELF_sha256'][:16]
        if args.check_only:
            print(json.dumps(dict(status='closed_ROM_only_candidate_verified', candidate=candidate.name,
                                  ELF_sha256=build['ELF_sha256'], **checked), indent=2))
            return
        makerom = find_tool(args.makerom, 'MAKEROM', 'makerom')
        bannertool = find_tool(args.bannertool, 'BANNERTOOL', 'bannertool')
        icon, banner = artwork()
        license_text = dsp_license(sources['dsp_bootstrap.hpp'])
        work = (args.work or ROOT / ('build/package-integrated-' + prefix)).resolve()
        output = (args.output or ROOT / ('dist/sm64ds-rom-only-' + prefix)).resolve()
        require(not work.exists() and not output.exists(), 'Work and output must be new exclusive directories')
        require(not work.is_relative_to(candidate) and not candidate.is_relative_to(work)
                and not output.is_relative_to(candidate) and not candidate.is_relative_to(output)
                and not work.is_relative_to(output) and not output.is_relative_to(work),
                'Work/output/candidate directories must be separate')
        # Make work exclusively first. A partial work directory is retained on failure.
        work.mkdir(parents=True, exist_ok=False)
        rsf_copy = work / 'sm64ds-integrated.rsf'
        shutil.copyfile(RSF, rsf_copy)
        icon.save(work / 'icon.png')
        shutil.copyfile(APPROVED_BANNER, work / 'banner.cgfx')
        (work/'approved-artwork.json').write_text(json.dumps(dict(icon_sha256=digest(APPROVED_ICON), banner_cgfx_sha256=digest(APPROVED_BANNER), kind='owner-selected native 3D banner; screenshot is preview only'),indent=2)+'\n')
        banner.save(work / 'banner.png')
        with wave.open(str(work / 'silence.wav'), 'wb') as wav:
            wav.setparams((1, 2, 32000, 0, 'NONE', 'not compressed'))
            wav.writeframes(bytes(16000))
        cxi, cia = work / ('sm64ds-native-' + prefix + '.cxi'), work / ('sm64ds-native-' + prefix + '.cia')
        commands = [
            [bannertool, 'makesmdh', '-s', 'SM64DS Native', '-l', 'ROM-only experimental native port',
             '-p', 'SM64DS native contributors', '-i', work / 'icon.png', '-r', 'regionfree',
             '-f', 'visible,allow3d', '-o', work / 'icon.smdh'],
            [bannertool, 'makebanner', '-ci', work / 'banner.cgfx', '-a', work / 'silence.wav', '-o', work / 'banner.bnr'],
            [makerom, '-f', 'ncch', '-target', 't', '-rsf', rsf_copy,
             '-elf', candidate / ELF_NAME, '-icon', work / 'icon.smdh', '-banner', work / 'banner.bnr', '-o', cxi],
            [makerom, '-f', 'cia', '-target', 't', '-content', str(cxi) + ':0:0', '-ver', '0', '-o', cia],
        ]
        with (work / 'packaging.log').open('x') as log:
            for command in commands:
                command = [str(x) for x in command]
                log.write(json.dumps(command) + '\n')
                log.flush()
                subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT)
        result = verify(cia, candidate / ELF_NAME, build['ELF_sha256'])
        require(digest(candidate / IMAGE_NAME) == build['image_sha256']
                and digest(candidate / 'BUILD.json') == checked['BUILD_sha256'], 'Candidate changed while packaging')
        require(digest(rsf_copy) == digest(RSF), 'Packaging profile changed while packaging')
        # Public delivery is an allowlist; no candidate tree or source catalog is copied.
        output.mkdir(parents=True, exist_ok=False)
        shutil.copyfile(cia, output / cia.name)
        shutil.copyfile(cxi, output / cxi.name)
        image_dest = output / 'SD/3ds/sm64ds/sm64ds.3dsx'
        image_dest.parent.mkdir(parents=True)
        shutil.copyfile(candidate / IMAGE_NAME, image_dest)
        shutil.copyfile(candidate / 'LICENSE', output / 'LICENSE-vendor.txt')
        (output / 'LICENSE-DSP1.txt').write_text(license_text, encoding='utf-8')
        (output / 'LEEME.txt').write_text(instructions(prefix, checked, build.get('build_number')), encoding='ascii')
        (output / 'CREDITS.txt').write_text(
            'Native backend: Sebastien Breton and SM64DS 3DS port contributors (MIT).\n'
            'Original decompilation: Tango and sm64ds-decomp contributors (MIT).\n'
            'Local integration, ROM bootstrap and experimental presentation: EstebanPdN / contributors.\n'
            'DSP component lookup/layout: zoogie/DSP1 (MIT), commit 9b0180772d16dc688ad8b3527df294c11bb736a1.\n'
            'https://github.com/zoogie/DSP1\n'
            'Original menu artwork, fonts, game and character trademarks: Nintendo.\n'
            'Supply your own verified EU ROM for gameplay.\n', encoding='ascii')
        result.update(candidate_id=candidate.name, candidate_verification=checked,
                      build_number=build.get('build_number'),
                      input_image_sha256=build['image_sha256'], rom_only=True,
                      diagnostics_enabled_by_package=False, game_data_files_copied=0,
                      embedded_menu_artwork_bytes=329344, embedded_artwork_owner="Nintendo",
                      approved_home_artwork=dict(icon_source_sha256=digest(APPROVED_ICON),
                          banner_cgfx_sha256=digest(work/'banner.cgfx'),
                          banner_cbmd_sha256=digest(work/'banner.bnr'),
                          icon_smdh_sha256=digest(work/'icon.smdh'),
                          kind='native 3D CGFX banner; owner-selected Mario icon',
                          physical_HOME_Menu_verified=False),
                      DSP_firmware_files_copied=0, published=False,
                      package_tool_sha256=digest(Path(__file__)), rsf_sha256=digest(rsf_copy),
                      tools={name: dict(path=str(path), sha256=digest(path))
                             for name, path in [('makerom', makerom), ('bannertool', bannertool)]},
                      work_directory=str(work), delivery_directory=str(output))
        (work / 'verification.json').write_text(json.dumps(result, indent=2) + '\n')
        (output / 'verification.json').write_text(json.dumps(result, indent=2) + '\n')
        allowed = {cia.name, cxi.name, 'SD/3ds/sm64ds/sm64ds.3dsx', 'LICENSE-vendor.txt',
                   'LICENSE-DSP1.txt', 'LEEME.txt', 'CREDITS.txt', 'verification.json'}
        actual = {path.relative_to(output).as_posix() for path in output.rglob('*') if path.is_file()}
        require(actual == allowed, 'Unexpected delivery files')
        hashes = {name: digest(output / name) for name in sorted(allowed)}
        require(hashes[cia.name] == result['sha256'] and hashes[cxi.name] == digest(cxi)
                and hashes['SD/3ds/sm64ds/sm64ds.3dsx'] == build['image_sha256'], 'Delivery copy mismatch')
        (output / 'SHA256SUMS.txt').write_text(''.join(value + '  ' + name + '\n' for name, value in hashes.items()))
        print(json.dumps(dict(status='ROM_only_integrated_CIA_packaged_and_verified',
                              cia=str(output / cia.name), cxi=str(output / cxi.name),
                              sha256=result['sha256'], ELF_sha256=build['ELF_sha256'],
                              verification=str(output / 'verification.json'), work=str(work),
                              physical_hardware_tested=False, published=False), indent=2))
    except (ValueError, OSError, KeyError, TypeError, ImportError, subprocess.CalledProcessError) as error:
        parser.exit(1, 'Integrated packaging failed: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
