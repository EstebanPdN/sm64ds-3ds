#!/usr/bin/env python3
"""Find relocation-masked EU function identities in the user's US image.

This is evidence generation, not a global delta or a loader. A match requires
every non-relocation byte of the entire function to agree, and exactly one
candidate in its own module. Data addresses inferred from matching literal
pools still need size/type/ABI validation before any native runtime uses them.
No ROM bytes are written into the report. Requires ndspy for local decoding.
"""
import argparse
import bisect
import collections
import hashlib
import json
import pathlib
import re
import struct
import sys
import zlib


def parse_address(value):
    return int(value, 0) if isinstance(value, str) else value


def module_name(value):
    if value == "main":
        return "arm9"
    m = re.fullmatch(r"overlay\((\d+)\)", value)
    return f"ov{int(m[1]):03d}" if m else value


def load_metadata(root, module):
    folder = root / "config/arm9"
    if module != "arm9":
        folder /= "overlays/" + module
    relocs = []
    for line in (folder / "relocs.txt").read_text().splitlines():
        m = re.match(r"from:0x([0-9a-f]+) kind:(\S+) to:0x([0-9a-f]+) module:(\S+)", line)
        if m:
            relocs.append((int(m[1], 16), m[2], int(m[3], 16), module_name(m[4])))
    symbols = {}
    for line in (folder / "symbols.txt").read_text().splitlines():
        m = re.match(r"(\S+) kind:(\S+) addr:0x([0-9a-f]+)", line)
        if m:
            symbols[int(m[3], 16)] = {"symbol": m[1], "kind": m[2]}
    relocs.sort(key=lambda r: r[0])
    if len({r[0] for r in relocs}) != len(relocs):
        raise ValueError(f'Duplicate relocation sites in {module}')
    return relocs, symbols


def longest_exact_run(mask):
    best = start = end = 0
    for i, v in enumerate(mask + b"\0"):
        if v == 255:
            continue
        if i - start > best:
            best, end = i - start, i
        start = i + 1
    return end - best, end


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("rom", type=pathlib.Path)
    p.add_argument("--source-rom",type=pathlib.Path,
                   help="Use all full functions from the supplied ASMP0 ROM, not only the near-miss corpus")
    p.add_argument("--upstream", type=pathlib.Path, default=pathlib.Path("upstream"))
    p.add_argument("--output", type=pathlib.Path, default=pathlib.Path("build/game-data/region-probe.json"))
    args = p.parse_args()
    import ndspy.rom
    import ndspy.codeCompression

    rom_bytes = args.rom.read_bytes()
    rom = ndspy.rom.NintendoDSRom(rom_bytes)
    if rom.idCode != b"ASME" or rom.version != 0:
        p.error("This probe's investigated target is ASME revision 0 only")
    overlays = rom.loadArm9Overlays()
    images = {"arm9": (rom.arm9RamAddress, ndspy.codeCompression.decompress(rom.arm9))}
    images.update({f"ov{i:03d}": (v.ramAddress, bytes(v.data)) for i, v in overlays.items()})
    metadata = {m: load_metadata(args.upstream, m) for m in ("arm9", "ov002", "ov007")}
    relocation_sites={m:[r[0] for r in meta[0]] for m,meta in metadata.items()}
    source_sha=None
    if args.source_rom:
        source_bytes=args.source_rom.read_bytes()
        source_rom=ndspy.rom.NintendoDSRom(source_bytes)
        if (source_rom.idCode!=b'ASMP' or source_rom.version!=0 or
                zlib.crc32(source_bytes)!=0x29715dec):
            p.error('Source must be the verified European ASMP0 image, CRC32 29715dec')
        source_sha=hashlib.sha256(source_bytes).hexdigest()
        source_images={'arm9':(source_rom.arm9RamAddress,
            ndspy.codeCompression.decompress(source_rom.arm9))}
        source_images.update({f'ov{i:03d}':(v.ramAddress,bytes(v.data))
                              for i,v in source_rom.loadArm9Overlays().items()})
        source_functions=[]
        for module,meta in metadata.items():
            base,data=source_images[module]
            for address,info in meta[1].items():
                size=re.fullmatch(r'function\(arm,size=0x([0-9a-f]+)\)',info['kind'])
                if not size: continue
                length=int(size[1],16);offset=address-base
                if offset<0 or offset+length>len(data):
                    raise ValueError(f'Function outside verified source image: {module}:{info["symbol"]}')
                source_functions.append({'module':module,'addr':address,
                    'name':info['symbol'],'target_hex':data[offset:offset+length].hex()})
    else:
        source_functions=[json.loads(line) for line in
            (args.upstream/'nearmiss/db.jsonl').read_text().splitlines()]
    functions, mappings = [], collections.defaultdict(list)
    seen = set()
    for row in source_functions:
        module = row.get("module")
        if module not in metadata:
            continue
        address = parse_address(row["addr"])
        code = bytes.fromhex(row["target_hex"])
        key = module, address, code
        if key in seen:
            continue
        seen.add(key)
        if len(code) % 4:
            continue
        mask = bytearray(b"\xff" * len(code))
        sites=relocation_sites[module]
        relocs=metadata[module][0][bisect.bisect_left(sites,address):
                                   bisect.bisect_left(sites,address+len(code))]
        safe = True
        for origin, kind, target, target_module in relocs:
            off = origin - address
            if off % 4 or off + 4 > len(code):
                safe = False
                break
            word = struct.unpack_from("<I", code, off)[0]
            if kind == "load":
                # Only a complete literal word is masked. Instructions reading
                # it, including the PC-relative offset, must match exactly.
                if word != target:
                    safe = False
                    break
                mask[off:off+4] = b"\0" * 4
            elif kind == "arm_call" and word & 0x0e000000 == 0x0a000000:
                mask[off:off+3] = b"\0" * 3
            else:
                safe = False
                break
        if not safe:
            continue
        start, end = longest_exact_run(mask)
        if end - start < 16:
            continue
        base, data = images[module]
        matches, search = [], 0
        anchor = code[start:end]
        while True:
            location = data.find(anchor, search)
            if location < 0:
                break
            search = location + 1
            off = location - start
            if off < 0 or off % 4 or off + len(code) > len(data):
                continue
            candidate = data[off:off+len(code)]
            if all((a & m) == (b & m) for a, b, m in zip(code, candidate, mask)):
                matches.append(off)
        record = {"module": module, "symbol": row["name"], "eu_address": address,
                  "bytes": len(code), "exact_byte_count": mask.count(255),
                  "candidate_addresses": [base + x for x in matches]}
        functions.append(record)
        if len(matches) != 1:
            continue
        native = base + matches[0]
        mappings[(module, address)].append({"us_address": native, "function": row["name"], "via": "full_function"})
        for origin, kind, target, target_module in relocs:
            off = matches[0] + origin - address
            word = struct.unpack_from("<I", data, off)[0]
            if kind == "load":
                destination = word
            else:
                imm = word & 0xffffff
                if imm & 0x800000:
                    imm -= 0x1000000
                destination = base + off + 8 + imm * 4
            mappings[(target_module, target)].append({"us_address": destination,
                "function": row["name"], "via": kind, "eu_site": origin,
                "us_site": base + off})
    result = []
    for (module, address), evidence in sorted(mappings.items()):
        info = metadata.get(module, ([], {}))[1].get(address, {})
        candidates = sorted({e["us_address"] for e in evidence})
        result.append({"module": module, "eu_address": address, **info,
                       "us_candidates": candidates, "conflict": len(candidates) != 1,
                       "independent_function_count": len({e["function"] for e in evidence}),
                       "evidence": evidence})
    report = {"schema": 1, "rom_sha256": hashlib.sha256(rom_bytes).hexdigest(),
              "source_rom_sha256":source_sha,
              "game_code": rom.idCode.decode(), "revision": rom.version,
              "method": "Unique full-function match, masking config-declared relocations only",
              "loadable_globals": False,
              "limitations": ["No data sizes or vtable ABI verified", "No unobserved region delta inferred",
                  "Source: complete verified ASMP0 function images" if source_sha else "Near-miss corpus is incomplete"],
              "functions": functions, "mappings": result}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    matched = sum(len(r["candidate_addresses"]) == 1 for r in functions)
    conflict = sum(r["conflict"] for r in result)
    print(f"Unique whole-function matches: {matched}/{len(functions)}; address mappings: {len(result)}; conflicts: {conflict}")
    print(f"Report: {args.output}; no initialized globals emitted")


if __name__ == "__main__":
    main()
