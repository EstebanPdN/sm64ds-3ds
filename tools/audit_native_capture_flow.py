#!/usr/bin/env python3
"""Audit DSMT's capture trigger using pinned metadata and bounded ROM leaves.

This uses explicitly constructed state/register fixtures and original ARM CPU
bodies. Named rendering/resource/sound operations outside the control proof
are intercepted and recorded, never claimed to run. No image is supplied or
rendered, and no capture pixels, complete menu, timing or console are verified.
"""
from collections import deque
import bisect
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
import zlib

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
    UC_ARM_REG_R3, UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
    UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11,
    UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_CPSR)

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/native-capture-flow'
OUT.mkdir(exist_ok=True)
sys.path.insert(0, str(ROOT / 'build/game-data/python'))
import ndspy.rom
import ndspy.codeCompression
import ndspy.code

raw = (ROOT / 'build/roms/europe.nds').read_bytes()
assert zlib.crc32(raw) == 0x29715dec, 'Requires verified ASMP0 ROM'
rom = ndspy.rom.NintendoDSRom(raw)
arm9 = ndspy.codeCompression.decompress(rom.arm9)
overlay = ndspy.code.loadOverlayTable(rom.arm9OverlayTable, lambda i, file_id: rom.files[file_id])[7]
blobs = {'arm9': (rom.arm9RamAddress, arm9), 'ov007': (overlay.ramAddress, overlay.data)}
metadata, addresses, functions, edges = {}, {}, {}, {}
for module in blobs:
    folder = ROOT / 'upstream/config/arm9'
    if module != 'arm9':
        folder /= 'overlays/' + module
    for line in (folder / 'symbols.txt').read_text().splitlines():
        m = re.match(r'(\S+) kind:(\S+) addr:0x([0-9a-f]+)', line)
        if not m:
            continue
        name, kind, address = m[1], m[2], int(m[3], 16)
        metadata[name] = (module, address, kind)
        f = re.match(r'function\((?:arm|thumb),size=0x([0-9a-f]+)\)', kind)
        if f:
            functions[name] = (module, address, int(f[1], 16))
            addresses[(module, address)] = name
    spans = sorted((address, address + size, name)
                   for name, (m, address, size) in functions.items() if m == module)
    for line in (folder / 'relocs.txt').read_text().splitlines():
        m = re.match(r'from:0x([0-9a-f]+) kind:((?:arm|thumb)_call|arm_branch) '
                     r'to:0x([0-9a-f]+) module:(\S+)', line)
        if not m:
            continue
        source, target = int(m[1], 16), int(m[3], 16)
        dest = m[4]
        ov = re.fullmatch(r'overlay\((\d+)\)', dest)
        dest = 'arm9' if dest == 'main' else 'ov' + ov[1].zfill(3) if ov else dest
        i = bisect.bisect_right(spans, (source, 1 << 64, '~')) - 1
        if i >= 0 and source < spans[i][1]:
            edges.setdefault(spans[i][2], []).append((dest, target, source))

real = (
    'func_ov007_020b6544', 'func_ov007_020b1cf0', 'func_ov007_020b1718',
    'func_ov007_020aed98', 'func_ov007_020b0358', 'func_ov007_020b6eb4',
    'func_ov007_020bd4e8', 'func_ov007_020bd648', 'func_ov007_020b2764',
    'func_ov007_020bf690', 'func_ov007_020bf790', 'RequestScreenFade',
    'func_ov007_020c8498', 'func_ov007_020c8688', 'func_ov007_020c86a8',
    'func_ov007_020c86c4', 'func_ov007_020c8970', 'func_ov007_020c897c',
    'func_ov007_020c8b34', 'func_ov007_020c934c', 'func_ov007_020c92d0',
    'func_ov007_020c1d8c', 'func_ov007_020c1da0', 'func_ov007_020c3be0',
    'func_020541a4', 'func_020541b8', 'func_02054430', 'Vram__Map',
    '_ZN2GX12SetBankForBGEt', '_ZN2GX15SetGraphicsModeEiii',
    '_ZN3G2x18SetBlendBrightnessEPVtts', '_ZN3G2x13SetBlendAlphaEPVttttj',
    'func_020551f0', 'func_02053a90',
    'func_ov007_020b8f78',
)
intercepts = (
    'func_ov007_020b20e8', 'func_ov007_020b4b5c',
    'func_ov007_020b2614', 'func_ov007_020bdeb0', 'func_ov007_020bce70',
    'func_ov007_020b72a0', 'ARMMathSaveState', 'ARMMathLoadState',
    'func_ov007_020b46b0', 'func_ov007_020b7a78', 'func_ov007_020c2090',
    'func_ov007_020b44ec', 'func_ov007_020add3c', 'func_ov007_020ad660',
    'func_ov007_020bca40',
)
cpu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
pages = set()


def map_memory(address, size, permissions):
    for page in range(address & ~4095, (address + size + 4095) & ~4095, 4096):
        if page not in pages:
            cpu.mem_map(page, 4096, permissions)
            pages.add(page)


hashes = {}
for name in real:
    module, address, size = functions[name]
    base, data = blobs[module]
    code = bytes(data[address - base:address - base + size])
    map_memory(address, size, 5)
    cpu.mem_write(address, code)
    hashes[name] = hashlib.sha256(code).hexdigest()
for name in intercepts:
    _, address, _ = functions[name]
    map_memory(address, 4, 5)

# Only necessary original scalar/table storage, plus the explicitly constructed
# native-sized state fixture. Register backing here records CPU writes only;
# it is not a DS video device and never produces a capture image.
map_memory(0x02103000, 0x2000, 3)
map_memory(0x02099000, 0x1000, 3)
map_memory(0x020a6000, 0x1000, 3)
for symbol, size in (('data_ov007_020ccc50', 12), ('data_ov007_020ccc5c', 12)):
    module, address, _ = metadata[symbol]
    base, data = blobs[module]
    map_memory(address, size, 3)
    cpu.mem_write(address, bytes(data[address - base:address - base + size]))
BUFFER, STACK, STOP = 0x500000, 0x600000, 0x700000
map_memory(BUFFER, 0x10000, 3)
map_memory(STACK, 4096, 3)
map_memory(STOP, 4096, 5)
map_memory(0x04000000, 0x2000, 3)
saved = (UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
    UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11)
register_args = (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)
intercept_by_address = {functions[name][1]: name for name in intercepts}
events, writes = [], []


def code_hook(uc, address, size, user):
    name = intercept_by_address.get(address)
    if name:
        events.append({'operation': 'intercepted:' + name,
                       'args': [uc.reg_read(reg) for reg in register_args]})
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))


cpu.hook_add(UC_HOOK_CODE, code_hook)
cpu.hook_add(UC_HOOK_MEM_WRITE,
    lambda _, access, address, size, value, user: writes.append((address, size, value)))


def put(address, value, size=4):
    cpu.mem_write(address, int(value & ((1 << (size * 8)) - 1)).to_bytes(size, 'little'))


def get(address, size=4):
    return int.from_bytes(cpu.mem_read(address, size), 'little')


def symbol(name):
    return metadata[name][1]


def invoke(name, *values):
    cpu.reg_write(UC_ARM_REG_CPSR, 0x10)
    for i, reg in enumerate(register_args):
        cpu.reg_write(reg, values[i] if i < len(values) else 0)
    cpu.reg_write(UC_ARM_REG_SP, STACK + 0xf00)
    cpu.reg_write(UC_ARM_REG_LR, STOP)
    for i, reg in enumerate(saved):
        cpu.reg_write(reg, 0x12345000 + i)
    cpu.emu_start(functions[name][1], STOP, count=100000)
    assert cpu.reg_read(UC_ARM_REG_PC) == STOP, name
    assert cpu.reg_read(UC_ARM_REG_SP) == STACK + 0xf00, name
    assert all(cpu.reg_read(reg) == 0x12345000 + i for i, reg in enumerate(saved)), name


G, MAIN, SUB, F, ANIM, CAMERA, POS, SCALE, LIGHT = (BUFFER + i * 0x400 for i in range(9))
TOUCH, PAD, ELEM, ELEM_OBJECT, TRANSFORM0, TRANSFORM1, FADER, FADER_OBJECT, FADER_OVERLAY = (
    BUFFER + i * 0x400 for i in range(9, 18))
STATE3, OPTION_STATE = (BUFFER + i * 0x400 for i in range(18, 20))


def fixture():
    cpu.mem_write(BUFFER, bytes(0x10000))
    cpu.mem_write(0x02103000, bytes(0x2000))
    cpu.mem_write(0x02099000, bytes(0x1000))
    cpu.mem_write(0x020a6000, bytes(0x1000))
    cpu.mem_write(0x04000000, bytes(0x2000))
    put(symbol('data_ov007_0210342c'), G)
    put(symbol('data_ov007_02104bd8'), FADER)
    for offset, pointer in ((4, MAIN), (8, SUB), (0xc, STATE3), (0x30, CAMERA), (0x3c, LIGHT),
                            (0x40, SCALE), (0x50, TOUCH), (0x54, PAD),
                            (0xf4, F), (0x108, TRANSFORM0), (0x10c, TRANSFORM1), (0x130, ELEM)):
        put(G + offset, pointer)
    put(F + 0x18, ANIM)
    put(symbol('data_ov007_02104ba0'), OPTION_STATE)
    put(OPTION_STATE + 8, ANIM)
    put(ELEM, ELEM_OBJECT)
    put(ELEM_OBJECT + 0x10, 4096)
    put(CAMERA + 0x3c, POS)
    put(CAMERA + 0x40, SCALE)
    put(FADER, FADER_OBJECT)
    put(FADER + 4, FADER_OVERLAY)
    put(symbol('data_02099fcc'), 1, 2)
    put(0x04000000, 0x10008)
    put(0x04001000, 0x10000)
    invoke('func_02053a90', 0x0400006c, 0)
    invoke('func_02053a90', 0x0400106c, 0)
    invoke('_ZN3G2x18SetBlendBrightnessEPVtts', 0x04000050, 0x3f, 0xfffffff0)
    invoke('_ZN3G2x18SetBlendBrightnessEPVtts', 0x04001050, 0x3f, 0xfffffff0)
    invoke('func_ov007_020bd648', 1)


def graph_sync():
    put(symbol('data_ov007_02103430'), 1)
    invoke('func_ov007_020b6eb4')


def fade(request, flags, frames):
    invoke('RequestScreenFade', request, flags, 0)
    for _ in range(frames):
        invoke('func_ov007_020bf690')


# Initialization explicitly clears the capture request. The intercepted camera
# and cell-initializer calls are outside this control-only test's scope.
fixture()
invoke('func_ov007_020b6544')
assert get(G + 0x18) == 0 and get(G + 0x1c) == 1
graph_sync()
assert not any(a == 0x04000064 and v == 0x80370010 for a, n, v in writes)

cases = []
for entry in ('fresh_touch', 'fresh_B_bit_2'):
    fixture()
    # The original intro requests 4/6, then 7/0x18, then 3/0xa. These are
    # completed fader fixtures, not a claim that the intro animation ran here.
    fade(4, 6, 20)
    fade(7, 0x18, 3)
    fade(3, 0xa, 80)
    assert get(0x04000050, 2) == 0xbf and get(0x04000054, 2) == 0
    put(MAIN, 0, 2)
    put(MAIN + 8, 0, 2)
    put(SUB, 0, 2)
    put(SUB + 0xc, 39)  # avoid the unrelated periodic modulo/title nudge
    if entry == 'fresh_touch':
        put(TOUCH + 0xc, 1, 2)
        put(TOUCH + 0x24, 1)
    else:
        put(PAD, 2, 2)
    invoke('func_ov007_020b1cf0')
    assert get(SUB + 2, 2) == 4
    mode = get(G + 0x20)
    assert mode == (1 if entry == 'fresh_touch' else 0)
    # Construct only the transition's exact completion condition. Do not
    # advance a fabricated animation through the earlier scene frames.
    put(SUB, 4, 2)
    put(SUB + 0xc, 30 if mode == 1 else 40)
    put(ANIM + 0xc, 0, 2)
    put(ANIM + 0x10, 28, 2)
    put(ANIM + 0x12, 30, 2)
    put(SCALE + 0xc, 15)
    invoke('func_ov007_020b1718')
    assert get(MAIN + 2, 2) == 1 and get(G + 0x1c) == 2
    graph_sync()
    assert get(0x04000243, 1) == 0x80  # D: LCDC
    assert get(0x04000244, 1) == 0x81  # E: main BG
    assert get(0x04000000) == 0x11b08 and get(0x04001000) == 0x11c00
    assert get(G + 0x18) == 0

    invoke('func_ov007_020aed98')
    assert get(MAIN, 2) == 1 and get(SUB, 2) == 0 and get(SUB + 0xc) == 0
    assert get(G + 0x18) == 1 and get(G + 0x1c) == 0
    writes.clear()
    graph_sync()
    capture_writes = [(a, n, v) for a, n, v in writes if a == 0x04000064]
    assert capture_writes == [(0x04000064, 4, 0x80370010)]
    assert get(G + 0x18) == 0
    captured = {'entry': entry, 'transition_mode': mode,
        'DISPCAPCNT': hex(get(0x04000064)), 'DISPCNT_A': hex(get(0x04000000)),
        'DISPCNT_B': hex(get(0x04001000)), 'VRAMCNT_D': hex(get(0x04000243, 1)),
        'VRAMCNT_E': hex(get(0x04000244, 1)), 'MASTER_BRIGHT_A': get(0x0400006c, 2),
        'MASTER_BRIGHT_B': get(0x0400106c, 2), 'BLDCNT_A': hex(get(0x04000050, 2)),
        'BLDY_A': get(0x04000054, 2), 'capture_request_cleared': True}
    # CPU fixtures do not emulate line-192 capture hardware or generate VRAM
    # pixels. Only set the auto-clear state needed for the next control step.
    put(0x04000064, 0x00370010)
    put(SUB + 0xc, 1)
    invoke('func_ov007_020b0358')
    assert get(G + 0x1c) == 3 and get(SUB + 2, 2) == 1
    graph_sync()
    assert get(0x04000243, 1) == 0x81 and get(0x04000244, 1) == 0x80
    assert get(0x04000000) == 0x1080b and get(0x0400000e, 2) == 0x4285
    captured['next_graphics_mode'] = 3
    captured['next_VRAMCNT_D'] = '0x81'
    captured['next_BG3CNT'] = '0x4285'
    cases.append(captured)

# The flag writer is gated on substate 0 and its first frame, independent of
# the next menu's choice or eventual StartFile result.
guards = []
for state, count in ((0, 1), (0, 2), (1, 0), (1, 1), (2, 2), (7, 0)):
    fixture()
    put(SUB, state, 2)
    put(SUB + 0xc, count)
    if state == 1 and count == 0:
        # Its resource reload is deliberately outside the bounded test.
        continue
    invoke('func_ov007_020b0358')
    assert get(G + 0x18) == 0
    guards.append({'substate': state, 'frame': count, 'capture_flag': 0})

manifest = json.loads((ROOT / 'build/native-runtime/manifest.json').read_text())
new_leaves = {'func_02052ec8', 'func_02052550', 'func_0205256c', 'func_020527e8'}
missing = set(manifest['unavailable_functions']) - new_leaves
closure = {}
for root in ('_ZN9dScDSMT_c13InitResourcesEv', 'func_ov007_020b0548', 'func_ov007_020b0358'):
    queue, visited, found = deque([(root, [root])]), set(), []
    while queue:
        name, path = queue.popleft()
        if name in visited:
            continue
        visited.add(name)
        if name in missing:
            found.append(path)
            continue
        for dest, target, _ in edges.get(name, ()):
            next_name = addresses.get((dest, target))
            if next_name:
                queue.append((next_name, path + [next_name]))
    closure[root] = {'direct_ROM_function_closure': len(visited), 'STOP_paths': found}

report = {'status': 'passed',
    'scope': 'Original CPU control/register leaves and explicit fixtures; intercepted resource/render operations did not run; no pixels, full scene, gameplay, Azahar, console or FPS proof',
    'upstream_pin': '6230b2996636e3fd0e03cb178161c94cb4b73cc9',
    'rom_sha256': hashlib.sha256(raw).hexdigest(), 'original_function_hashes': hashes,
    'intercepted_operations': list(intercepts), 'capture_flow_cases': cases,
    'non_capture_guard_cases': guards,
    'static_closure_limits': 'Direct calls/branches only; follows all branch arms, excludes indirect calls and modules other than ARM9/OV007. The three DS STOP paths under InitResources already have native LoadOverlay/UnloadArchives replacements.',
    'static_closure': closure}
(OUT / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps({k: v for k, v in report.items() if k not in ('original_function_hashes', 'intercepted_operations')}, indent=2))
