"""Reserve monotonically increasing local build identities, before input freeze."""
from pathlib import Path
import fcntl
import os
import re
import tempfile

PATTERN = re.compile(r'^#define SM64DS_BUILD_NUMBER ([0-9]+)$', re.M)


def reserve(root):
    root = Path(root)
    path = root / 'source/integrated_native/options/build_version.hpp'
    lock = root / 'build/integrated-native/build-number.lock'
    lock.parent.mkdir(parents=True, exist_ok=True)
    with lock.open('a') as stream:
        fcntl.flock(stream, fcntl.LOCK_EX)
        text = path.read_text()
        matches = PATTERN.findall(text)
        if len(matches) != 1:
            raise ValueError('Expected one build number in ' + str(path))
        number = int(matches[0]) + 1
        if number > 0x7fffffff:
            raise ValueError('Build number exhausted')
        text = PATTERN.sub('#define SM64DS_BUILD_NUMBER ' + str(number), text)
        descriptor, name = tempfile.mkstemp(prefix='.build-version-', dir=path.parent)
        try:
            with os.fdopen(descriptor, 'w') as output:
                output.write(text)
                output.flush()
                os.fsync(output.fileno())
            os.replace(name, path)
        finally:
            Path(name).unlink(missing_ok=True)
        return number
