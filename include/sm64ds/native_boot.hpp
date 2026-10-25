#pragma once

#include <cstdint>

namespace sm64ds {

// These switches declare incomplete services. They never report that sound or
// wireless succeeded. Their default keeps single-player integration independent
// of the DS ARM7 sound/wireless processors.
struct NativeBootOptions {
    bool original_audio = false;
    bool original_wireless = false;
};

struct NativeBootCallbacks {
    void* context = nullptr;
    // The two display-engine brightness registers are not native 3DS addresses.
    // Route them to the 2D compositor's register state. Unsupported writes fail.
    bool (*write_register)(void*, std::uint32_t address,
                           std::uint32_t value, unsigned bytes) = nullptr;
    // begin_tick: latch input/reset the GX command stream.
    // input_ready: after the original pad/touch samplers, retain pending input
    // edges and publish Circle Pad state (NativeFrame provides these callbacks).
    bool (*begin_tick)(void*) = nullptr;
    bool (*input_ready)(void*) = nullptr;
    bool (*frame_ready)(void*) = nullptr;
    // 3DS suspend/lid integration. This replaces the DS firmware read and its
    // ARM7 sleep handshake in func_02019ac4; it must not advance game time.
    bool (*power_tick)(void*) = nullptr;
    // Optional logging/diagnostic callbacks; neither changes game behavior.
    void (*report)(void*, const char*) = nullptr;
    bool (*runtime_ok)(void*) = nullptr;
};

enum class NativeBootState : std::uint8_t {
    not_started, initializing, ready, waiting_vblank, failed
};

// Runs the original init/scene/actor loop. ROM data, pointer relocation, the
// private original heap and original static constructors must be ready first.
// No hard-coded title/Stage factory, replacement menu, or player update lives
// here: Scene::SpawnIfNecessary consumes the original scene request and table.
//
// Display-loop order:
//   NativeFrame::sample_input();
//   if (boot.state()==ready) boot.begin_tick();
//   NativeFrame::present(); native_platform_advance_vblank();
//   boot.after_vblank();
//
// A simulation tick remains asleep until the original VBlank handler wakes its
// queue. In particular, data_0208ee44=2 means TWO display refreshes per update.
// The current 30 Hz simulation is not silently doubled to claim 60 FPS.
class NativeBoot {
public:
    bool initialize(const NativeBootCallbacks&, NativeBootOptions = {});
    bool begin_tick();
    bool after_vblank();
    NativeBootState state() const { return state_; }
    const char* failure() const { return failure_; }
    const char* operation() const { return operation_; }
    std::uint64_t completed_ticks() const { return ticks_; }

private:
    bool check(const char* operation);
    bool fail(const char* reason);
    bool write_register(std::uint32_t address, std::uint32_t value, unsigned bytes);
    void report(const char* message) const;
    NativeBootCallbacks callbacks_{};
    NativeBootOptions options_{};
    NativeBootState state_ = NativeBootState::not_started;
    const char* failure_ = nullptr;
    const char* operation_ = nullptr;
    std::uint64_t ticks_ = 0;
};

} // namespace sm64ds
