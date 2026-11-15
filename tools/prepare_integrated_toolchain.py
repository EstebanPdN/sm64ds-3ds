"""Give generated final-link helpers the same configurable SDK as CMake."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def response_link(text):
    old='result=subprocess.run(command,cwd=out,capture_output=True,text=True)'
    if text.count(old)!=1:raise ValueError('Final-link response-file contract changed')
    return text.replace(old,'''# Retain expanded JSON argv; keep both driver and linker child argv bounded.
from compiler_link import run_link
result=run_link(command,out,name,capture_output=True,text=True)''',1)


def prepare_response_link(output):
    """Upgrade an already SDK-staged generated checkout without restaging it."""
    output=Path(output).resolve();path=output/'build3ds/build_menu_probe.py'
    if output.is_relative_to((ROOT/'third_party').resolve()) or not path.resolve().is_relative_to(output):
        raise ValueError('Response-file preparation requires a generated helper')
    helper=output/'build3ds/compiler_link.py'
    if not helper.resolve().is_relative_to(output):raise ValueError('Response helper escaped output')
    patched=response_link(path.read_text());path.write_text(patched)
    helper.write_bytes((ROOT/'tools/compiler_link.py').read_bytes())
    return str(path.relative_to(output))


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
            text = response_link(text)
        updates[path] = text.replace('from pathlib import Path\n',
            'from pathlib import Path\nfrom devkitpro_tools import arm_tool, sdk_tool\n', 1).encode()
    helper = output / 'build3ds/devkitpro_tools.py'
    if not helper.resolve().is_relative_to(output):
        raise ValueError('SDK helper escapes output')
    updates[helper] = (ROOT / 'tools/devkitpro_tools.py').read_bytes()
    helper=output/'build3ds/compiler_link.py'
    if not helper.resolve().is_relative_to(output):raise ValueError('Response helper escaped output')
    updates[helper]=(ROOT/'tools/compiler_link.py').read_bytes()
    for path, raw in updates.items():
        path.write_bytes(raw)
    return [str(path.relative_to(output)) for path in updates]
