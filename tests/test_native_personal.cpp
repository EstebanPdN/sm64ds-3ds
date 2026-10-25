// Synthetic CFGU responses for host/ARM11 tests only. Production links libctru.
#include "sm64ds/native_personal.hpp"
#include "sm64ds/native_platform.hpp"

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>

extern "C" {
#include <3ds/services/cfgu.h>
std::uint8_t test_cfg_name[0x1c], test_cfg_birthday[2], test_cfg_language;
std::uint32_t test_cfg_calls[5], test_platform_failures, test_cfg_protocol_errors;
std::int32_t test_cfg_failure_stage, test_cfg_success_result;
}

namespace {
constexpr std::int32_t kFailure = -0x1234567;
Result service_result(int stage) {
    return stage == test_cfg_failure_stage ? kFailure : test_cfg_success_result;
}
}

extern "C" Result cfguInit() {
    ++test_cfg_calls[0];
    return service_result(1);
}
extern "C" void cfguExit() { ++test_cfg_calls[4]; }
extern "C" Result CFGU_GetSystemLanguage(u8* language) {
    ++test_cfg_calls[1];
    if (test_cfg_failure_stage != 2) *language = test_cfg_language;
    return service_result(2);
}
extern "C" Result CFGU_GetConfigInfoBlk2(u32 size, u32 block, void* destination) {
    int stage;
    const void* source;
    if (size == 0x1c && block == 0xa0000) {
        ++test_cfg_calls[2]; stage = 3; source = test_cfg_name;
    } else if (size == 2 && block == 0xa0001) {
        ++test_cfg_calls[3]; stage = 4; source = test_cfg_birthday;
    } else {
        ++test_cfg_protocol_errors;
        return kFailure;
    }
    if (test_cfg_failure_stage != stage) std::memcpy(destination, source, size);
    return service_result(stage);
}

namespace sm64ds {
void native_platform_fail(const char*) { ++test_platform_failures; }
}
extern "C" [[noreturn]] void sm64ds_native_missing(const char*) { std::abort(); }

extern "C" unsigned test_personal_initialize() {
    return sm64ds::native_personal_initialize();
}
extern "C" unsigned test_personal_copy(void* destination, unsigned capacity) {
    return sm64ds::native_personal_copy(destination, capacity);
}
extern "C" unsigned test_personal_stat(unsigned index) {
    const auto stats = sm64ds::native_personal_stats();
    switch (index) {
    case 0: return stats.initialization_attempted;
    case 1: return stats.ready;
    case 2: return stats.english_fallback;
    case 3: return stats.copies;
    case 4: return stats.failures;
    case 5: return unsigned(stats.service_result);
    case 6: return stats.last_error != nullptr;
    default: return 0xffffffffu;
    }
}

#ifdef SM64DS_NATIVE_PERSONAL_HOST_TEST
#include <array>
#include <cstdio>

int main(int argc, char** argv) {
    assert(argc == 3);
    const unsigned seed = std::strtoul(argv[1], nullptr, 10);
    const unsigned scenario = std::strtoul(argv[2], nullptr, 10);
    const unsigned length = seed % 11;
    std::memset(test_cfg_name, 0xce, sizeof(test_cfg_name));
    for (unsigned index = 0; index < length; ++index) {
        const auto unit = std::uint16_t(0x100 + seed + index);
        test_cfg_name[index * 2] = std::uint8_t(unit);
        test_cfg_name[index * 2 + 1] = std::uint8_t(unit >> 8);
    }
    if (length < 10) {
        test_cfg_name[length * 2] = 0;
        test_cfg_name[length * 2 + 1] = 0;
    }
    test_cfg_language = std::uint8_t(seed % 12);
    test_cfg_birthday[0] = std::uint8_t(1 + seed % 12);
    test_cfg_birthday[1] = std::uint8_t(1 + seed % 28);
    if (scenario >= 1 && scenario <= 4) test_cfg_failure_stage = scenario;
    if (scenario == 5) test_cfg_language = 255;
    if (scenario == 6) test_cfg_birthday[0] = 0;
    if (scenario == 7) test_cfg_birthday[1] = 0;
    if (scenario == 8) { test_cfg_birthday[0] = 4; test_cfg_birthday[1] = 31; }
    if (scenario >= 9 && scenario <= 12) {
        std::memset(test_cfg_name, 0, sizeof(test_cfg_name));
        test_cfg_name[0] = 0x01; test_cfg_name[1] = 0xd8;
        if (scenario == 10) test_cfg_name[1] = 0xdc;
        if (scenario == 11) test_cfg_name[2] = 0x41;
        if (scenario == 12) { test_cfg_name[2] = 0x02; test_cfg_name[3] = 0xdc; }
    }
    // Successful nonzero Result codes must still count as success.
    test_cfg_success_result = (seed & 1) ? 0x08000001 : 0;
    alignas(4) std::array<std::uint8_t, 160> output;
    output.fill(0xa5);
    if (scenario == 13) {
        assert(!sm64ds::native_personal_copy(output.data() + 16, 80));
        for (auto byte : output) assert(byte == 0xa5);
        assert(test_cfg_calls[0] == 0);
        assert(test_personal_stat(0) == 0 && test_personal_stat(1) == 0);
    } else {
        const bool valid = scenario == 0 || scenario == 12;
        assert(sm64ds::native_personal_initialize() == valid);
        const auto calls = std::array<std::uint32_t, 5>{test_cfg_calls[0], test_cfg_calls[1],
            test_cfg_calls[2], test_cfg_calls[3], test_cfg_calls[4]};
        assert(calls[0] == 1);
        assert(calls[1] == (scenario == 1 ? 0 : 1));
        assert(calls[2] == (scenario == 1 || scenario == 2 ? 0 : 1));
        assert(calls[3] == (scenario >= 1 && scenario <= 3 ? 0 : 1));
        assert(calls[4] == (scenario == 1 ? 0 : 1));
        assert(test_cfg_protocol_errors == 0);
        assert(sm64ds::native_personal_initialize() == valid);
        assert(!std::memcmp(test_cfg_calls, calls.data(), sizeof(test_cfg_calls)));
        assert(test_personal_stat(0) == 1 && test_personal_stat(1) == valid);
        if (!valid) {
            assert(!sm64ds::native_personal_copy(output.data() + 16, 80));
            for (auto byte : output) assert(byte == 0xa5);
            assert(test_personal_stat(6) == 1 && test_platform_failures > 0);
            if (scenario >= 1 && scenario <= 4)
                assert(test_personal_stat(5) == unsigned(kFailure));
        } else {
            std::array<std::uint8_t, 80> expected{};
            expected[0] = test_cfg_language >= 1 && test_cfg_language <= 5
                ? test_cfg_language : 1;
            expected[2] = test_cfg_birthday[0]; expected[3] = test_cfg_birthday[1];
            std::memcpy(expected.data() + 4, test_cfg_name, 20);
            expected[0x18] = std::uint8_t(scenario == 12 ? 2 : length);
            const unsigned offsets[] = {0, 2, 8, 14, 32, 64};
            for (auto offset : offsets) {
                output.fill(0xa5);
                assert(sm64ds::native_personal_copy(output.data() + offset, 80));
                assert(!std::memcmp(output.data() + offset, expected.data(), 80));
                for (unsigned index = 0; index < output.size(); ++index)
                    if (index < offset || index >= offset + 80) assert(output[index] == 0xa5);
            }
            // SDK's common caller reserves 84 bytes; the extra 4 stay intact.
            output.fill(0xa5);
            func_02059eb0(output.data() + 16);
            assert(!std::memcmp(output.data() + 16, expected.data(), 80));
            for (unsigned index = 96; index < output.size(); ++index) assert(output[index] == 0xa5);
            // Cached settings are immutable for this boot even if service fixture changes.
            test_cfg_language = 3; std::memset(test_cfg_name, 0, sizeof(test_cfg_name));
            assert(sm64ds::native_personal_initialize());
            assert(sm64ds::native_personal_copy(output.data() + 16, 84));
            assert(!std::memcmp(output.data() + 16, expected.data(), 80));
            assert(!std::memcmp(test_cfg_calls, calls.data(), sizeof(test_cfg_calls)));
            assert(test_personal_stat(3) == 8);
            assert(test_platform_failures == 0);
            output.fill(0xa5);
            assert(!sm64ds::native_personal_copy(output.data() + 16, 79));
            assert(!sm64ds::native_personal_copy(output.data() + 17, 80));
            assert(!sm64ds::native_personal_copy(nullptr, 80));
            assert(!sm64ds::native_personal_copy(reinterpret_cast<void*>(0x027ffc80), 80));
            for (auto byte : output) assert(byte == 0xa5);
            assert(test_personal_stat(3) == 8 && test_personal_stat(4) == 4);
        }
    }
    std::printf("{\"passed\":true,\"scenario\":%u,\"service_calls\":%u,\"destination_bytes\":80}\n",
        scenario, test_cfg_calls[0] + test_cfg_calls[1] + test_cfg_calls[2] + test_cfg_calls[3]);
}
#endif
