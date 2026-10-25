#!/usr/bin/env python3
"""Package the native development preview as CIA; does not embed game assets."""
import argparse
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import wave

from package_verify import verify


def find_tool(explicit, variable, name):
    candidate = explicit or os.environ.get(variable) or shutil.which(name)
    if not candidate:
        raise ValueError(f'Set --{name} or {variable} to the installed {name} executable')
    path = pathlib.Path(candidate).expanduser().resolve()
    if not path.is_file() or not os.access(path, os.X_OK):
        raise ValueError(f'{name} is not executable: {path}')
    return path


def main():
    root = pathlib.Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=pathlib.Path, default=root / 'build/3ds/source/ctr/sm64ds-platform.elf')
    parser.add_argument('--makerom', type=pathlib.Path)
    parser.add_argument('--bannertool', type=pathlib.Path)
    parser.add_argument('--output', type=pathlib.Path, default=root / 'dist/sm64ds-native-preview')
    args = parser.parse_args()
    try:
        makerom = find_tool(args.makerom, 'MAKEROM', 'makerom')
        bannertool = find_tool(args.bannertool, 'BANNERTOOL', 'bannertool')
        elf = args.elf.resolve()
        if elf.read_bytes()[:5] != b'\x7fELF\x01':
            raise ValueError('Input must be a 32-bit native ELF')
        from PIL import Image, ImageDraw, ImageFont
    except (ValueError, OSError, ImportError) as error:
        parser.error(str(error))
    output = args.output.resolve()
    work = root / 'build/package'
    work.mkdir(parents=True, exist_ok=True)
    output.mkdir(parents=True, exist_ok=True)
    sd = output / '3ds/sm64ds'
    sd.mkdir(parents=True, exist_ok=True)
    # Original text-only development artwork; no Nintendo image or audio.
    for name, dimensions, lines in (
            ('icon', (48, 48), [('S64', 15, 9), ('DEV', 10, 28)]),
            ('banner', (256, 128), [('SM64DS', 30, 16), ('NATIVE PREVIEW', 14, 62), ('DEVELOPMENT BUILD', 11, 86)])):
        image = Image.new('RGB', dimensions, '#112038')
        draw = ImageDraw.Draw(image)
        draw.rectangle((3, 3, dimensions[0]-4, dimensions[1]-4), outline='#51cfdd', width=2)
        for text, size, y in lines:
            font = ImageFont.load_default(size=size)
            bounds = draw.textbbox((0, 0), text, font=font)
            draw.text(((dimensions[0] - (bounds[2]-bounds[0])) / 2, y), text, font=font, fill='white')
        image.save(work / f'{name}.png')
    with wave.open(str(work / 'silence.wav'), 'wb') as audio:
        audio.setparams((1, 2, 32000, 0, 'NONE', 'not compressed'))
        audio.writeframes(bytes(16000))
    commands = [
        [str(bannertool), 'makesmdh', '-s', 'SM64DS native preview', '-l', 'Asset importer and GPU preview; adventure not integrated',
         '-p', 'EstebanPdN / contributors', '-i', str(work/'icon.png'), '-r', 'regionfree', '-f', 'visible,allow3d', '-o', str(work/'icon.smdh')],
        [str(bannertool), 'makebanner', '-i', str(work/'banner.png'), '-a', str(work/'silence.wav'), '-o', str(work/'banner.bnr')],
        [str(makerom), '-f', 'ncch', '-target', 't', '-rsf', str(root/'packaging/sm64ds.rsf'), '-elf', str(elf),
         '-icon', str(work/'icon.smdh'), '-banner', str(work/'banner.bnr'), '-o', str(work/'sm64ds-preview.cxi')],
        [str(makerom), '-f', 'cia', '-target', 't', '-content', str(work/'sm64ds-preview.cxi')+':0:0',
         '-ver', '0', '-o', str(work/'sm64ds-preview.cia')],
    ]
    with (work/'packaging.log').open('w') as log:
        for command in commands:
            log.write(json.dumps(command) + '\n'); log.flush()
            subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT)
    result = verify(work/'sm64ds-preview.cia')
    result['input_elf_sha256'] = hashlib.sha256(elf.read_bytes()).hexdigest()
    result['tools'] = {name: {'path': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
                       for name, path in [('makerom', makerom), ('bannertool', bannertool)]}
    result['stage'] = 'asset importer and native GPU preview; adventure not integrated'
    (work/'verification.json').write_text(json.dumps(result, indent=2)+'\n')
    shutil.copyfile(work/'sm64ds-preview.cia', output/'sm64ds-preview.cia')
    shutil.copyfile(work/'verification.json', output/'verification.json')
    shutil.copyfile(root/'source/vendor/ntr/LICENSE', output/'LICENSE-upstream.txt')
    (sd/'LEEME.txt').write_text('Coloca tu ROM como rom.nds en esta carpeta.\nRuta SD: /3ds/sm64ds/rom.nds\nLa aplicacion extrae sus recursos al primer arranque.\n')
    (output/'LEEME.txt').write_text(
        'SM64DS native preview - compilacion de desarrollo\n\n'
        'Este CIA contiene el importador y el visor nativo del castillo.\n'
        'La aventura, el menu original y el control del personaje aun no estan integrados.\n'
        'No representa el juego terminado ni acredita 60 FPS en una Old 3DS fisica.\n\n'
        'Instala sm64ds-preview.cia con tu instalador homebrew habitual.\n'
        'Perfil de arranque de esta preview: ROM USA ASME revision 0.\n'
        'Copia esa ROM a /3ds/sm64ds/rom.nds en la SD.\n'
        'La extraccion genera assets.pak en esa misma carpeta.\n'
        'No se incluye ninguna ROM ni recursos extraidos del juego en este paquete.\n'
        'START sale. Los controles actuales mueven la camara del visor, no al personaje.\n'
        'Title ID de desarrollo: 000400000F646D00. Compatible por cabecera con Old/New.\n')
    print(json.dumps({'cia': str(output/'sm64ds-preview.cia'), 'sha256': result['sha256'], 'bytes': result['bytes'],
                      'verification': str(work/'verification.json')}, indent=2))


if __name__ == '__main__':
    main()
