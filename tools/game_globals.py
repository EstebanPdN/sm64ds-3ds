#!/usr/bin/env python3
"""Emit genuine zero-initialized module storage for the original game.

Only symbols explicitly classified as BSS are eligible. Original relative
offsets are retained in one arena per module; initialized data is never filled
with dummy zeroes. The generated object is a link component, not a boot proof.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--frontier',type=Path,default=Path('build/native-core/symbols.json'))
    parser.add_argument('--output',type=Path,default=Path('build/native-globals'))
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[1]
    config=root/'upstream/config/arm9'
    frontier=json.loads(args.frontier.read_text())
    needed=set(frontier['playable']['unresolved'])
    toolchain=Path(os.environ.get('DEVKITPRO','/opt/devkitpro'))/'devkitARM/bin'
    linked=root/'build/native-core/gameplay-frontier.o'
    if not linked.exists():
        parser.error('Run native_core_inventory.py --link-frontier first')
    defined={line.split()[-1] for line in subprocess.check_output(
        [str(toolchain/'arm-none-eabi-nm'),'-g','--defined-only',str(linked)],text=True).splitlines()}
    modules=[]
    supplied=set()
    for symbols in sorted(config.rglob('symbols.txt')):
        manifest=symbols.with_name('delinks.txt')
        declaration=re.search(r'^    \.bss\s+start:0x([0-9a-fA-F]+) end:0x([0-9a-fA-F]+).*align:(\d+)',
                              manifest.read_text(),re.M)
        if not declaration:
            continue
        start,end=int(declaration[1],16),int(declaration[2],16)
        alignment=int(declaration[3])
        name='arm9' if symbols.parent==config else symbols.parent.name
        entries=[]
        existing=[]
        for line in symbols.read_text().splitlines():
            match=re.fullmatch(r'(\w+) kind:bss addr:0x([0-9a-fA-F]+)(?:\s+.*)?',line)
            if not match:
                continue
            symbol,address=match[1],int(match[2],16)
            if not start<=address<end:
                raise ValueError(f'{name}: {symbol} is outside the declared BSS')
            if symbol in defined:
                existing.append(symbol)
            if symbol in needed:
                if symbol in supplied:
                    raise ValueError(f'Ambiguous BSS symbol: {symbol}')
                supplied.add(symbol)
                entries.append({'symbol':symbol,'offset':address-start,'original_address':address})
        if not entries:
            continue
        if existing:
            raise ValueError(f'{name}: existing independent BSS definitions must be reconciled: {existing}')
        modules.append({'name':name,'original_start':start,'original_end':end,
                        'size':end-start,'alignment':alignment,'entries':entries})
    lines=['/* Generated from verified upstream BSS metadata. No ROM payload. */']
    for module in modules:
        arena='sm64ds_bss_'+module['name']
        lines.extend([f'.section .bss.{arena}, "aw", %nobits',f'.balign {module["alignment"]}',
                      f'.global {arena}',f'.type {arena}, %object',f'{arena}:',
                      f'.space {module["size"]}',f'.size {arena}, . - {arena}'])
        for item in module['entries']:
            symbol=item['symbol']
            lines.extend([f'.global {symbol}',f'.type {symbol}, %object',
                          f'.set {symbol}, {arena} + {item["offset"]}'])
    # These are linker constants: source passes &overlay_N as the numeric ID.
    # They are never backing memory or callable function pointers.
    overlay_ids=[]
    for symbol in sorted(needed):
        match=re.fullmatch(r'overlay_(\d+)',symbol)
        if match and int(match[1])<=102:
            lines.extend([f'.global {symbol}',f'.set {symbol}, {int(match[1])}'])
            overlay_ids.append(symbol)
    lines.append('.section .note.GNU-stack,"",%progbits')
    args.output.mkdir(parents=True,exist_ok=True)
    assembly=args.output/'game_bss.S'
    assembly.write_text('\n'.join(lines)+'\n')
    obj=args.output/'game_bss.o'
    subprocess.run([str(toolchain/'arm-none-eabi-gcc'),'-march=armv6k','-mfloat-abi=hard',
                    '-c',str(assembly),'-o',str(obj)],check=True)
    # Verify the assembler kept every declared relative offset. No load address
    # is presumed: the final ELF linker chooses the arena's native address.
    emitted={}
    for line in subprocess.check_output([str(toolchain/'arm-none-eabi-nm'),str(obj)],text=True).splitlines():
        columns=line.split()
        if len(columns)==3:emitted[columns[2]]=int(columns[0],16)
    for module in modules:
        base=emitted['sm64ds_bss_'+module['name']]
        for item in module['entries']:
            if emitted[item['symbol']]-base!=item['offset']:
                raise RuntimeError('Assembler altered BSS offsets')
    report={'upstream_commit':frontier['upstream_commit'],'scope':'Zero-initialized BSS only; no original code execution',
            'object':str(obj),'module_count':len(modules),'bss_symbols':len(supplied),
            'reserved_bytes':sum(m['size'] for m in modules),'linker_constants':overlay_ids,
            'modules':modules}
    (args.output/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='modules'},indent=2))


if __name__=='__main__':
    main()
