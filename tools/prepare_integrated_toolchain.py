"""Give generated final-link helpers the same configurable SDK as CMake."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def prepare(output):
    output = Path(output).resolve()
    if output.is_relative_to((ROOT / 'third_party').resolve()):
        raise ValueError('Pinned vendor tree is immutable')
    updates = {}
    for name in ('build3ds/build_game.py', 'build3ds/build_menu_probe.py'):
        path = output / name
        if not path.resolve().is_relative_to(output):
            raise ValueError('Generated build helper escapes output')
        text = path.read_text()
        old = "'/opt/devkitpro/devkitARM/bin/arm-none-eabi-g++'"
        if text.count(old) != 1:
            raise ValueError('Final-link compiler contract changed: ' + name)
        text = text.replace(old, "arm_tool('g++')", 1)
        if name.endswith('build_menu_probe.py'):
            old = "'/opt/devkitpro/tools/bin/3dsxtool'"
            if text.count(old) != 1:
                raise ValueError('3DSX packaging contract changed')
            text = text.replace(old, "sdk_tool('3dsxtool')", 1)
        updates[path] = text.replace('from pathlib import Path\n',
            'from pathlib import Path\nfrom devkitpro_tools import arm_tool, sdk_tool\n', 1).encode()
    helper = output / 'build3ds/devkitpro_tools.py'
    if not helper.resolve().is_relative_to(output):
        raise ValueError('SDK helper escapes output')
    updates[helper] = (ROOT / 'tools/devkitpro_tools.py').read_bytes()
    for path, raw in updates.items():
        path.write_bytes(raw)
    return [str(path.relative_to(output)) for path in updates]
