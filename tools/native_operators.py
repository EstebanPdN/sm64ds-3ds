"""Keep the original game's allocator ABI private to its ARM11 objects.

Apply to the rebuilt game and its adapters BEFORE linking the C++ runtime.
Platform objects must retain their normal new/delete references. An unknown
operator is an error rather than an implicit change of allocator ownership.
"""
import subprocess

SYMBOLS = {
    '_Znwj': 'sm64ds_game_operator_new',
    '_ZdlPv': 'sm64ds_game_operator_delete',
    '_ZdlPvj': 'sm64ds_game_operator_delete_sized',
}


def copy_isolated(tool, source, destination, cwd):
    command = [tool + 'objcopy']
    for original, private in SYMBOLS.items():
        command += ['--redefine-sym', original + '=' + private]
    subprocess.run([*command, str(source), str(destination)], cwd=cwd, check=True)


def verify_isolated(tool, objects, cwd):
    listing = subprocess.check_output(
        [tool + 'nm', '-A', '-g', *map(str, objects)], cwd=cwd, text=True)
    operators = [line for line in listing.splitlines() if line.split() and
                 line.split()[-1].startswith(('_Znw', '_Zna', '_Zdl', '_Zda'))]
    if operators:
        raise ValueError('Unisolated C++ allocator operations: ' + '\n'.join(operators))
    return listing
