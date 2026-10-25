"""Resolve SDK executables without tying builds to one installation directory.

DEVKITARM overrides the ARM tool directory; DEVKITPRO controls the other SDK
tools. An explicit installation is authoritative so a typo cannot silently
select a different compiler from PATH. Results remain strings for subprocess.
"""
import os
from pathlib import Path
import shutil


def devkitpro_root():
    return Path(os.environ.get('DEVKITPRO', '/opt/devkitpro')).expanduser()


def arm_tool(name):
    root = Path(os.environ.get('DEVKITARM', devkitpro_root() / 'devkitARM')).expanduser()
    return _executable(root / 'bin' / ('arm-none-eabi-' + name),
                       'arm-none-eabi-' + name,
                       bool(os.environ.get('DEVKITARM') or os.environ.get('DEVKITPRO')))


def sdk_tool(name):
    return _executable(devkitpro_root() / 'tools' / 'bin' / name, name,
                       bool(os.environ.get('DEVKITPRO')))


def _executable(path, name, explicit):
    if path.is_file() and os.access(path, os.X_OK):
        return str(path.resolve())
    found = None if explicit else shutil.which(name)
    if found:
        return found
    raise FileNotFoundError(
        f'Missing devkitPro executable: {path}. Set DEVKITPRO/DEVKITARM '
        'to the installed SDK or add its tools to PATH.')
