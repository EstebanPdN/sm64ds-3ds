#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace sm64ds::render::detail {
constexpr std::array<std::uint32_t,256> crc_table() noexcept {
    std::array<std::uint32_t,256> table{};
    for(unsigned i=0;i<256;++i){
        std::uint32_t v=i;
        for(unsigned bit=0;bit<8;++bit)v=(v>>1)^((v&1)?0xedb88320u:0u);
        table[i]=v;
    }
    return table;
}
inline constexpr auto bank_crc_table=crc_table();
// Caller supplies a valid byte span. Exclusion can extend beyond a short
// span; this also permits checksumming truncated inputs in diagnostics/tests.
inline std::uint32_t bank_crc32(const std::uint8_t* data,std::size_t bytes,std::size_t zero_at) noexcept {
    std::uint32_t crc=0xffffffffu;
    for(std::size_t i=0;i<bytes;++i){
        const std::uint8_t b=(i>=zero_at&&i-zero_at<4)?0:data[i];
        crc=bank_crc_table[(crc^b)&255]^(crc>>8);
    }
    return ~crc;
}
}
