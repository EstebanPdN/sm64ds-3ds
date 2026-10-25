# Build natively on each target OS; PyInstaller does not cross-compile.
from pathlib import Path
import sys
base = Path(SPECPATH)
a = Analysis([str(base / 'app.py')], pathex=[str(base)], binaries=[],
             datas=[(str(base / 'recipes.json.zlib'), '.'), (str(base / 'assets'), 'assets')], hiddenimports=[],
             hookspath=[], hooksconfig={}, runtime_hooks=[],
             excludes=['PySide6.QtWebEngineCore', 'PySide6.QtWebEngineWidgets',
                       'PySide6.QtQml', 'PySide6.QtQuick', 'tkinter'])
pyz = PYZ(a.pure)
exe = EXE(pyz, a.scripts, [], exclude_binaries=True, name='SM64DS Packer',
          debug=False, bootloader_ignore_signals=False, strip=False, upx=False,
          console=False, icon=str(base / 'assets' / ('icon.icns' if sys.platform == 'darwin' else 'icon.ico')))
coll = COLLECT(exe, a.binaries, a.datas, strip=False, upx=False, name='SM64DS Packer')
if sys.platform == 'darwin':
    app = BUNDLE(coll, name='SM64DS Packer.app', icon=str(base / 'assets' / 'icon.icns'), bundle_identifier='local.sm64ds.packer',
                 info_plist={'NSHighResolutionCapable': True,
                             'CFBundleShortVersionString': '1.0.0',
                             'LSMinimumSystemVersion': '14.0'})
