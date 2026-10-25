#!/usr/bin/env python3
"""Remove relocation records for original overlay-ID constants, not pointers.

GCC represents `&overlay_100` as ABS32 although the DS symbol means integer 100.
The linked word is already correct; a homebrew loader must not rebase that ID.
All loadable ELF bytes are preserved. Only matching relocation-table entries
and their section sizes change in a separate packaging copy.
"""
import argparse
import json
from pathlib import Path
import re
import struct

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path);parser.add_argument('output',type=Path)
    args=parser.parse_args();data=bytearray(args.source.read_bytes())
    if data[:7]!=b'\x7fELF\x01\x01\x01':raise ValueError('Expected little-endian ELF32')
    shoff=struct.unpack_from('<I',data,32)[0]
    shsize,shnum=struct.unpack_from('<HH',data,46)
    if shsize!=40:raise ValueError('Unsupported section header')
    sections=[struct.unpack_from('<10I',data,shoff+i*shsize) for i in range(shnum)]
    removed=[]
    for index,section in enumerate(sections):
        _,kind,_,_,offset,size,link,target,_,entry_size=section
        if kind!=9:continue
        if entry_size!=8:raise ValueError('Unexpected REL shape')
        symbols=sections[link];strings=sections[symbols[6]]
        target_section=sections[target];kept=bytearray()
        for row in range(offset,offset+size,8):
            address,info=struct.unpack_from('<II',data,row)
            symbol=struct.unpack_from('<IIIBBH',data,symbols[4]+(info>>8)*symbols[9])
            string_start=strings[4]+symbol[0];string_end=data.index(0,string_start)
            name=bytes(data[string_start:string_end]).decode('ascii')
            match=re.fullmatch(r'overlay_(\d+)',name)
            if info&255==2 and match:
                value=int(match[1])
                word=struct.unpack_from('<I',data,target_section[4]+address-target_section[3])[0]
                if symbol[5]!=0xfff1 or symbol[1]!=value or word!=value:
                    raise ValueError('Overlay ID relocation is not a resolved integer constant')
                removed.append({'symbol':name,'address':address,'value':value})
            else:kept.extend(data[row:row+8])
        data[offset:offset+len(kept)]=kept
        struct.pack_into('<I',data,shoff+index*shsize+20,len(kept))
    args.output.write_bytes(data)
    args.output.with_suffix('.relocations.json').write_text(json.dumps({
        'scope':'Packaging only; original overlay IDs remain integers',
        'removed_constant_relocations':removed},indent=2)+'\n')
    print(f'Preserved executable bytes; removed {len(removed)} overlay-ID relocation records')

if __name__=='__main__':main()
