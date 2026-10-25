#!/usr/bin/env python3
"""Build an auditable ARM11 source corpus, without pretending it is a game.

Only generated copies are changed. A nonzero result means compilation or symbol
ownership remains unresolved. No archive with ambiguous strong definitions is
published. Run native_core_inventory.py afterwards for the playable dependency
closure and unresolved platform/data interfaces.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time

import native_vtable
import native_local_externs

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT / 'upstream'
OUT = ROOT / 'build/native-core'
PIN = '6230b2996636e3fd0e03cb178161c94cb4b73cc9'

# These routines access caller-provided memory and normal ARM registers; the
# soft-float conversion also uses the original software FP status in owned BSS.
# DS startup, privileged CP15/IRQ/context-switch primitives deliberately remain
# unresolved: assembling them for ARM11 would not make them safe in user mode.
PURE_ASM = {
    'Copy32Bytes', 'Copy36Bytes', 'Copy48Bytes', 'Copy48BytesFixed',
    'Matrix3x3_LoadIdentity', 'Matrix4x3_LoadIdentity', 'MultiCopy32Bytes',
    'MultiCopyHalf', 'MultiCopy_Int', 'MultiStore32Bytes', 'MultiStore_Int',
    'func_02052514',
    # Soft-float int32->binary32 returns its bits in r0. Preserve the original
    # GP-register ABI, rounding modes and real FP status word; a hard-float C
    # return would silently use s0 and break the original atan2 caller.
    'func_01ffa4bc',
}

# These original bodies exist in the pinned source but are outside the matching
# delinks enrollment. They are needed by the native Model/menu frontier. Their
# inclusion is explicit and is not reported as a byte-matching result.
ADDITIONAL_RECOVERED_SOURCES = {
    'src/func_02052514.c', # pure 3x3-to-4x4 matrix expansion, original assembly
    'src/_ZN11ShadowModel12InitCylinderEv.cpp',
    # The paired cuboid initializer also lives outside delinks. Stage actors
    # use the original overlay-1 cuboid template through ModelBase::SetFile.
    'src/_ZN11ShadowModel10InitCuboidEv.cpp',
    'src/_ZN5Model27LoadCompressedTextureToVramEPcjS0_.cpp',
    'src/func_ov007_020bfd70.cpp',
    # The complete recovered Camera update was explicitly NONMATCHING and is
    # outside delinks. A native port still needs its original collision/mode
    # behavior; enrolling the body does not assert byte matching or gameplay.
    'src/func_02009e70.cpp',
    # Third callback in the original VS message parser table. Its recovered
    # source lies outside delinks and retains an incorrect callee spelling;
    # adapt the two calls below from the actual ARM9 BL targets.
    'src/func_020313d8.c',
    # Upstream's explicitly non-byte-matched walk-physics adoption. Preserve
    # that provenance; enrolling it here is not a matching or gameplay claim.
    'port/unmatched/func_ov002_020d3b9c.c',
}


def replace_once(text, before, after):
    if text.count(before) != 1:
        raise ValueError(f'Expected exactly one adaptation anchor: {before[:70]}')
    return text.replace(before, after, 1)


def adapt(name, text):
    changes = []
    if name == 'src/actors/da1up_c.cpp':
        text, views = native_local_externs.adapt_da1up(text)
        changes.append('preserve-MWCC-local-C-function-views-via-assembler-aliases:' + str(len(views)))
    if name == 'src/actors/daMip_c.cpp':
        text, views = native_local_externs.adapt_mip_data(text)
        changes.append('preserve-MWCC-local-data-views-via-typed-assembler-addresses:' + str(len(views)))
    text, vptr_edits = native_vtable.adapt(name, text)
    if vptr_edits:
        changes.append('reviewed-manual-vptr-address-point-8-bytes:' + str(len(vptr_edits)))
    if name == 'src/func_020313d8.c':
        # ROM BL at 02031400/02031410 targets 02031b84 twice. AddChar belongs
        # to a different message buffer; retain the original two GP arguments.
        if text.count('_ZN7Message7AddCharEc') != 3:
            raise ValueError('Original VS character callback sites changed')
        text = text.replace('_ZN7Message7AddCharEc', 'func_02031b84')
        changes.append('restore-original-VS-character-callee-02031b84')
    if name == 'src/d_a_obj_fl_koma_u.cpp':
        # This recovered declaration represents callback addresses as void*.
        # Make the ARM32 function-address conversion explicit without changing
        # the array constructor, its callback words, count or element stride.
        text = replace_once(text,
            '__cxa_vec_ctor((char*)p + 0x360, 8, 0x3c, _ZN8dCcPos_cC1Ev, _ZN8dCcPos_cD1Ev);',
            '__cxa_vec_ctor((char*)p + 0x360, 8, 0x3c, (void*)_ZN8dCcPos_cC1Ev, (void*)_ZN8dCcPos_cD1Ev);')
        changes.append('explicit-original-ARM32-array-callback-addresses')
    if name == 'src/minigames/d_s_mg_roulette.cpp':
        text = replace_once(text,
            '__cxa_vec_ctor(c + 0x51a8, 5, 0x34, func_ov006_0210a4ac, func_ov006_021079c8);',
            '__cxa_vec_ctor(c + 0x51a8, 5, 0x34, (void*)func_ov006_0210a4ac, func_ov006_021079c8);')
        changes.append('explicit-original-ARM32-array-callback-address')
    if name == 'src/_ZN16dScMgSmartball_c21AfterCleanupResourcesEj.cpp':
        for offset in ('0x4c', '0x34'):
            text = replace_once(text,
                f'__cxa_vec_cleanup((char *)p + {offset}, 3, 8, NullDestructor_0203d47c);',
                f'__cxa_vec_cleanup((char *)p + {offset}, 3, 8, (void*)NullDestructor_0203d47c);')
        changes.append('explicit-original-ARM32-array-cleanup-callback-addresses')
    if name == 'src/_ZN15ModelComponents6RenderEP9Matrix4x3P7Vector3.cpp':
        # The old code generator kept the loop's material index live in r1.
        # The callee consumes it; make that existing ABI argument explicit.
        text=replace_once(text,'extern void func_02044b30(ModelComponents* self);',
                         'extern void func_02044b30(ModelComponents* self, int idx);')
        if text.count('func_02044b30(this);')!=2:
            raise ValueError('Model material dispatch shape changed')
        text=text.replace('func_02044b30(this);','func_02044b30(this, idx);')
        changes.append('preserve-original-r1-material-index-in-render-dispatch')
    if name in ('src/func_02044b30.c','src/func_0204488c.c','src/func_02044534.c'):
        # Only explicit writes to GX command ports are changed. Do not turn
        # arbitrary DS memory accesses into a mapped emulator window.
        text='extern void sm64ds_draw_port(unsigned, unsigned);\n'+text
        patterns={
            'src/func_02044b30.c':r'\*\(\(int \*\) (0x4000[0-9a-f]+)\)\s*=\s*([^;]+);',
            'src/func_0204488c.c':r'\*\(volatile int\*\)(0x4000[0-9a-f]+)\s*=\s*([^;]+);',
            'src/func_02044534.c':r'\*\(int\*\)(0x4000[0-9a-f]+)\s*=\s*([^;]+);',
        }
        ports=[]
        def port_write(match):
            address=int(match[1],16)
            if not 0x4000440<=address<=0x40005c8 or address&3:
                raise ValueError('Unexpected hardware write')
            port=(address-0x4000400)//4;ports.append(port)
            return f'sm64ds_draw_port({port:#x}, (unsigned)({match[2]}));'
        text=re.sub(patterns[name],port_write,text)
        expected=5 if name.endswith('0204488c.c') else 7
        if len(ports)!=expected: raise ValueError(f'Unexpected GX write count in {name}: {len(ports)}')
        if name.endswith('0204488c.c'):
            text=replace_once(text,'    volatile int* mtxScale;','')
            text=replace_once(text,'    mtxScale = (volatile int*)0x400046c;','')
            text,scales=re.subn(r'\*mtxScale = ([^;]+);',r'sm64ds_draw_port(0x1b, (unsigned)(\1));',text)
            if scales!=6: raise ValueError('Unexpected GX scale write count')
        changes.append('route-original-GX-port-writes-to-native-command-buffer')
        if name.endswith('02044534.c'):
            # This sole async FIFO call has callback=arg=0 and immediately
            # waits for DMA3. Submit the same bytes synchronously through the
            # native FIFO provider used by sibling 0204488c. Preserve the wait
            # provider's completed-transfer check; do not enter the DS GXSTAT
            # queue/IRQ transport or pretend its raw registers are mapped.
            text=replace_once(text,
                'extern void func_0205a358(int a0, int a1, int a2, int a3, int a4);',
                'extern void func_01ffde98(unsigned channel, const void* source, unsigned bytes);')
            text=replace_once(text,
                'func_0205a358(three, part->fc, part->f8, a4, a5);',
                '(void)a4; (void)a5; func_01ffde98((unsigned)three, (const void*)part->fc, (unsigned)part->f8);')
            changes.append('transport-awaited-original-billboard-display-list-through-native-FIFO')
    if name == 'src/func_ov002_020e6ad4.c':
        # DISP3DCNT bit 1 selects highlight vs toon shading. Bits 12/13 are
        # write-one-to-clear error flags, so the original AND does not clear
        # them: only the shading selection changes. Keep the original ramp.
        text='extern void sm64ds_geometry_set_highlight(unsigned);\n'+text
        text=replace_once(text,'*(volatile unsigned short *)0x4000060 &= ~0x3002;',
                          'sm64ds_geometry_set_highlight(0);')
        changes.append('route-original-toon-selection-to-native-geometry-state')
    if name == 'src/__sinit_ov002_021019d0.c':
        # The callee's definition takes one pointer and returns that pointer.
        # Its missing prototype is rejected by modern GCC; it is not a reason
        # to discard the Player's original file/state initializer.
        text = 'extern unsigned short *func_ov002_020e6ad4(void *);\n' + text
        changes.append('declare-original-player-palette-initializer-pointer-ABI')
    if name == 'src/_ZN7dBgW_Kc10DetectClsnER12dBgCh_SphCrr.cpp':
        start = text.index('#define SQRT_RESULT ')
        end = text.index('/* The scale-preserving product', start)
        text = text[:start] + '''// ARM11 native replacement for the DS's raw integer square-root unit.
// This is floor(sqrt(x)), NOT the rounded/scaled cstd::sqrt primitive.
extern "C" unsigned sm64ds_collision_sqrt(unsigned long long);
static s32 native_sqrt_result;
#define SQRT_RESULT native_sqrt_result
static inline void SqrtStart(u64 x, s32, s32) {
    native_sqrt_result = (s32)sm64ds_collision_sqrt(x);
}
static inline s32 SqrtRaw(u64 x, s32, s32) {
    return (s32)sm64ds_collision_sqrt(x);
}

''' + text[end:]
        changes.append('replace-DS-IRQ-and-raw-sqrt-registers-with-exact-integer-sqrt')
    if Path(name).stem in PURE_ASM:
        match = re.search(r'\basm\s+(?:void|float)\s+(\w+)\([^)]*\)\s*\{(.*)\}\s*$', text, re.S)
        if not match:
            raise ValueError(f'ASM shape changed: {name}')
        symbol, body = match.groups()
        # GAS places the condition after the addressing/width suffix. Opcode,
        # operand order, flags and the loop termination remain identical.
        body = body.replace('ldrlth', 'ldrh'+'lt').replace('strlth', 'strh'+'lt')
        body = body.replace('ldmltia', 'ldmia'+'lt').replace('stmltia', 'stmia'+'lt')
        adaptation = 'mwcc-to-gas-soft-float-r0-bits' if symbol == 'func_01ffa4bc' else 'mwcc-to-gas-memory-primitive'
        return ('.syntax unified\n.arm\n.text\n.align 2\n.global ' + symbol +
                '\n.type ' + symbol + ', %function\n' + symbol + ':\n' + body +
                '\n.size ' + symbol + ', .-' + symbol + '\n'), [adaptation], 'assembler'
    if name == 'src/_ZN6Player13InitResourcesEv.cpp':
        before = '''    asm {
      mov r1, #5
      mov r0, #0
    Lzero:
      strb r0, [q], #1
      strb r0, [q], #1
      strb r0, [q], #1
      strb r0, [q], #1
      subs r1, r1, #1
      bne Lzero
    }'''
        text = replace_once(text, before, '    for (unsigned int clear = 0; clear < 20; ++clear) *q++ = 0;')
        changes.append('player-held-object-queue-clear-20-bytes')
    if name == 'src/_ZN6Player17St_HoldLight_MainEv.cpp':
        # The skipped initialization has no side effect and the value is not
        # observed at the end label. Declaration is hoisted; assignment stays.
        text = replace_once(text, '    int var_r4 = 0;', '    var_r4 = 0;')
        at = text.index('{', text.index('Player::St_HoldLight_Main(')) + 1
        text = text[:at] + '\n    int var_r4;' + text[at:]
        changes.append('declaration-hoist-over-goto-no-value-change')
    if name in {'src/_ZN5Model17LoadTextureToVramEPcj.cpp',
                'src/game/actors/daTrs_c/_ZN7daTrs_c13InitResourcesEv.cpp',
                'src/_ZN11ShadowModel12InitCylinderEv.cpp',
                'src/_ZN11ShadowModel10InitCuboidEv.cpp',
                'src/_ZN5Model27LoadCompressedTextureToVramEPcjS0_.cpp'}:
        expected = 4 if name == 'src/_ZN5Model27LoadCompressedTextureToVramEPcjS0_.cpp' else 1
        if text.count('extern "C" extern ') != expected:
            raise ValueError(f'Redundant linkage shape changed: {name}')
        text = text.replace('extern "C" extern ', 'extern "C" ')
        changes.append('redundant-storage-class-in-linkage-declaration')
    if name == 'src/_ZN7fBase_c21AfterCleanupResourcesEj.cpp':
        text = replace_once(text, '''#ifdef SM64DS_PLATFORM_PC
extern char data_020a4b6c;
#else
char data_020a4b6c;
#endif
char data_020a4ba8;''', '''extern char data_020a4b6c;
extern char data_020a4ba8;''')
        changes.append('remove-one-byte-storage-for-12-and-16-byte-BSS-manager-nodes')
    if name == 'src/_ZN7Vector3D1Ev.cpp':
        # The original destructor is exactly empty. MWCC's artificial array
        # cleanup forces its address to exist, but GCC optimizes even that away.
        # Export the same empty complete destructor under its required ABI name.
        # This is the body documented by the original source, not a substitute
        # for nonempty gameplay behavior.
        text = text[:text.index('struct Vector3_ForceDestructor')] + '''
extern "C" void sm64ds_vector3_complete_destructor(void*) asm("_ZN7Vector3D1Ev");
extern "C" void sm64ds_vector3_complete_destructor(void*) {}
'''
        changes.append('emit-exact-empty-Vector3-complete-destructor-ABI-symbol')
    if name == 'src/_ZN7fBase_c13InitResourcesEv.cpp':
        # The ROM build deliberately hides its key function behind an extern-C
        # definition so MWCC does not duplicate the vtable already in ROM. The
        # native build owns no such ROM table: defining the actual member lets
        # GCC emit the class's real vtable/RTTI, in the declared original order.
        text = replace_once(text, '''extern "C" int _ZN7fBase_c13InitResourcesEv(void)
{
    return 1; /* VS_FAIL */
}''', '''#include "fBase_c.h"
s32 fBase_c::InitResources()
{
    return 1; /* VS_FAIL: the original base implementation. */
}''')
        changes.append('define-real-fBase-key-function-and-compiler-owned-vtable')
    if name == 'src/func_ov007_020cca74.c':
        # Original absolute tail branch forwards r0 unchanged. Its no-argument
        # declaration only matched the old code generator accidentally.
        text = replace_once(text, 'extern int LoadFile(void);', 'extern int LoadFile(int handle);')
        text = replace_once(text, 'int func_ov007_020cca74(void)', 'int func_ov007_020cca74(int handle)')
        text = replace_once(text, 'return LoadFile();', 'return LoadFile(handle);')
        changes.append('forward-original-r0-file-handle-through-menu-veneer')
    if name == 'src/func_ov007_020cca68.c':
        # Deallocate's actual body consumes r0 as a pointer and returns void.
        text = replace_once(text, 'extern int Deallocate(void);', 'extern void Deallocate(void *ptr);')
        text = replace_once(text, 'int func_ov007_020cca68(void)', 'void func_ov007_020cca68(void *ptr)')
        text = replace_once(text, 'return Deallocate();', 'Deallocate(ptr);')
        changes.append('forward-original-r0-allocation-through-menu-veneer')
    if name == 'src/func_ov007_020c3e64.c':
        # The allocator's actual definition consumes a byte count in r2 and
        # returns the allocation in r0; the menu caller uses that return value.
        text = replace_once(text, 'extern void func_02059170(int idx, int sel, void *arg);',
                            'extern void *func_02059170(int idx, int sel, int size);')
        text = replace_once(text, 'void func_ov007_020c3e64(void *arg)',
                            'void *func_ov007_020c3e64(int size)')
        text = replace_once(text, '    func_02059170(0, -1, arg);',
                            '    return func_02059170(0, -1, size);')
        changes.append('preserve-menu-allocator-size-and-pointer-return-ABI')
    if name == 'src/func_ov007_020cca80.c':
        text = replace_once(text, 'void func_ov007_020cca80(void)',
                            'unsigned int func_ov007_020cca80(void)')
        text = replace_once(text, '    (void)_ZN4Heap21MaxAllocationUnitSizeEv',
                            '    return _ZN4Heap21MaxAllocationUnitSizeEv')
        changes.append('preserve-max-allocation-size-return-through-menu-callback')
    if name == 'src/_ZN11BillBlaster8BehaviorEv.cpp':
        redundant = [
            'int dBgActor_c_helper::UpdateKillByMegaChar(short, short, short, FixV);',
            'dActor_c* Actor_s::FindWithID(unsigned int);',
            'dActor_c* Actor_s::ClosestPlayer();',
            'dActor_c* Actor_s::Spawn(unsigned int, unsigned int, const Vector3&, const Vector3_16*, signed char, short);',
            'int dBgActor_c_helper::IsClsnInRange(FixV, FixV);',
        ]
        for declaration in redundant:
            text = replace_once(text, declaration, '')
        changes.append('remove-redundant-out-of-class-member-declarations')
    if name == 'src/_ZN5Stage13UpdateMessageEv.cpp':
        # These four in-TU dummy definitions are present in the recovered source
        # but have real enrolled providers. Keeping them lets GCC inline no-ops
        # into Stage before the linker can even report the ODR violation.
        for body in ['bool Message::UpdateWindow() { return 0; }',
                     'void Message::Update() {}', 'void Message::DisplaySaving(u16) {}',
                     'void SaveData::SaveCurrentFile() {}']:
            text = replace_once(text, body, '')
        text = replace_once(text, 'static bool UpdateWindow();', 'static int UpdateWindow();')
        text = replace_once(text, 'static void SaveCurrentFile();', 'static int SaveCurrentFile();')
        changes.append('bind-message-and-save-calls-to-real-enrolled-providers')
    if 'Player.cpp.parts/' in name:
        count = text.count('extern struct State;')
        text = text.replace('extern struct State;', 'using State = Player::State;')
        if count:
            changes.append('name-actual-Player-State-instead-of-illegal-extern-type')
        marker = re.search(r'/\* ROM ordinal \d+ -- (\S+),', text)
        if not marker:
            raise ValueError('Missing Player source provenance')
        # A recovered block sometimes redeclares its own function with a
        # differing pointer spelling. Its actual definition is already visible.
        # Drop only that redundant local prototype, leaving all calls intact.
        own = re.escape(marker[1])
        text, count = re.subn(r'^\s+extern [^;\n]*\b' + own + r'\s*\([^;\n]*\);\s*$', '', text, flags=re.M)
        if count:
            changes.append('remove-redundant-local-self-prototype')
    return text, changes, ('c++' if name.endswith('.cpp') or text.lstrip().startswith('//cpp') else 'c')


def source_token_key(text):
    """Exact non-comment token spelling, including string literal contents.

    This is used only on isolated compiler destructor-family translation units,
    not as a general claim that differently-written functions are equivalent.
    """
    tokens = re.findall(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*|\w+|[^\s]', text, re.S)
    return tuple(t for t in tokens if not t.startswith(('//', '/*')))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--only-source', action='append', default=[],
                        help='Recompile named sources only; refuse stale source/header inputs elsewhere')
    args = parser.parse_args()
    if not 1 <= args.jobs <= 16:
        parser.error('--jobs must be 1..16')
    commit = subprocess.check_output(['git', '-C', str(UPSTREAM), 'rev-parse', 'HEAD'], text=True).strip()
    if commit != PIN:
        parser.error(f'Adapter was reviewed for {PIN}; found {commit}')
    generated = OUT / 'generated'
    generated.mkdir(parents=True, exist_ok=True)
    includes = generated / 'include'
    if args.only_source:
        # Partial builds may reuse only a corpus made with exactly these
        # headers. Check before refreshing generated copies, not afterwards.
        for source in (UPSTREAM/'include').rglob('*'):
            if not source.is_file(): continue
            relative=source.relative_to(UPSTREAM/'include')
            expected=source.read_bytes()
            if relative.as_posix()=='types.h':
                expected=expected.replace(b'#ifdef _MSC_VER\n#include <stddef.h>',
                    b'#if defined(_MSC_VER) || defined(__3DS__)\n#include <stddef.h>')
                expected=expected.replace(b'#if defined(__cplusplus) && defined(_MSC_VER)',
                    b'#if defined(__cplusplus) && (defined(_MSC_VER) || defined(__3DS__))')
            old=includes/relative
            if not old.is_file() or old.read_bytes()!=expected:
                parser.error(f'Header changed; full native build required: {relative}')
    if not args.only_source:
        # Partial builds already verified every adapted header above. Preserve
        # their mtimes so an unrelated source does not rebuild camera/video.
        shutil.copytree(UPSTREAM / 'include', includes, dirs_exist_ok=True)
        types = includes / 'types.h'
        types.write_text(replace_once(types.read_text(), '#ifdef _MSC_VER\n#include <stddef.h>',
                                     '#if defined(_MSC_VER) || defined(__3DS__)\n#include <stddef.h>'))
        types.write_text(replace_once(types.read_text(), '#if defined(__cplusplus) && defined(_MSC_VER)',
                                     '#if defined(__cplusplus) && (defined(_MSC_VER) || defined(__3DS__))'))
    names = set()
    for manifest in (UPSTREAM / 'config/arm9').rglob('delinks.txt'):
        names.update(re.findall(r'^(src/[^\r\n:]+\.(?:cpp|c)):', manifest.read_text(), re.M))
    enrolled_count = len(names)
    if names & ADDITIONAL_RECOVERED_SOURCES:
        raise ValueError('Additional source entered the enrolled set; review ownership')
    names.update(ADDITIONAL_RECOVERED_SOURCES)
    sources = {name: (UPSTREAM / name).read_text() for name in sorted(names)}

    # The consolidated Player TU intentionally preserves incompatible local
    # declarations accepted by MWCC. Isolate its already annotated function
    # blocks into separate C++ TUs, with exactly the shared prefix and body the
    # enrolled source contains. The manifest and source markers must agree.
    player_path = 'src/actors/Player.cpp'
    player = sources.pop(player_path)
    markers = list(re.finditer(r'/\* ROM ordinal (\d+) -- (\S+), (0x[0-9a-f]+), size (0x[0-9a-f]+) \*/', player))
    manifest = json.loads((UPSTREAM / 'config/tu_manifest.d/ov002/Player.json').read_text())
    if len(markers) != len(manifest['functions']):
        raise ValueError('Player manifest/function markers disagree')
    prefix = player[:markers[0].start()]
    split = []
    for i, (marker, function) in enumerate(zip(markers, manifest['functions'])):
        if (int(marker[1]) != function['ordinal'] or marker[2] != function['symbol']
                or marker[3] != function['address']):
            raise ValueError(f'Player manifest mismatch at {i}')
        end = markers[i + 1].start() if i + 1 < len(markers) else player.index('/* Assembled from these legacy')
        name = f'{player_path}.parts/{i:03}.cpp'
        sources[name] = prefix + player[marker.start():end]
        split.append({'source': name, 'symbol': function['symbol'], 'ordinal': i,
                      'original_source': player_path, 'original_line': player[:marker.start()].count('\n') + 1})

    compiler = Path(os.environ.get('DEVKITPRO', '/opt/devkitpro')) / 'devkitARM/bin/arm-none-eabi-g++'
    flags = ['-march=armv6k', '-mtune=mpcore', '-mfloat-abi=hard', '-mtp=soft',
             '-D__3DS__', '-Os', '-fno-strict-aliasing', '-ffunction-sections',
             '-fdata-sections', '-Wno-builtin-declaration-mismatch',
             '-Wa,--noexecstack', '-Werror=trampolines', '-I', str(includes)]

    # GCC emits D0/D1/D2 together from one real destructor body. Remove ONLY
    # whole TUs whose tokens exactly agree and whose nominal symbols are the
    # destructor family of one class. Never pass --allow-multiple-definition.
    families = {}
    candidates = []
    for name, text in sources.items():
        stem = Path(name).stem
        family = re.fullmatch(r'(?:_ZN|_ZThn\d+_N)(\w+)[CD][012]Ev', stem)
        if family:
            candidates.append((name, family[1]))
    def preprocessed_key(candidate):
        name, family = candidate
        result = subprocess.run([str(compiler), *flags, '-std=gnu++17', '-x', 'c++', '-E', '-P',
                                 str(UPSTREAM / name)], capture_output=True, text=True, check=True)
        return name, family, source_token_key(result.stdout)
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for name, family, tokens in pool.map(preprocessed_key, candidates):
            families.setdefault((family, tokens), []).append(name)
    equivalent = {}
    for candidates in families.values():
        if len(candidates) > 1:
            owner = min(candidates, key=lambda n: ('_ZThn' in n, not bool(re.search('[CD]1Ev', n)), n))
            for name in candidates:
                if name != owner:
                    equivalent[name] = owner
    objects = OUT / 'objects'
    objects.mkdir(exist_ok=True)

    def compile_one(name):
        text, changes, language = adapt(name, sources[name])
        target = generated / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text)
        obj = objects / (hashlib.sha256(name.encode()).hexdigest()[:20] + '.o')
        command = [str(compiler), *flags, '-x', language]
        if language == 'c++':
            command.append('-Wno-invalid-offsetof')
        if name == 'src/actors/da1up_c.cpp':
            # The original actor allocator returns NULL on failure, and both
            # ASMP0 factories guard construction. Its recovered class-local
            # new declaration has no noexcept specification, so GCC otherwise
            # assumes success and value-initializes address zero. Preserve the
            # original check without changing the pinned source/header ABI.
            command.append('-fcheck-new')
            changes.append('retain-original-null-check-before-mushroom-construction')
        if name == 'src/ProcessKuppaScript.cpp':
            # Its local FaderBrightness declaration is deliberately a vtable
            # view of a FaderColor object. GCC must keep the original indirect
            # calls, not invent direct calls to shadow overloads that do not
            # exist (Ejj vs Ej). This retains the DS's actual dispatch semantics.
            command += ['-fno-devirtualize', '-fno-devirtualize-speculatively']
            changes.append('retain-original-virtual-dispatch-for-shadow-fader-view')
        if language != 'assembler':
            command += ['-std=gnu++17' if language == 'c++' else '-std=gnu11']
        command += ['-c', str(target), '-o', str(obj)]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode == 0 and name in {
                'src/_ZN8dActor_cC1Ev.cpp','src/_ZN8dActor_cC2Ev.cpp'}:
            # GCC emits C1 and C2 from each constructor definition. These two
            # original TUs spell equivalent work differently, so token-based
            # deduplication must NOT discard either body. Export each TU's
            # enrolled ABI variant and localize only its compiler-created peer.
            peer='_ZN8dActor_cC2Ev' if 'C1Ev' in name else '_ZN8dActor_cC1Ev'
            subprocess.run([str(compiler.with_name('arm-none-eabi-objcopy')),
                            '--localize-symbol',peer,str(obj)],check=True)
            changes.append('localize-compiler-generated-constructor-peer:'+peer)
        return {'source': name, 'compiled': result.returncode == 0, 'adaptations': changes,
                'sha256_original': hashlib.sha256(sources[name].encode()).hexdigest(),
                'object': str(obj.relative_to(ROOT)) if result.returncode == 0 else None,
                'diagnostics': result.stderr.replace(str(generated) + '/', '')}

    started = time.monotonic()
    selected = sorted(sources.keys() - equivalent.keys())
    previous_entries={}
    if args.only_source:
        prior=json.loads((OUT/'report.json').read_text())
        if prior['upstream_commit']!=commit or prior['flags']!=flags:
            parser.error('Pinned source/compiler flags changed; full native build required')
        previous_entries={e['source']:e for e in prior['entries']}
        requested=set(args.only_source)
        if not requested.issubset(selected):
            parser.error('Partial build must name selected source owners: '+str(sorted(requested-set(selected))))
        for name in selected:
            if name in requested: continue
            text,_,_=adapt(name,sources[name])
            old=generated/name
            if name not in previous_entries or not old.is_file() or old.read_text()!=text:
                parser.error('Unselected source changed; include --only-source '+name)
        selected=sorted(requested)
    entries = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for entry in pool.map(compile_one, selected):
            entries.append(entry)
            if len(entries) % 1000 == 0:
                print(f'{len(entries)}/{len(selected)} processed', flush=True)
    if args.only_source:
        previous_entries.update({e['source']:e for e in entries})
        entries=[previous_entries[name] for name in sorted(sources.keys()-equivalent.keys())]
    report = {'scope': 'ARM11 compile corpus; no linked or running game', 'upstream_commit': commit,
              'flags': flags, 'seconds': time.monotonic() - started, 'enrolled': enrolled_count,
              'additional_recovered_sources': sorted(ADDITIONAL_RECOVERED_SOURCES),
              'compiled': sum(e['compiled'] for e in entries), 'selected': len(entries),
              'equivalent_destructor_tus': equivalent, 'split_translation_units': split, 'entries': entries}
    (OUT / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: v for k, v in report.items() if k not in {'entries', 'equivalent_destructor_tus', 'split_translation_units'}}, indent=2))
    return 0 if all(e['compiled'] for e in entries) else 1


if __name__ == '__main__':
    raise SystemExit(main())
