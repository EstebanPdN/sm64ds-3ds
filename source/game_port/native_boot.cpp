#include "sm64ds/native_boot.hpp"
#include "sm64ds/game_draw.hpp"
#include "sm64ds/native_platform.hpp"
#include <initializer_list>
#include <cstdio>

// The bodies below follow upstream/src/main.c, func_0201a054.c and
// func_020197b8.c in the pinned port revision. The only split in the game loop
// is its own OS_SleepThread boundary. The SDK functions remain real link
// dependencies: the runtime builder must provide native services or stop with
// their names. This adapter contains no success-returning SDK stubs.
extern "C" {
void func_02059788();
void func_02059bc0();
void func_02059578(std::uint32_t, std::uint32_t);
void func_0201a03c(int);
void _ZN4Heap9SetNodeIDEj(void*, unsigned);
void func_020427f8();
std::uint64_t func_02042784();
void func_02053a8c();
void func_02053c40();
void func_0205b858();
void func_0203d740();
void func_02013e64();
void func_0201a4e4();
unsigned _ZN3IRQ10EnableIRQsEj(unsigned);
unsigned _ZN3IRQ11DisableIRQsEj(unsigned);
unsigned _ZN3IRQ6EnableEv();
int func_02053be0(int);
void func_0203bbc0();
void func_0203bb5c();
void func_0201fec8();
void func_02042f68(int, void*);
void func_02018aa4();
void func_0203ad84();
void func_0201a9fc(void*);
void _ZN4Heap18InitializeGameHeapEjPS_(unsigned, void*);
void func_02019440();
void func_020134c8();
void func_0203b684();
void func_020233f0();
void _ZN8dScene_c18PrepareToSpawnBootEv();
void func_020196cc();
void func_0201a5cc();
void func_02019390();
void func_0203bb60();
void func_0203bc7c();
void func_02023498();
int _ZN8dScene_c16SpawnIfNecessaryEv();
void func_02044120();
void func_02019404();
int func_02019018();
void func_0201a4bc();
void func_0201ffcc();
void func_020132d8();
unsigned short func_0203da80(unsigned short);
int func_02030e4c();

extern unsigned char data_020a621c[];
extern void* data_020a0e9c;
extern unsigned char data_0208ee50[];
extern unsigned char data_0209d574[];
extern unsigned char ACTOR_SPAWN_TABLE[];
extern void* data_020a4bb8;
extern unsigned char data_0209ee90[];
extern int data_0209d50c;
extern unsigned char data_0209d4e8;
extern unsigned char data_020a0e40;
extern unsigned short data_020a0e58[];
extern unsigned short data_0209d534[];
extern int data_0209d51c;
extern std::uint32_t data_020a0db0;
extern unsigned char data_0209d4f0;
extern unsigned char data_0209d464;
extern int data_0209d514;
extern unsigned char data_0209b480;
extern unsigned char data_0209e64c;
extern unsigned short data_02092664;
extern unsigned char data_02092660;
}

namespace sm64ds {
bool NativeBoot::fail(const char* reason) {
    failure_ = reason;
    state_ = NativeBootState::failed;
    report(reason);
    return false;
}
void NativeBoot::report(const char* message) const {
    if (callbacks_.report) callbacks_.report(callbacks_.context, message);
}
bool NativeBoot::check(const char* operation) {
    if(state_==NativeBootState::initializing && operation_!=operation)report(operation);
    operation_ = operation;
    if (!native_platform_ok()) return fail("native platform service failed");
    if (callbacks_.runtime_ok && !callbacks_.runtime_ok(callbacks_.context))
        return fail("original runtime stopped at an unresolved service");
    return true;
}
bool NativeBoot::write_register(std::uint32_t address, std::uint32_t value, unsigned bytes) {
    if (!callbacks_.write_register(callbacks_.context, address, value, bytes))
        return fail("display register has no native compositor provider");
    return true;
}

// Record the active call BEFORE entering it, so a fail-fast provider can name
// the actual boot boundary. Also observe providers that report an error and
// return; no later original phase runs after such a report.
#define ORIGINAL_CALL(function, ...) do { \
    if (!check(#function)) return false; \
    function(__VA_ARGS__); \
    if (!check(#function)) return false; \
} while (false)

bool NativeBoot::initialize(const NativeBootCallbacks& callbacks, NativeBootOptions options) {
    if (state_ != NativeBootState::not_started)
        return fail("native boot may only initialize once");
    callbacks_ = callbacks;
    options_ = options;
    if (!callbacks_.write_register || !callbacks_.begin_tick || !callbacks_.input_ready ||
        !callbacks_.frame_ready || !callbacks_.power_tick)
        return fail("native boot requires display, input and power services");
    if (!data_020a0e9c) return fail("original private root heap is not initialized");
    state_ = NativeBootState::initializing;
    report("original boot sequence; title and Stage are selected by the game's scene table");
    if (!options_.original_audio) report("audio is disabled and unimplemented in this integration");
    if (!options_.original_wireless) report("wireless is disabled in this single-player integration");

    // main.c, before its call to func_0201a054.
    ORIGINAL_CALL(func_02059788);
    ORIGINAL_CALL(func_02059bc0);
    ORIGINAL_CALL(func_02059578, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&func_0201a03c)),
                  static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(data_020a621c)));
    ORIGINAL_CALL(_ZN4Heap9SetNodeIDEj, data_020a0e9c, 2);

    // func_0201a054. A native application boots as a cartridge game, never as
    // DS download-play firmware. Platform owns the boot indicator's value.
    const bool download_play = native_platform_boot_indicator() == 2;
    ORIGINAL_CALL(func_020427f8);
    ORIGINAL_CALL(func_02042784);
    ORIGINAL_CALL(func_02053a8c);
    ORIGINAL_CALL(func_02053c40);
    ORIGINAL_CALL(func_0205b858);
    ORIGINAL_CALL(func_0203d740);
    ORIGINAL_CALL(func_02013e64);
    ORIGINAL_CALL(func_0201a4e4);
    ORIGINAL_CALL(_ZN3IRQ10EnableIRQsEj, 1);
    native_platform_irq_set_master(true); // DS IME=1 in the original init.
    ORIGINAL_CALL(_ZN3IRQ6EnableEv);
    ORIGINAL_CALL(func_02053be0, 1);
    ORIGINAL_CALL(func_0203bbc0);
    ORIGINAL_CALL(func_0203bb5c);
    if (!download_play && options_.original_wireless) ORIGINAL_CALL(func_0201fec8);
    ORIGINAL_CALL(func_02042f68, 0xd01, data_0208ee50);
    if (!download_play) ORIGINAL_CALL(func_02018aa4);
    if (download_play) ORIGINAL_CALL(_ZN3IRQ11DisableIRQsEj, 0x100000);
    if (!download_play) ORIGINAL_CALL(func_0203ad84);
    ORIGINAL_CALL(func_0201a9fc, data_0209d574);
    ORIGINAL_CALL(_ZN4Heap18InitializeGameHeapEjPS_, 0x3b000, nullptr);
    ORIGINAL_CALL(func_0201a9fc, data_0209d574);
    ORIGINAL_CALL(func_02019440);
    if (!download_play && options_.original_audio) ORIGINAL_CALL(func_020134c8);
    ORIGINAL_CALL(func_0203b684);
    data_020a4bb8 = ACTOR_SPAWN_TABLE;
    ORIGINAL_CALL(func_020233f0);
    ORIGINAL_CALL(_ZN8dScene_c18PrepareToSpawnBootEv);
    ORIGINAL_CALL(func_020196cc);
    *reinterpret_cast<std::uint32_t*>(data_0209ee90 + 0x44) = 0x100;
    // Original GX defaults, submitted as native command records instead of
    // stores to DS geometry-engine ports.
    sm64ds_draw_port(0x32, 0x296a5800);
    sm64ds_draw_port(0x33, 0x7fff);
    sm64ds_draw_port(0x30, 0x7fff);
    sm64ds_draw_port(0x31, 0);
    ORIGINAL_CALL(func_0201a5cc);
    ORIGINAL_CALL(_ZN4Heap9SetNodeIDEj, data_020a0e9c, 3);

    // func_020197b8's one-time fade-to-white setup, as performed by
    // G2x::SetBlendBrightness(reg, 0x3f, 0x1f).
    for (const auto base : {0x04000050u, 0x04001050u}) {
        if (!write_register(base, 0xbf, 2) || !write_register(base + 4, 0x1f, 2)) return false;
    }
    state_ = NativeBootState::ready;
    operation_ = "ready for original scene dispatch";
    return true;
}

bool NativeBoot::begin_tick() {
    if (state_ != NativeBootState::ready) return fail("begin_tick requires a completed VBlank wait");
    if (!check("begin_tick") || !callbacks_.begin_tick(callbacks_.context))
        return fail("native frame begin failed");
    data_0209d50c = 0xb;
    if (!callbacks_.power_tick(callbacks_.context)) return fail("native suspend service failed");
    data_0209d50c = 2;
    ORIGINAL_CALL(func_02019390);
    data_0209d50c = 0x16;
    ORIGINAL_CALL(func_0203bb60);
    data_0209d50c = 0x17;
    ORIGINAL_CALL(func_0203bc7c);
    if (!callbacks_.input_ready(callbacks_.context)) return fail("native input publication failed");
    const auto held = data_020a0e58[data_020a0e40 * 2];
    data_0209d534[data_0209d4e8] = held;
    data_0209d51c = held;
    data_0209d4e8 = (data_0209d4e8 + 1) & 0x1f;
    ORIGINAL_CALL(func_02023498);
    data_0209d50c = 3;
    if(!data_02092660 && data_02092664!=0x187) {
        char message[80];
        std::snprintf(message,sizeof(message),"original scene request id=%u",unsigned(data_02092664));
        report(message);
    }
    ORIGINAL_CALL(_ZN8dScene_c16SpawnIfNecessaryEv);
    data_0209d50c = 4;
    ORIGINAL_CALL(func_02044120);
    data_0209d50c = 5;
    ORIGINAL_CALL(func_02019404);
    data_0209d50c = 6;
    ++data_020a0db0;
    ORIGINAL_CALL(_ZN3IRQ11DisableIRQsEj, 1);
    // func_020190b8: its only hardware write is SWAP_BUFFERS.
    if (func_02019018()) {
        sm64ds_draw_port(0x50, 3);
        data_0209d464 = 1;
    }
    data_0209d4f0 = 1;
    ORIGINAL_CALL(_ZN3IRQ10EnableIRQsEj, 1);
    data_0209d50c = 7;
    ORIGINAL_CALL(func_0201a4bc);
    state_ = NativeBootState::waiting_vblank;
    if (!native_platform_is_waiting()) return fail("original VBlank wait did not register its queue");
    if (!callbacks_.frame_ready(callbacks_.context)) return fail("native frame decoding failed");
    return true;
}

bool NativeBoot::after_vblank() {
    if (state_ != NativeBootState::waiting_vblank)
        return fail("after_vblank requires the original phase-7 wait");
    if (!check("VBlank wake")) return false;
    if (native_platform_is_waiting()) return true;
    // The tail of the same original iteration executes only after the real
    // original VBlank handler has woken data_0209d500.
    data_0209d4f0 = 0;
    data_0209d50c = 0x15;
    if (options_.original_wireless) ORIGINAL_CALL(func_0201ffcc);
    if ((*reinterpret_cast<std::uint32_t*>(data_0209ee90 + 0x108) & 0x10) == 0)
        data_0209d514 = 0;
    data_0209d50c = 9;
    if (options_.original_audio && data_0209b480) ORIGINAL_CALL(func_020132d8);
    if (options_.original_wireless && (data_0209e64c || func_0203da80(0x9d)))
        ORIGINAL_CALL(func_02030e4c);
    ++ticks_;
    state_ = NativeBootState::ready;
    operation_ = "original simulation iteration complete";
    return true;
}
#undef ORIGINAL_CALL
} // namespace sm64ds
