// Old / New 3DS profiles: budgets follow the model and the granted memory.
#include "console_profile.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
using namespace console_profile;

int main() {
    const Profile old_80 = make(false, 80u << 20);
    assert(!old_80.large_memory && old_80.worker_core == 1);
    assert(old_80.gpu_textures == 8u << 20 && old_80.decoded_textures == 8u << 20 && old_80.level_preload == 3u << 20);
    // A New 3DS in the 124 MB mode: larger caches, worker on core 2.
    const Profile new_124 = make(true, 124u << 20);
    assert(new_124.large_memory && new_124.worker_core == 2);
    assert(new_124.gpu_textures > old_80.gpu_textures && new_124.decoded_textures > old_80.decoded_textures &&
           new_124.level_preload > old_80.level_preload);
    // The 3DSX on a New 3DS in an 80/96 MB mode keeps the Old memory budgets.
    for (const uint32_t mode : {64u << 20, 72u << 20, 80u << 20, 96u << 20}) {
        const Profile p = make(true, mode);
        assert(!p.large_memory && p.gpu_textures == old_80.gpu_textures &&
               p.decoded_textures == old_80.decoded_textures && p.level_preload == old_80.level_preload);
    }
    // An Old 3DS never takes the large budgets.
    assert(!make(false, 124u << 20).large_memory);
    std::puts("PASS console profile: Old budgets unless a New 3DS has the 124 MB mode, worker core by model");
}
