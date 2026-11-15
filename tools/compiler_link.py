"""Link with ordered response files and compact paths for GCC's child argv."""
import hashlib
import os
from pathlib import Path
import shlex
import subprocess


def run_link(command,directory,name,**options):
    directory=Path(directory);prefix=hashlib.sha256(name.encode()).hexdigest()[:16]
    arguments=[];pending=[];groups=0
    def flush():
        nonlocal groups
        if not pending:return
        # A simple basename avoids -Wl's comma splitting even when the working
        # directory contains commas/spaces. GCC must pass this @file to ld,
        # rather than expanding thousands of objects for its collect2 child.
        file=f'link-{prefix}-objects-{groups}.rsp';groups+=1
        (directory/file).write_text('\n'.join(shlex.quote(value) for value in pending)+'\n')
        arguments.append('-Wl,@'+file);pending.clear()
    for argument in command[1:]:
        if not argument.startswith('-') and Path(argument).suffix in ('.o','.a'):
            # GCC collect2 can expand linker response files before spawning
            # ld. Compact these 4,000+ input paths as well; outer @files alone
            # do not solve macOS ARG_MAX in a deeply nested checkout.
            absolute=(directory/argument).resolve()
            relative=os.path.relpath(absolute,directory.resolve())
            pending.append(relative if len(relative)<len(argument) else argument)
        else:
            flush();arguments.append(argument)
    flush()
    response=directory/f'link-{prefix}-driver.rsp'
    response.write_text('\n'.join(shlex.quote(value) for value in arguments)+'\n')
    return subprocess.run([command[0],'@'+str(response.resolve())],cwd=directory,**options)
