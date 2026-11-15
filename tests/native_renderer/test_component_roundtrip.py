"""Every exported semantic field must survive generated native C++ storage."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory() as directory:
    output=Path(directory)/'roundtrip.json'
    for executable, source in [(sys.argv[1],sys.argv[3]),(sys.argv[2],sys.argv[4])]:
        subprocess.run([executable,str(output)],check=True)
        expected=json.loads(Path(source).read_text())
        actual=json.loads(output.read_text())
        assert actual==expected,(source,[k for k in expected if expected[k]!=actual.get(k)])
print('PASS: exact scene JSON -> compiled native fixture -> scene JSON, including skin/toon/highlight')
