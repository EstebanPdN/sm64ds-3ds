#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace sm64ds {
struct NarcMember { uint32_t offset, size; };
// Validated byte ranges in the owning NARC image; no pointers or ROM addresses.
// Output is unchanged on failure. The name table is not interpreted by this API.
bool parse_narc(const std::vector<uint8_t>& image, std::vector<NarcMember>& members,
                std::string& error);
}
