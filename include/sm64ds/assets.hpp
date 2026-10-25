#pragma once
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace sm64ds {
struct AssetEntry {
    uint32_t id = 0, offset = 0, size = 0, crc = 0;
    std::string path;
};
struct RomInfo {
    std::string code;
    uint8_t revision = 0;
    uint32_t size = 0, crc = 0;
    std::vector<AssetEntry> files;
};
// Returning false cancels before publishing. An existing pack is preserved.
using Progress = std::function<bool(uint32_t done, uint32_t total)>;
uint32_t crc32(const void* data, size_t size, uint32_t previous = 0);
bool inspect_rom(const std::string& path, RomInfo& info, std::string& error);
bool import_rom(const std::string& rom, const std::string& pack,
                std::string& error, const Progress& progress = {});

class AssetPack {
public:
    ~AssetPack();
    AssetPack() = default;
    AssetPack(const AssetPack&) = delete;
    AssetPack& operator=(const AssetPack&) = delete;
    bool open(const std::string& path, std::string& error);
    bool read(size_t index, std::vector<uint8_t>& data, std::string& error);
    bool verify(std::string& error);
    int find(const std::string& path) const;
    const RomInfo& info() const { return info_; }
private:
    FILE* file_ = nullptr;
    RomInfo info_;
};
}
