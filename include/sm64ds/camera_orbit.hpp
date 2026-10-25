#pragma once
#include <cstddef>
#include <cstdint>

namespace sm64ds::camera {
constexpr std::int32_t stick_one = 32768;
constexpr std::size_t trig_entries = 8192;

struct Vector { std::int32_t x=0, y=0, z=0; };
struct Stick { std::int32_t x=0, y=0; }; // Normalized Q15; +y means up.
struct Identity {
    // Opaque ARM32 identities, never dereferenced. Generation must come from
    // the owning scene/camera lifecycle, not from a heap residency test.
    std::uint32_t camera=0, target=0, mode=0, generation=0;
};
struct Config {
    // Q16 original angle units/second; 65536 angle units are a full turn.
    std::uint32_t yaw_rate=2147483648u;   // 180 degrees/second.
    std::uint32_t pitch_rate=1431655765u; // 120 degrees/second.
    std::uint32_t response_us=60000;
    std::uint32_t max_elapsed_us=100000;
    std::int32_t pitch_limit=0x3c72; // About 85 degrees, away from LookAt poles.
};
struct Tick {
    Identity identity;
    std::uint64_t number=0;
    // Duration from the ORIGINAL game/VBlank clock. This is not wall time,
    // emulator elapsed time, or a request to advance simulation.
    std::uint32_t elapsed_us=0;
    Stick stick;
    Vector target;
    std::int16_t seed_yaw=0, seed_pitch=0;
    std::int32_t seed_radius=0;
    bool allowed=false, paused=false;
};
enum class Status : std::uint32_t {
    idle, reseeded, candidate, suspended, duplicate, stale,
    invalid_identity, invalid_input, invalid_config, invalid_time,
    invalid_seed, invalid_geometry
};
struct Result {
    Status status=Status::idle;
    bool has_candidate=false;
    Vector eye;
    std::int16_t yaw=0, pitch=0;
    std::int32_t radius=0;
};
struct State {
    Identity identity;
    std::uint64_t last_tick=0;
    std::uint32_t yaw_q16=0;
    std::int32_t pitch_q16=0, radius=0;
    std::int64_t yaw_velocity=0, pitch_velocity=0;
    std::uint32_t substep_us=0;
    bool has_tick=false, armed=false;
};

// A trusted, caller-owned original LUT is required. No allocation, game
// pointers, globals, input publication, collision result, or matrix is read.
// Returns false without changing out for bad LUT entries/radius or overflow.
bool orbit_position(Vector target, std::int32_t radius,
                    std::int16_t yaw, std::int16_t pitch,
                    const std::int16_t* lut, std::size_t entries, Vector& out);

class Orbit {
public:
    explicit Orbit(Config config={}) : config_(config) {}
    void reset();
    Result step(const Tick& tick, const std::int16_t* lut,
                std::size_t entries=trig_entries);
    // The future collision adapter may rebase a blocked orientation from the
    // ORIGINAL solved vectors. This does not certify or execute a collision.
    bool rebase(std::int16_t yaw, std::int16_t pitch);
    const State& state() const { return state_; }
private:
    void disarm();
    Config config_;
    State state_;
    std::int64_t yaw_remainder_=0, pitch_remainder_=0;
    std::uint32_t neutral_us_=0;
};
}
