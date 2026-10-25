#!/usr/bin/env python3
"""Static-only review of the ROM archive bootstrap repair; reads no old RAM."""
import json
import re
import subprocess
from pathlib import Path
import audit_native_family_virtuals as proof

ROOT=proof.ROOT
SHA='1e1eef06dff21c7a4ded69c93366f5c3d2718f8190d129abe06ffb94dec78ba0'


def main():
    elf=proof.Elf(ROOT/'build/3ds/source/ctr/sm64ds-native.elf')
    assert proof.sha(elf.raw)==SHA
    card=ROOT/'source/game_port/native_card.cpp'
    runtime=ROOT/'source/game_port/native_runtime.cpp'
    c,r=card.read_text(),runtime.read_text()
    helper=c[c.index('int sm64ds_native_register_rom_archive() {'):c.index('// Same 8-byte FSFileID')]
    bootstrap=r[r.index('extern "C" void func_02018aa4() {'):]
    assert bootstrap.index('!loaded || handles.empty()') < bootstrap.index('sm64ds_native_register_rom_archive()') < bootstrap.index('data_0209d3b8=handles.data()') < bootstrap.index('data_0209d3b4=data_020a8074')
    idem=helper[helper.index('if(rom_registry_ready)'):helper.index('if(data_020a8048)')]
    assert all(t in idem for t in ['data_020a8048!=data_020a8074','word(data_020a8074,0)!=rom_key','word(data_020a8074,8)!=0','return 1;'])
    assert all(t not in idem for t in ['func_0205cd34','func_0205cc80','word(data_020a8074,4)','memset'])
    assert helper.index('if(data_020a8048)') < helper.index('u32(data_0209a048)') < helper.index('func_0205cd34(data_020a8074)') < helper.index('func_0205cc80(data_020a8074,data_0209a048,3)') < helper.index('rom_registry_ready=true')
    assert 'func_0205cb68' not in helper and 'func_0205c7c4' not in helper
    nm=subprocess.check_output([proof.TOOL+'nm','--defined-only',str(elf.path)],text=True)
    ownership={name:[line for line in nm.splitlines() if line.split()[-1]==name]
        for name in ['sm64ds_native_register_rom_archive','func_02018aa4','func_0205cd34','func_0205cc80']}
    assert all(len(lines)==1 and lines[0].split()[1]=='T' for lines in ownership.values())
    hd=proof.disassemble_native(elf.path,'sm64ds_native_register_rom_archive')
    bd=proof.disassemble_native(elf.path,'func_02018aa4')
    calls=lambda ds:[line.strip() for line in ds.splitlines() if re.search(r'\bbl\s',line)]
    assert len([l for l in calls(hd) if '<func_0205cd34>' in l])==1
    assert len([l for l in calls(hd) if '<func_0205cc80>' in l])==1
    assert len([l for l in calls(bd) if '<sm64ds_native_register_rom_archive>' in l])==1
    assert re.search(r'\bmov\s+r2, #3\b',hd)
    ready=elf.names['_ZN12_GLOBAL__N_1L18rom_registry_readyE']
    assert ready['bytes']==1 and elf.read(ready['address'],1)==b'\0'
    prior=ROOT/'build/native-stage-plan/fs-archive-anchor-static-contract.json'
    old=json.loads(prior.read_text())
    unchanged=[]
    for path in ['upstream/src/func_0205cbe4.c','upstream/src/func_0205cc80.c','upstream/src/func_0205cd34.c','upstream/src/func_0205d96c.c']:
        current=proof.record(ROOT/path)
        assert current==next(row for row in old['sources'] if row['path']==path)
        unchanged.append(current)
    out=dict(status='passed_bounded_static_review',tool=proof.record(Path(__file__).resolve()),elf=proof.record(elf.path),
        sources=[proof.record(card),proof.record(runtime)],previous_original_contract=proof.record(prior),
        original_sources_unchanged=unchanged,unique_strong_symbol_ownership=ownership,
        helper_native_code_sha256=proof.sha(elf.read(elf.names['sm64ds_native_register_rom_archive']['address'],elf.names['sm64ds_native_register_rom_archive']['bytes'])),
        bootstrap_native_code_sha256=proof.sha(elf.read(elf.names['func_02018aa4']['address'],elf.names['func_02018aa4']['bytes'])),
        helper_selected_calls=calls(hd),bootstrap_calls=calls(bd),
        original_contract='Original5d96c clears staticROM withcd34 and registers its actualname pointer+length3 throughcc80 before FAT/FNT validity earlyreturns. Candidate restores that identity/link-order step; it does not invoke original DS card initialization.',
        checks=dict(catalog_before_registration_before_publication=True,original_name_alias_with_low24bit_ROM_key_validated=True,
            first_registration_rejects_nonempty_head=True,original_cd34_then_cc80_GP_arguments_retained=True,
            ready_flag_published_only_after_cc80_success=True,initial_ready_flag_is_zero_BSS=True,
            idempotent_checks_head_ROM_key_prev0=True,idempotent_does_not_clear_or_walk_NARC_chain=True,
            no_FS_loaded_flags_or_DMA_callbacks_supplied=True,unregister_original_source_unchanged=True),
        ABI='externC helper returnsint. cd34 receivesrecord inr0; cc80 receivesROMrecord/name/length3 inr0/r1/r2, matching original ARM32 call. Bootstrap branches to noreturnmissing on failure before publishing globals.',
        findings=[],limits=['Static source/linked-instruction review only: no proof of successful runtime registration, complete StageInit or a rendered frame.',
            'No old RAM is read or installed onto the changed ELF. No CPU replay, tests, Azahar, UI or production edits.'])
    dest=ROOT/'build/native-stage-plan/rom-archive-registration-review.json'
    dest.write_text(json.dumps(out,indent=2)+'\n')
    print(json.dumps(dict(status=out['status'],report=str(dest),ELF_SHA256=SHA,strong_definitions_checked=4,findings=0,CPU_replays=0)))


if __name__=='__main__':main()
