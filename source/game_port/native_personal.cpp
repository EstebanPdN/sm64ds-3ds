// Native provider for the SDK personal-data export at original 02059eb0.
// CTR CFGU supplies real settings once; no DS firmware RAM is mapped/read.
#include "sm64ds/native_personal.hpp"
#include "sm64ds/native_platform.hpp"

#include <array>
#include <cstring>

extern "C" {
#include <3ds/services/cfgu.h>
#include <3ds/result.h>
[[noreturn]] void sm64ds_native_missing(const char* operation);
}

namespace {
sm64ds::NativePersonalStats stats;
std::array<std::uint8_t, sm64ds::kNativePersonalBytes> personal{};

bool error(const char* reason) {
    ++stats.failures;
    stats.last_error = reason;
    sm64ds::native_platform_fail(reason);
    return false;
}

bool error(const char* reason, std::int32_t result) {
    stats.service_result = result;
    return error(reason);
}

std::uint16_t halfword(const std::uint8_t* bytes) {
    return std::uint16_t(bytes[0] | (std::uint16_t(bytes[1]) << 8));
}

bool valid_name(const std::uint8_t* name, unsigned length) {
    for (unsigned index = 0; index < length; ++index) {
        const auto unit = halfword(name + index * 2);
        if (unit >= 0xd800 && unit <= 0xdbff) {
            if (++index >= length) return false;
            const auto next = halfword(name + index * 2);
            if (next < 0xdc00 || next > 0xdfff) return false;
        } else if (unit >= 0xdc00 && unit <= 0xdfff) {
            return false;
        }
    }
    return true;
}

bool configure(std::uint8_t language, const std::uint8_t* name,
               const std::uint8_t* birthday) {
    if (language > CFG_LANGUAGE_TW)
        return error("native personal: invalid CFGU system language");
    static constexpr unsigned days[] = {31,29,31,30,31,30,31,31,30,31,30,31};
    if (birthday[0] < 1 || birthday[0] > 12 || birthday[1] < 1 ||
        birthday[1] > days[birthday[0] - 1])
        return error("native personal: invalid CFGU birthday");
    unsigned length = 0;
    while (length < 10 && halfword(name + length * 2)) ++length;
    if (!valid_name(name, length))
        return error("native personal: invalid CFGU nickname UTF-16");

    // EU game resources only provide these five languages. JP also maps to
    // English in the original menu switch; other CTR IDs must not be truncated
    // to the old three-bit firmware field and accidentally select another one.
    stats.english_fallback = language < CFG_LANGUAGE_EN || language > CFG_LANGUAGE_ES;
    personal[0] = stats.english_fallback ? std::uint8_t(CFG_LANGUAGE_EN) : language;
    personal[1] = 0;  // explicit DS colour compatibility default: grey
    personal[2] = birthday[0];
    personal[3] = birthday[1];
    std::memcpy(personal.data() + 4, name, 20);
    personal[0x18] = std::uint8_t(length);
    personal[0x19] = 0;
    // The remaining 54 zero bytes mean empty DS comment and length=0.
    stats.ready = true;
    return true;
}
} // namespace

namespace sm64ds {
bool native_personal_initialize() {
    if (stats.initialization_attempted) return stats.ready;
    stats.initialization_attempted = true;
    auto result = cfguInit();
    if (R_FAILED(result)) return error("native personal: cfguInit failed", result);
    // Name block is 0x1c bytes: only its first 10 UTF-16 units belong to the
    // NTR export. Its extra unit, NGWord flag and version are never copied.
    alignas(4) std::uint8_t name[0x1c]{};
    alignas(4) std::uint8_t birthday[2]{};
    std::uint8_t language = 0xff;
    const char* operation = "native personal: CFGU language read failed";
    result = CFGU_GetSystemLanguage(&language);
    if (R_SUCCEEDED(result)) {
        operation = "native personal: CFGU nickname read failed";
        result = CFGU_GetConfigInfoBlk2(sizeof(name), 0x000a0000, name);
    }
    if (R_SUCCEEDED(result)) {
        operation = "native personal: CFGU birthday read failed";
        result = CFGU_GetConfigInfoBlk2(sizeof(birthday), 0x000a0001, birthday);
    }
    cfguExit();
    if (R_FAILED(result)) return error(operation, result);
    stats.service_result = result;
    return configure(language, name, birthday);
}

NativePersonalStats native_personal_stats() { return stats; }

bool native_personal_copy(void* destination, std::size_t capacity) {
    if (!stats.ready) return error("native personal: profile was not initialized");
    const auto address = reinterpret_cast<std::uintptr_t>(destination);
    if (!address || (address & 1) || capacity < kNativePersonalBytes ||
        address > UINTPTR_MAX - kNativePersonalBytes ||
        (address >= 0x02000000 && address < 0x08000000))
        return error("native personal: invalid original profile destination");
    std::memcpy(destination, personal.data(), personal.size());
    ++stats.copies;
    return true;
}
} // namespace sm64ds

extern "C" void func_02059eb0(unsigned char* destination) {
    if (!sm64ds::native_personal_copy(destination, sm64ds::kNativePersonalBytes))
        sm64ds_native_missing("native personal: original SDK profile export failed");
}
