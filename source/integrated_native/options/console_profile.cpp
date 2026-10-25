// Console profile detection (console_profile.hpp) and the C entry points of
// the translation units that cannot include libctru (ntr/gx.cpp, hal/fs.cpp).
#include "console_profile.hpp"
#include <3ds.h>
#include <cstdio>

namespace console_profile {
const Profile& current() {
    static const Profile profile = [] {
        bool new_model = false;
        if (R_FAILED(APT_CheckNew3DS(&new_model))) new_model = false;
        const Profile p = make(new_model, uint32_t(osGetMemRegionSize(MEMREGION_APPLICATION)));
        std::printf("Console profile model=%s appMemoryMiB=%u largeMemory=%u gpuTexturesMiB=%u decodedTexturesMiB=%u levelPreloadMiB=%u workerCore=%d\n",
                    p.new_model ? "new" : "old", unsigned(p.app_memory >> 20), unsigned(p.large_memory),
                    unsigned(p.gpu_textures >> 20), unsigned(p.decoded_textures >> 20), unsigned(p.level_preload >> 20),
                    p.worker_core);
        return p;
    }();
    return profile;
}
}

extern "C" std::size_t sm64ds_profile_decoded_texture_budget() { return console_profile::current().decoded_textures; }
extern "C" std::size_t sm64ds_profile_level_preload_budget() { return console_profile::current().level_preload; }
