#pragma once
#include "sm64ds/assets.hpp"
#include "sm64ds/narc.hpp"
#include <cstddef>
#include <cstdint>
#include <utility>

namespace sm64ds {
enum class FileIdDomain { ExplicitSourceBindings, CurrentRomFat };
struct VerifiedSourceFileBinding { uint16_t source_id; const char* path; };
// Exact EU FAT IDs asserted by upstream port/tests/smoke_fs.cpp at the pinned
// commit. This is a two-file verified subset, not a complete EU catalog.
inline constexpr VerifiedSourceFileBinding eu_fs_baseline_bindings[] = {
    {1626, "MG/casino_back.bmd"},
    {1628, "MG/d_2d_blue_shadow_nsc.bin"},
};

// SharedFilePtr stores a resolved FAT ID, not an overlay-0 logical handle.
// Bind source-region FAT IDs by verified path when those IDs differ from the ROM.
class GameFiles {
public:
    explicit GameFiles(AssetPack& pack, uint32_t maximum_decoded_size = 16*1024*1024);
    bool bind_source_file_id(uint16_t source_id, const std::string& path, std::string& error);
    // The 198 members of arc0 are byte-identical in the verified ASME0/ASMP0
    // images, including the Player models/animations. Other archive ID ranges
    // differ between regions and are deliberately not guessed.
    bool initialize_common_archive(std::string& error);
    bool bind_archive_range(uint16_t base,uint16_t end,const std::string& path,std::string& error);
    bool read_archive(uint16_t id, std::vector<uint8_t>& output, std::string& error);
    bool read_fat(uint16_t current_rom_id, std::vector<uint8_t>& output, std::string& error);
    bool read_source(uint16_t source_id, std::vector<uint8_t>& output, std::string& error);
    bool read_path(const std::string& path, std::vector<uint8_t>& output, std::string& error);
    bool read(uint16_t id, FileIdDomain domain, std::vector<uint8_t>& output, std::string& error);
    const std::string& last_error() const { return last_error_; }
    void record_error(std::string error) { last_error_ = std::move(error); }
    // Raw NitroFS services must not use read_path(): the original game decides
    // whether/when to decompress FS_ReadFile payloads itself.
    AssetPack& asset_pack() { return pack_; }
private:
    bool read_index(size_t index, std::vector<uint8_t>& output, std::string& error);
    bool decode_payload(const std::vector<uint8_t>& packed, std::vector<uint8_t>& output,
                        std::string& error);
    AssetPack& pack_;
    uint32_t maximum_;
    std::vector<std::pair<uint16_t, size_t>> fat_, bindings_;
    std::vector<uint8_t> common_archive_;
    std::vector<NarcMember> common_members_;
    struct ArchiveBinding {uint16_t base,end;size_t index;};
    std::vector<ArchiveBinding> archives_;
    size_t cached_archive_=size_t(-1);
    std::vector<uint8_t> archive_bytes_;
    std::vector<NarcMember> archive_members_;
    std::string last_error_;
};

// Install once before running original game code; the service must outlive it.
// Explicit bindings are the safe default. Selecting CurrentRomFat is a promise
// that the upstream handle table has already been translated to this ROM.
void bind_native_game_files(GameFiles* files,
                           FileIdDomain domain = FileIdDomain::ExplicitSourceBindings);
GameFiles* bound_native_game_files();
}
