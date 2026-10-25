#include "enhance_settings.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>

static void put(const std::string& p, const std::string& text) {
    FILE* f = std::fopen(p.c_str(), "wb"); assert(f);
    assert(std::fwrite(text.data(), 1, text.size(), f) == text.size());
    assert(std::fclose(f) == 0);
}
int main() {
    char directory[] = "/tmp/sm64ds-settings-io-XXXXXX"; assert(mkdtemp(directory));
    const std::string path = std::string(directory) + "/settings.ini";
    auto s = enhance::Settings::safe(); s.audio_volume = 63;
    const auto original = s;
    enhance::ParseReport report; report.applied = 789;
    auto rejected = [&](const std::string& input) {
        put(path, input); assert(!enhance::load_file(path.c_str(), s, &report));
        assert(s == original && report.applied == 789);
    };
    rejected("[audio]\nvolume=0\n" + std::string(5000, '#'));
    rejected(std::string("[audio]\nvolume=0\n\0[input]\nmode=2\n", 32));
    assert(!enhance::load_file(directory, s, &report)); // fread error (directory)
    assert(s == original && report.applied == 789);
    std::string line = "[audio]\nvolume=0" + std::string(300, ' ') + "oops\n";
    auto r = enhance::parse_ini(line.c_str(), s);
    assert(r.invalid == 1 && r.applied == 0 && s == original);
    r = enhance::parse_ini("[audio]bad\nvolume=0\n", s);
    assert(r.invalid == 1 && r.applied == 0 && s == original);
    char canary[2] = {'x', 'y'};
    assert(enhance::write_ini(s, canary, -1) == -1 && canary[0] == 'x' && canary[1] == 'y');
    assert(enhance::write_ini(s, nullptr, 4096) == -1);
    assert(enhance::parse_ini(nullptr, s).invalid == 1);
    assert(enhance::save_file(path.c_str(), original));
    auto loaded = enhance::Settings::recommended();
    assert(enhance::load_file(path.c_str(), loaded) && loaded == original);
    // Interrupted fallback: old complete settings load from backup and the
    // next checked save repairs the primary before replacing it.
    const auto backup = path + ".bak";
    assert(std::rename(path.c_str(), backup.c_str()) == 0);
    assert(enhance::load_file(path.c_str(), loaded) && loaded == original);
    s.audio_volume = 124; assert(enhance::save_file(path.c_str(), s));
    assert(enhance::load_file(path.c_str(), loaded) && loaded == s);
    // Dev/test environment input must never turn snprintf's required length
    // into an out-of-bounds terminator index; later valid entries still apply.
    const std::string override = "audio.volume=" + std::string(5000, '1') + ";audio.volume=87";
    assert(setenv("SM64DS_SETTINGS", override.c_str(), 1) == 0);
    assert(enhance::boot_apply(path.c_str()).audio_volume == 87);
    assert(unsetenv("SM64DS_SETTINGS") == 0);
    // Lock FPS: Auto follows the console (Old 30, New 60) and stays Auto on
    // disk; an explicit choice survives a roundtrip on either model.
    {
        auto lock = enhance::Settings::safe();
        assert(lock.fps_lock == enhance::kFpsLockAuto);
        enhance::set_console_model(false); enhance::publish(lock);
        assert(enhance::live().fps_lock == enhance::kFpsLock30 && enhance::effective_fps_lock(lock) == enhance::kFpsLock30);
        enhance::set_console_model(true);
        assert(enhance::live().fps_lock == enhance::kFpsLock60 && enhance::effective_fps_lock(lock) == enhance::kFpsLock60);
        char text[4096]; const int n = enhance::write_ini(lock, text, sizeof text);
        assert(n > 0 && n < int(sizeof text) && std::strstr(text, "fps_lock = auto"));
        for (const char* value : {"off", "30", "60", "auto"}) {
            auto parsed = enhance::Settings::safe(); parsed.fps_lock = 77;
            const std::string ini = std::string("[display]\nfps_lock = ") + value + "\n";
            assert(enhance::parse_ini(ini.c_str(), parsed).applied == 1);
            assert(enhance::write_ini(parsed, text, sizeof text) > 0 && std::strstr(text, (std::string("fps_lock = ") + value).c_str()));
            auto again = enhance::Settings::safe(); enhance::parse_ini(text, again); assert(again == parsed);
        }
        auto bad = enhance::Settings::safe();
        assert(enhance::parse_ini("[display]\nfps_lock = 45\n", bad).invalid == 1 && bad.fps_lock == enhance::kFpsLockAuto);
        lock.fps_lock = enhance::kFpsLockOff; enhance::publish(lock);
        assert(enhance::live().fps_lock == enhance::kFpsLockOff);
        enhance::set_console_model(false); assert(enhance::live().fps_lock == enhance::kFpsLockOff);
    }
    assert(std::remove(path.c_str()) == 0); assert(rmdir(directory) == 0);
    std::puts("PASS settings reject partial/oversize/NUL input without mutation, parser boundaries, roundtrip and backup recovery");
}
