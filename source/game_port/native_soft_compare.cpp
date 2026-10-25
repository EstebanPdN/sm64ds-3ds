#include <cstdint>

// The original soft-float helper passes IEEE-754 words in r0/r1 and returns
// an integer word in r0. A C++ float signature would select the VFP ABI.
extern "C" {
void* func_0207322c();
std::uint32_t func_0206dd30(std::uint32_t first, std::uint32_t second,
                          std::uint32_t operation);
}

namespace {
std::int32_t ordered_key(std::uint32_t bits) {
    const auto magnitude = std::int32_t(bits & 0x7fffffffu);
    return bits & 0x80000000u ? -magnitude : magnitude;
}
}

extern "C" std::uint32_t func_01ff98f4(std::uint32_t first,
                                      std::uint32_t second) {
    // Relational '<' raises invalid for quiet and signaling NaNs alike.
    // Preserve the original software FP status, including rounding controls.
    if ((first & 0x7fffffffu) > 0x7f800000u ||
        (second & 0x7fffffffu) > 0x7f800000u) {
        auto* status = static_cast<std::uint32_t*>(func_0207322c());
        if (*status & 0x100u) {
            // The original enabled-exception path leaves sticky untouched,
            // delegates the original operands and returns the dispatch word.
            // An unavailable dispatcher must fail through its real provider.
            return func_0206dd30(first, second, 0x04000049u);
        }
        *status |= 1u;
        return 0;
    }
    // Sign/magnitude ordering also handles infinities, subnormals and ±0.
    return ordered_key(first) < ordered_key(second) ? 1u : 0u;
}
