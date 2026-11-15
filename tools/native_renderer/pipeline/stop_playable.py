#!/usr/bin/env python3
"""Stop one owned playable session (its own process group only)."""
from pathlib import Path
import json, os, signal, sys, time

instance = Path(sys.argv[1]).resolve()
pid = json.loads((instance / 'process.json').read_text())['pid']
try:
    os.killpg(pid, signal.SIGTERM)
    for _ in range(30):
        time.sleep(.1); os.killpg(pid, 0)
    os.killpg(pid, signal.SIGKILL)
    print('KILLED', pid)
except ProcessLookupError:
    print('STOPPED', pid)
