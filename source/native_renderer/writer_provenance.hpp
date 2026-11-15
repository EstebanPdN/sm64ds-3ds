#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace sm64ds::render {
// Diagnostic provenance only. No command/data words, resource pointers, stack
// unwinding or graphics interpretation are stored. Caller is a code address.
struct WriterOrigin {
    std::uintptr_t caller=0;
    std::uint32_t target=0,kind=0,width=0;
    std::uint64_t calls=0,units=0;
};
class WriterProvenance {
public:
    static constexpr unsigned capacity=512;
    void reset() noexcept;
    void record(std::uintptr_t caller,std::uint32_t kind,std::uint32_t target,
                std::uint32_t width,std::uint32_t units) noexcept;
    const WriterOrigin* entries() const noexcept {return entries_.data();}
    std::size_t size() const noexcept {return used_;}
    std::uint64_t dropped() const noexcept {return dropped_;}
private:
    std::array<WriterOrigin,capacity> entries_{};
    std::array<std::uint16_t,capacity*2> index_{};
    std::size_t used_=0;
    std::uint64_t dropped_=0;
};
}
