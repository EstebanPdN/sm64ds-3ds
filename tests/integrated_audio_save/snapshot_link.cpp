#include "audio_diagnostics.hpp"
// Relocatable ARM link probe: this TU must share the inline counter object
// published by the independently compiled production output TU.
extern "C" uint32_t audio_snapshot_link_probe() {
    return enhance::audio_diagnostics::snapshot().submitted;
}
