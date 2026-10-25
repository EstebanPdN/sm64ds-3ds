#!/usr/bin/env python3
"""Compare the native files service with ndspy for every common NARC member.

Reads local user ROMs only. Produces sizes/checksums and verification metadata,
never ROM payloads. This is host verification, not gameplay or performance.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import zlib
import struct


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--case',nargs=2,action='append',required=True,metavar=('ROM','PAK'))
    p.add_argument('--test',type=Path,default=Path('build/sanitize/test_narc'))
    p.add_argument('--output',type=Path,default=Path('build/game-data/narc-verification.json'))
    p.add_argument('--all-archives',action='store_true',help='Verify all 13 original ASMP0 archive ranges')
    args=p.parse_args()
    import ndspy.rom,ndspy.narc,ndspy.lz10,ndspy.codeCompression
    results=[]
    args.output.parent.mkdir(parents=True,exist_ok=True)
    for rom_path,pak in args.case:
        raw=Path(rom_path).read_bytes();rom=ndspy.rom.NintendoDSRom(raw)
        expected_crc={b'ASME':0xe6321562,b'ASMP':0x29715dec}.get(bytes(rom.idCode))
        if rom.version!=0 or expected_crc!=zlib.crc32(raw): raise ValueError('Unverified ROM profile')
        archive=ndspy.narc.NARC(rom.getFileByName('ARCHIVE/arc0.narc'))
        archives=[(0x8000,0x80c6,'ARCHIVE/arc0.narc')]
        command=[str(args.test),pak]
        if args.all_archives:
            if bytes(rom.idCode)!=b'ASMP':raise ValueError('Full table currently verified only for ASMP0')
            main=ndspy.codeCompression.decompress(rom.arm9)
            archives=[]
            for i in range(13):
                offset=0x0208ecf4-rom.arm9RamAddress+i*20
                base,end=struct.unpack_from('<HH',main,offset+8)
                pointer=struct.unpack_from('<I',main,offset+16)[0]-rom.arm9RamAddress
                path=main[pointer:pointer+64].split(b'\0')[0].decode().lstrip('/')
                archives.append((base,end,path))
            fixture=args.output.with_suffix('.ranges.txt')
            fixture.write_text(''.join(f'{base:x} {end:x} {path}\n' for base,end,path in archives))
            command.append(str(fixture))
        output=subprocess.check_output(command,text=True)
        rows=[line.split() for line in output.splitlines() if line.startswith('member ')]
        assert len(rows)==198==len(archive.files)
        total=0
        for index,row in enumerate(rows):
            data=bytes(archive.files[index])
            if data.startswith(b'LZ77'): data=ndspy.lz10.decompress(data[4:])
            assert (int(row[1],16),int(row[2]),int(row[3],16))==(
                0x8000+index,len(data),zlib.crc32(data)),f'Member mismatch: {index}'
            total+=len(data)
        archive_members=0;all_decoded_bytes=0
        if args.all_archives:
            rows=[line.split() for line in output.splitlines() if line.startswith('archive ')]
            expected=[]
            for base,end,path in archives:
                payloads=ndspy.narc.NARC(rom.getFileByName(path)).files
                assert len(payloads)==end-base
                for index,data in enumerate(payloads):
                    data=bytes(data)
                    if data.startswith(b'LZ77'):data=ndspy.lz10.decompress(data[4:])
                    expected.append((base+index,len(data),zlib.crc32(data)))
                    all_decoded_bytes+=len(data)
            actual=[(int(row[1],16),int(row[2]),int(row[3],16)) for row in rows]
            assert actual==expected
            archive_members=len(actual)
        code=rom.idCode.decode()
        (args.output.parent/f'narc-{code}.txt').write_text(output)
        results.append({'rom':code,'rom_sha256':hashlib.sha256(raw).hexdigest(),
            'common_members':198,'common_decoded_bytes':total,
            'all_archive_members':archive_members,'archives':len(archives),
            'all_decoded_bytes':all_decoded_bytes,
            'independent_decoder':'ndspy NARC and lz10','all_sizes_and_crc32_equal':True})
    report={'status':'passed','scope':'Host files service; no gameplay or hardware performance claim',
            'test_executable_sha256':hashlib.sha256(args.test.read_bytes()).hexdigest(),'roms':results}
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
