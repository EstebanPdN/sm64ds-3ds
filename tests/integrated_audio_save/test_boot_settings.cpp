// The 3DS game's global new uses Heap::defaultHeap, null until smoke_main.
// Reject every C++ allocation during the real settings I/O path; libc's C
// allocator remains usable exactly as it is before the retail heap exists.
#include "enhance_settings.h"
#include "atomic_file.hpp"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <new>
#include <unistd.h>

static bool heap_unavailable = false;
static unsigned rejected_allocations = 0;
void* operator new(std::size_t size) {
    if (heap_unavailable) { ++rejected_allocations; throw std::bad_alloc(); }
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

int main() {
    char directory[] = "/tmp/sm64ds-pre-heap-XXXXXX"; assert(mkdtemp(directory));
    char path[256], backup[260];
    std::snprintf(path, sizeof path, "%s/settings.ini", directory);
    std::snprintf(backup, sizeof backup, "%s.bak", path);
    assert(unsetenv("SM64DS_SETTINGS") == 0);
    // Both model profiles: no settings and no .bak is the first-install path
    // that faulted on hardware. Follow it with existing + recovered settings.
    for (bool new_model : {false, true}) {
        enhance::set_console_model(new_model);
        heap_unavailable = true;
        auto defaults = enhance::boot_apply(path);
        assert(defaults == enhance::Settings::safe());
        assert(enhance::live().fps_lock == (new_model ? enhance::kFpsLock60 : enhance::kFpsLock30));
        auto saved = defaults; saved.audio_volume = 72;
        assert(enhance::save_file(path, saved));
        assert(enhance::boot_apply(path) == saved);
        assert(std::rename(path, backup) == 0);
        assert(enhance::boot_apply(path) == saved);
        saved.audio_volume = 83;
        assert(enhance::save_file(path, saved));
        assert(enhance::boot_apply(path) == saved);
        assert(std::remove(path) == 0);
        heap_unavailable = false;
    }
    assert(rejected_allocations == 0);
    // Reject overlong sibling paths before touching the filesystem.
    char long_path[integrated_storage::path_capacity + 1];
    std::memset(long_path, 'x', sizeof long_path); long_path[sizeof long_path-1] = 0;
    heap_unavailable = true;
    errno = 0;
    assert(!integrated_storage::replace_file(long_path, "x", 1) && errno == ENAMETOOLONG);
    char sibling[integrated_storage::path_capacity];
    assert(!integrated_storage::sibling_path(sibling, long_path, ".bak"));
    long_path[integrated_storage::path_capacity-5] = 0;
    assert(integrated_storage::sibling_path(sibling, long_path, ".bak"));
    assert(std::strlen(sibling) == integrated_storage::path_capacity-1);
    heap_unavailable = false;
    assert(rejected_allocations == 0 && rmdir(directory) == 0);
    std::puts("PASS Old/New first boot, existing/backup settings and writes without C++ heap; bounded sibling paths");
}
