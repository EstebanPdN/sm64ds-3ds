#!/usr/bin/env python3
"""Verify EU logical handles and map them to another ROM by exact NitroFS path.

Writes a private metadata report only, never game payloads or a guessed numeric
delta. This does not activate the mapping in the native runtime.
Requires ndspy and the pinned upstream checkout.
"""
import argparse
import hashlib
import importlib.util
import json
import pathlib
import sys


def catalog(source, target, upstream):
    import ndspy.rom
    spec = importlib.util.spec_from_file_location("sm64ds_upstream_catalog", upstream / "tools/asset_catalog.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    source_bytes, target_bytes = source.read_bytes(), target.read_bytes()
    eu = ndspy.rom.NintendoDSRom(source_bytes)
    other = ndspy.rom.NintendoDSRom(target_bytes)
    if eu.idCode != b"ASMP" or eu.version != 0:
        raise ValueError("Source must be European ASMP revision 0")
    assets, handles = module.catalogs_from_rom(source)
    target_paths = {}
    for file_id in range(len(other.files)):
        path = other.filenames.filenameOf(file_id)
        if path is None:
            continue
        if path in target_paths:
            raise ValueError("Duplicate target NitroFS path: " + path)
        target_paths[path] = file_id
    if len({a.path for a in assets}) != len(assets):
        raise ValueError("Duplicate source NitroFS path")
    if len({h.path for h in handles}) != len(handles):
        raise ValueError("Duplicate source handle path")
    rows = []
    for h in handles:
        file_id = target_paths.get(h.path)
        rows.append({"source_handle": h.handle, "source_fat_id": h.file_id,
                     "path": h.path, "target_fat_id": file_id,
                     "payload_identical": None if file_id is None else
                         eu.files[h.file_id] == other.files[file_id]})
    return {"schema": 1, "runtime_integrated": False,
            "source": {"code": eu.idCode.decode(), "revision": eu.version,
                       "sha256": hashlib.sha256(source_bytes).hexdigest()},
            "target": {"code": other.idCode.decode(), "revision": other.version,
                       "sha256": hashlib.sha256(target_bytes).hexdigest()},
            "source_named_files": len(assets), "source_handles": len(rows),
            "matched": sum(r["target_fat_id"] is not None for r in rows),
            "missing": sum(r["target_fat_id"] is None for r in rows),
            "identical_payloads": sum(r["payload_identical"] is True for r in rows),
            "handles": rows}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=pathlib.Path)
    parser.add_argument("target", type=pathlib.Path)
    parser.add_argument("--upstream", type=pathlib.Path, default=pathlib.Path("upstream"))
    parser.add_argument("--output", type=pathlib.Path, default=pathlib.Path("build/game-data/eu-file-catalog.json"))
    args = parser.parse_args()
    if args.output.resolve() in (args.source.resolve(), args.target.resolve()):
        parser.error("Output cannot replace either ROM")
    report = catalog(args.source, args.target, args.upstream)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({k: v for k, v in report.items() if k != "handles"}, indent=2))


if __name__ == "__main__":
    main()
