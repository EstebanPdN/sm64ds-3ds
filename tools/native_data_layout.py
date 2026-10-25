"""Emit owned native storage for declared ROM data sections, without ROM bytes.

This only allocates storage. Symbol resolution and relocation completeness are
the caller's responsibility; neither unsupported data nor code is synthesized.
"""


def section_name(index):
    return 'sm64ds_native_section_' + str(index)


def owner(regions, module, address):
    matches = [(i, r) for i, r in enumerate(regions)
               if r['module'] == module and r['start'] <= address < r['end']]
    if len(matches) > 1:
        raise ValueError('Overlapping original sections')
    return matches[0] if matches else None


def expression(regions, module, address):
    match = owner(regions, module, address)
    if match is None:
        return None
    index, region = match
    return section_name(index) + '+' + hex(address - region['start'])


def assembly(regions):
    lines = ['.syntax unified']
    for index, region in enumerate(regions):
        size = region['end'] - region['start']
        if not 0 < size <= 0x1000000 or region['section'] not in ('.rodata', '.data', '.bss'):
            raise ValueError('Not a bounded nonempty data section')
        name = section_name(index)
        lines += [f'.section .bss.{name},"aw",%nobits', '.balign 32',
                  f'.global {name}', f'.type {name},%object',
                  f'{name}:', f'.space {size}', f'.size {name}, .-{name}']
    lines += ['.section .note.GNU-stack,"",%progbits']
    return '\n'.join(lines) + '\n'
