"""Fit the integrated game in the Old 3DS 80 MB application mode.

The 96 MB mode leaves the SYSTEM region 12 MB; a loaded HOME Menu needs about
20.5 MB there (3dbrew Memory layout), so it crashed on launch. 80 MB leaves
28 MB. Savings, without changing game behaviour:
  - DS VRAM: map only the windows reachable from DS register fields instead
    of one flat 9 MiB block (falls back to the flat block if mapping fails).
  - Linear heap: 20 MiB (castle peak use 11.1 MiB; GPU/DSP buffers only).
"""
from prepare_integrated_native_pause import GeneratedTree
from prepare_integrated_profiling import once


VRAM_WINDOWS = '''
// SM64DS port memory: DS VRAM windows instead of a flat 9 MiB block. Every
// PPU/GX/LCDC address derives from bounded DS register fields:
//   engine A BG  DISPCNT bases + BGxCNT bases + 64 KiB tiles, bitmap mask 7FFFF
//   engine B BG  BGxCNT bases + 64 KiB tiles, bitmap base + mask 1FFFF
//   OBJ A/B      masked 3FFFF/1FFFF
//   LCDC         banks A-I (0x06800000-0x068A3FFF)
// Returns false (nothing left mapped) so the caller can use the flat block.
bool map_vram_windows() {
#if defined(__3DS__)
    static const struct { u32 base, size; } windows[] = {
        {0x06000000u, 0x100000u}, {0x06200000u, 0xA0000u}, {0x06400000u, 0x40000u},
        {0x06600000u, 0x20000u}, {0x06800000u, 0xA4000u},
    };
    constexpr unsigned count = sizeof(windows) / sizeof(windows[0]);
    void *heap[count] = {};
    unsigned mapped = 0;
    bool via_process = false;
    Handle self = 0;
    const MemPerm rw = static_cast<MemPerm>(MEMPERM_READ | MEMPERM_WRITE);
    for (; mapped < count; ++mapped) {
        const auto &w = windows[mapped];
        heap[mapped] = std::aligned_alloc(0x1000, w.size);
        if (!heap[mapped]) break;
        u32 out = 0;
        Result r = via_process ? -1 : svcControlMemory(&out, w.base, reinterpret_cast<u32>(heap[mapped]), w.size, MEMOP_MAP, rw);
        if (R_FAILED(r)) {
            if (!self && R_FAILED(svcDuplicateHandle(&self, CUR_PROCESS_HANDLE))) { self = 0; break; }
            if (R_FAILED(svcControlProcessMemory(self, w.base, reinterpret_cast<u32>(heap[mapped]), w.size, MEMOP_MAP, rw))) break;
            via_process = true;
        }
    }
    if (mapped < count) {
        for (unsigned i = 0; i < mapped; ++i) {
            u32 out = 0;
            if (via_process) svcControlProcessMemory(self, windows[i].base, reinterpret_cast<u32>(heap[i]), windows[i].size, MEMOP_UNMAP, MEMPERM_DONTCARE);
            else svcControlMemory(&out, windows[i].base, reinterpret_cast<u32>(heap[i]), windows[i].size, MEMOP_UNMAP, MEMPERM_DONTCARE);
        }
        for (unsigned i = 0; i <= mapped && i < count; ++i) std::free(heap[i]);
        if (self) svcCloseHandle(self);
        std::fprintf(stderr, "ntr io: VRAM windows unavailable, using the flat block\\n");
        return false;
    }
    if (self) svcCloseHandle(self);
    unsigned total = 0;
    for (const auto &w : windows) { std::memset(reinterpret_cast<void *>(w.base), 0, w.size); total += w.size; }
    g_region[VRAM_BASE >> 24] = reinterpret_cast<uint8_t *>(VRAM_BASE);
    g_region_identity[VRAM_BASE >> 24] = true;
    std::fprintf(stderr, "ntr io: VRAM windows mapped bytes=%u (flat block %u)\\n", total, unsigned(VRAM_SIZE));
    return true;
#else
    return false;
#endif
}
'''


def prepare(output):
    tree = GeneratedTree(output)
    name = 'port/ntr/io.cpp'
    text = tree.read_text(name)
    text = once(text, '    if (p) std::memset(p, 0, size);\n    return p;\n}\n\n}  // namespace\n',
                '    if (p) std::memset(p, 0, size);\n    return p;\n}\n' + VRAM_WINDOWS + '\n}  // namespace\n')
    text = once(text, '        {VRAM_BASE, VRAM_SIZE}, {OAM_BASE, OAM_SIZE},', '        {OAM_BASE, OAM_SIZE},')
    text = once(text, '    g_io = static_cast<uint8_t *>(io);\n',
                '    if (!map_vram_windows() && !map_fixed(VRAM_BASE, VRAM_SIZE)) return false;\n'
                '    g_io = static_cast<uint8_t *>(io);\n')
    tree.write_text(name, text)
    name = 'port/platform/3ds/game_entry.cpp'
    text = tree.read_text(name)
    text = once(text, 'u32 __ctru_linear_heap_size=24*1024*1024;',
                '// Old 3DS 80 MB mode (HOME Menu needs the SYSTEM region): castle peak\n'
                '// linear use is 11.1 MiB, so 20 MiB keeps margin for heavier courses.\n'
                'u32 __ctru_linear_heap_size=20*1024*1024;')
    tree.write_text(name, text)
    tree.commit()
    return ['port/ntr/io.cpp', 'port/platform/3ds/game_entry.cpp']
