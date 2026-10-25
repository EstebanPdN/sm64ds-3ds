#include "sm64ds/game_files.hpp"
#include <cassert>
#include <cstring>
#include <fstream>
#include <string>
#include <unistd.h>

static void put16(std::vector<uint8_t>& bytes, size_t at, uint16_t value) {
    bytes[at] = uint8_t(value); bytes[at+1] = uint8_t(value>>8);
}
static void put32(std::vector<uint8_t>& bytes, size_t at, uint32_t value) {
    for (int i=0; i<4; ++i) bytes[at+i] = uint8_t(value>>(8*i));
}

int main(int argc, char** argv) {
    char directory[] = "/tmp/sm64ds-files-XXXXXX"; assert(mkdtemp(directory));
    const std::string base = directory, rom = base+"/rom.nds", pak = base+"/assets.pak";
    std::vector<uint8_t> bytes(2048), compressed{'L','Z','7','7',0x10,8,0,0,0x20,'A','B',0x30,1};
    const std::vector<std::string> names{"raw.bin", "lz.bin", "bad.bin", "big.bin"};
    auto bad = compressed; bad.back() = 10; // Backreference before start of output.
    auto big = compressed; big[7] = 0xff;  // Allocation must be rejected before decoding.
    const std::vector<std::vector<uint8_t>> files{{'R','A','W'}, compressed, bad, big};
    std::memcpy(bytes.data(), "S.MARIO64DS", 10); std::memcpy(bytes.data()+12, "ASME", 4);
    put32(bytes, 0x40, 0x200); put32(bytes, 0x48, 0x280); put32(bytes, 0x4c, uint32_t(files.size()*8));
    put32(bytes, 0x200, 8); put16(bytes, 0x206, 1);
    size_t name_at=0x208, data_at=0x300;
    for (size_t i=0; i<files.size(); ++i) {
        bytes[name_at++] = uint8_t(names[i].size());
        std::memcpy(bytes.data()+name_at, names[i].data(), names[i].size()); name_at += names[i].size();
        put32(bytes, 0x280+i*8, uint32_t(data_at));
        std::memcpy(bytes.data()+data_at, files[i].data(), files[i].size()); data_at += files[i].size();
        put32(bytes, 0x284+i*8, uint32_t(data_at));
    }
    put32(bytes, 0x44, uint32_t(name_at+1-0x200));
    uint16_t checksum=0xffff;
    for (size_t i=0; i<0x15e; ++i) {
        checksum ^= bytes[i];
        for (int bit=0; bit<8; ++bit) checksum=uint16_t((checksum>>1)^((checksum&1)?0xa001:0));
    }
    put16(bytes, 0x15e, checksum);
    { std::ofstream file(rom, std::ios::binary); file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()); assert(file.good()); }
    std::string error;
    assert(sm64ds::import_rom(rom, pak, error));
    {
        sm64ds::AssetPack pack; assert(pack.open(pak, error)); sm64ds::GameFiles game(pack, 1024);
        assert(!game.initialize_common_archive(error)); // Synthetic ROM has no accepted profile.
        std::vector<uint8_t> output;
        assert(game.read_fat(0, output, error) && output == files[0]);
        assert(game.read_fat(1, output, error) && std::string(output.begin(), output.end()) == "ABABABAB");
        assert(game.bind_source_file_id(42, "lz.bin", error));
        assert(game.bind_source_file_id(42, "lz.bin", error));
        assert(!game.bind_source_file_id(42, "raw.bin", error));
        assert(!game.bind_source_file_id(43, "missing.bin", error));
        assert(!game.bind_source_file_id(0x8000, "lz.bin", error));
        assert(game.read_source(42, output, error)); const auto before=output;
        // Source-region IDs must never silently fall back to numerically equal US IDs.
        assert(!game.read_source(1, output, error) && output == before);
        assert(!game.read_fat(2, output, error) && output == before);
        assert(!game.read_fat(3, output, error) && output == before);
        assert(!game.read_fat(0x8000, output, error) && output == before);
        assert(!game.read_fat(100, output, error) && output == before);
        assert(game.read(1, sm64ds::FileIdDomain::CurrentRomFat, output, error));
        assert(!game.read(1, sm64ds::FileIdDomain::ExplicitSourceBindings, output, error));
        assert(!game.last_error().empty());
    }
    if (argc == 2) {
        sm64ds::AssetPack pack; assert(pack.open(argv[1], error)); sm64ds::GameFiles game(pack);
        const std::string path = "data/stage/main_castle/main_castle_all.bmd";
        const int index = pack.find(path); assert(index >= 0);
        std::vector<uint8_t> by_path, by_fat, by_source;
        assert(game.read_path(path, by_path, error) && by_path.size() > 1024);
        assert(game.read_fat(uint16_t(pack.info().files[size_t(index)].id), by_fat, error));
        assert(game.bind_source_file_id(17, path, error));
        assert(game.read_source(17, by_source, error));
        assert(by_path == by_fat && by_fat == by_source);
        for (const auto& binding : sm64ds::eu_fs_baseline_bindings)
            assert(game.bind_source_file_id(binding.source_id, binding.path, error));
        // Expectations are from upstream's independent EU-ROM smoke fixtures;
        // the same paths in the supplied US ROM produce the identical payload.
        assert(game.read_source(1626, by_source, error) && by_source.size() == 35600);
        uint32_t sum=0; for (auto byte : by_source) sum += byte;
        assert(sum == 0x2128ef);
        const uint8_t first8[]{4,0,0,0,1,0,0,0};
        assert(std::memcmp(by_source.data(), first8, sizeof(first8)) == 0);
        assert(game.read_source(1628, by_source, error) && by_source.size() == 2048);
        sum=0; for (auto byte : by_source) sum += byte;
        assert(sum == 0x48000);
    }
    unlink(rom.c_str()); unlink(pak.c_str()); rmdir(base.c_str());
}
