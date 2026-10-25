#pragma once
#include "sm64ds/assets.hpp"
#include <array>

namespace sm64ds::game_data {
// DS addresses remain integers. No address from a ROM is executed or cast to a
// native pointer; callers must obtain a checked offset into an owning image.
struct OverlayImage {
    uint32_t id=0, base=0, bss_size=0;
    std::vector<uint8_t> bytes;
    bool range(uint32_t address, size_t length, size_t& offset) const;
};
struct HandleTable {
    uint32_t original_address=0;
    std::vector<size_t> asset_indices;
};
struct ObjectGroup {
    uint8_t type=0, scenario=0, count=0;
    int area=-1;
    uint32_t original_address=0;
};
struct Entrance {
    uint16_t object=0, parameter=0;
    std::array<int16_t,3> position{}, rotation{};
    int area=-1;
    uint8_t scenario=0;
};
struct LevelData {
    OverlayImage image;
    uint32_t original_address=0, clps_address=0;
    std::array<uint16_t,4> handles{}; // model, collision, minimap chars/palette
    std::vector<ObjectGroup> groups;
    std::vector<Entrance> entrances;
};

// BLZ input is only decoded when the cartridge's overlay flag says compressed.
// expected_size comes from that cartridge's overlay table, not a region map.
bool decode_blz(const std::vector<uint8_t>& packed, uint32_t expected_size,
                std::vector<uint8_t>& output, std::string& error);
bool load_overlay(AssetPack& pack, uint32_t id, OverlayImage& result,
                  std::string& error);
bool load_arm9(AssetPack& pack, OverlayImage& result, std::string& error);
bool load_handles(AssetPack& pack, HandleTable& result, std::string& error);
// Locate by actual model path and structural validation. Ambiguous matches fail.
bool load_level(AssetPack& pack, uint32_t overlay, const HandleTable& handles,
                const std::string& model_path, LevelData& result,
                std::string& error);
// Separate TU defines the two verified, pointer-free native game globals.
// Call before any original game code; presently accepts the verified ASME0 ROM.
bool initialize_native_constants(AssetPack& pack, std::string& error);

enum class SymbolKind { data, function };
struct NativeSymbol {
    uintptr_t address=0;
    size_t size=0; // required for data; function relocations cannot have addends
    SymbolKind kind=SymbolKind::data;
};
struct Relocation {
    size_t offset=0;
    uint32_t original_pointer=0;
    const char* module=nullptr;
    const char* symbol=nullptr;
    size_t addend=0;
    SymbolKind kind=SymbolKind::data;
};
// A resolver must identify BOTH module and symbol. It must return an address of
// a compiled function or owned native allocation, never a cartridge address.
using NativeResolver=std::function<bool(const char* module, const char* symbol,
                                        NativeSymbol& result)>;
// Explicit relocations only, with exact expected source words. All entries must
// resolve before output is published. Host probes may use synthetic 32-bit
// addresses; ARM11 callers provide real addresses from their linker/allocations.
bool relocate_block(const std::vector<uint8_t>& source,
                    const std::vector<Relocation>& relocations,
                    const NativeResolver& resolver,
                    std::vector<uint8_t>& output, std::string& error);
// Loads the six original title callbacks and two initial allocation callbacks.
// The six u32 fields consumed by func_ov007_020b7138 are not a GCC/C++ vtable.
bool initialize_menu_callbacks(AssetPack& pack, const NativeResolver& resolver,
                               std::string& error);
} // namespace sm64ds::game_data
