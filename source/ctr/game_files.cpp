#include "sm64ds/game_files.hpp"
#include "ntr/lz77.h"
#include <algorithm>
#include <cstring>

namespace sm64ds {
namespace {
GameFiles* native_files = nullptr;
FileIdDomain native_domain = FileIdDomain::ExplicitSourceBindings;
auto find_id(const std::vector<std::pair<uint16_t, size_t>>& table, uint16_t id) {
    return std::lower_bound(table.begin(), table.end(), id,
        [](const auto& entry, uint16_t value) { return entry.first < value; });
}
}

GameFiles::GameFiles(AssetPack& pack, uint32_t maximum_decoded_size)
    : pack_(pack), maximum_(maximum_decoded_size) {
    for (size_t i = 0; i < pack.info().files.size(); ++i) {
        const auto id = pack.info().files[i].id;
        if (id < 0x8000) fat_.emplace_back(static_cast<uint16_t>(id), i);
    }
    std::sort(fat_.begin(), fat_.end());
}

bool GameFiles::bind_source_file_id(uint16_t source_id, const std::string& path,
                                    std::string& error) {
    if (source_id >= 0x8000) { error = "NARC interior IDs require a verified archive binding"; return false; }
    const auto index = pack_.find(path);
    if (index < 0) { error = "Game asset path not present: " + path; return false; }
    const auto found = find_id(bindings_, source_id);
    if (found != bindings_.end() && found->first == source_id) {
        if (found->second != static_cast<size_t>(index)) { error = "Conflicting source file ID binding"; return false; }
        error.clear(); return true;
    }
    bindings_.insert(found, {source_id, static_cast<size_t>(index)});
    error.clear(); return true;
}

bool GameFiles::read_index(size_t index, std::vector<uint8_t>& output, std::string& error) {
    std::vector<uint8_t> packed;
    if (!pack_.read(index, packed, error)) return false;
    return decode_payload(packed, output, error);
}

bool GameFiles::decode_payload(const std::vector<uint8_t>& packed, std::vector<uint8_t>& output,
                               std::string& error) {
    std::vector<uint8_t> decoded;
    if (packed.size() >= 4 && std::memcmp(packed.data(), "LZ77", 4) == 0) {
        if (packed.size() < 8 || packed[4] != 0x10) { error = "Invalid game LZ77 header"; return false; }
        const uint32_t size = uint32_t(packed[5]) | uint32_t(packed[6])<<8 | uint32_t(packed[7])<<16;
        if (!size || size > maximum_) { error = "Decoded game file exceeds memory limit or is empty"; return false; }
        decoded.resize(size);
        if (ntr::lz77_unpack(packed.data(), static_cast<int32_t>(packed.size()), decoded.data(),
                            static_cast<int32_t>(decoded.size())) != static_cast<int32_t>(size)) {
            error = "Truncated or invalid game LZ77 stream"; return false;
        }
    } else {
        if (packed.empty() || packed.size() > maximum_) { error = "Game file exceeds memory limit or is empty"; return false; }
        decoded = packed;
    }
    output = std::move(decoded); error.clear(); return true;
}

bool GameFiles::initialize_common_archive(std::string& error) {
    const auto& info=pack_.info();
    if(info.revision!=0 || !((info.code=="ASME" && info.crc==0xe6321562) ||
                             (info.code=="ASMP" && info.crc==0x29715dec))) {
        error="Common archive requires a verified ASME0 or ASMP0 ROM";return false;
    }
    const int index=pack_.find("ARCHIVE/arc0.narc");
    if(index<0) { error="Original Player archive is missing";return false; }
    std::vector<uint8_t> bytes;
    std::vector<NarcMember> members;
    if(!pack_.read(size_t(index),bytes,error)) return false;
    if(bytes.size()!=321224 || crc32(bytes.data(),bytes.size())!=0xe96c144b) {
        error="Original Player archive differs from the verified regional payload";return false;
    }
    if(!parse_narc(bytes,members,error)) return false;
    if(members.size()!=198) { error="Unexpected Player archive member count";return false; }
    common_archive_=std::move(bytes); common_members_=std::move(members);
    error.clear();return true;
}

bool GameFiles::read_archive(uint16_t id, std::vector<uint8_t>& output, std::string& error) {
    for(const auto& binding:archives_) if(id>=binding.base && id<binding.end) {
        if(binding.base==0x8000 && binding.end==0x80c6)break;
        if(cached_archive_!=binding.index) {
            std::vector<uint8_t> bytes;std::vector<NarcMember> members;
            if(!pack_.read(binding.index,bytes,error) || !parse_narc(bytes,members,error))return false;
            if(members.size()!=size_t(binding.end-binding.base)) {
                error="ROM archive range differs from its NARC member count";return false;
            }
            archive_bytes_=std::move(bytes);archive_members_=std::move(members);
            cached_archive_=binding.index;
        }
        const auto member=archive_members_[id-binding.base];
        const std::vector<uint8_t> bytes(archive_bytes_.begin()+member.offset,
                                       archive_bytes_.begin()+member.offset+member.size);
        return decode_payload(bytes,output,error);
    }
    if(id<0x8000 || size_t(id-0x8000)>=common_members_.size()) {
        error="Archive ID has no verified member binding";return false;
    }
    const auto member=common_members_[id-0x8000];
    const std::vector<uint8_t> bytes(common_archive_.begin()+member.offset,
                                    common_archive_.begin()+member.offset+member.size);
    return decode_payload(bytes,output,error);
}

bool GameFiles::bind_archive_range(uint16_t base,uint16_t end,const std::string& path,std::string& error) {
    if(base<0x8000 || end<=base) {error="Invalid original NARC ID range";return false;}
    const auto index=pack_.find(path);
    if(index<0) {error="Original archive path missing: "+path;return false;}
    for(const auto& other:archives_) {
        if(base==other.base && end==other.end && size_t(index)==other.index) {error.clear();return true;}
        if(base<other.end && other.base<end) {error="Overlapping original NARC ID ranges";return false;}
    }
    archives_.push_back({base,end,size_t(index)});error.clear();return true;
}

bool GameFiles::read_fat(uint16_t id, std::vector<uint8_t>& output, std::string& error) {
    if (id >= 0x8000) { error = "NARC interior ID is not a ROM FAT ID"; return false; }
    const auto found = find_id(fat_, id);
    if (found == fat_.end() || found->first != id) { error = "ROM FAT ID not present"; return false; }
    return read_index(found->second, output, error);
}

bool GameFiles::read_source(uint16_t id, std::vector<uint8_t>& output, std::string& error) {
    if(id>=0x8000) return read_archive(id,output,error);
    const auto found = find_id(bindings_, id);
    if (found == bindings_.end() || found->first != id) { error = "Source file ID has no verified path binding"; return false; }
    return read_index(found->second, output, error);
}

bool GameFiles::read_path(const std::string& path, std::vector<uint8_t>& output, std::string& error) {
    const auto index = pack_.find(path);
    if (index < 0) { error = "Game asset path not present: " + path; return false; }
    return read_index(static_cast<size_t>(index), output, error);
}

bool GameFiles::read(uint16_t id, FileIdDomain domain, std::vector<uint8_t>& output, std::string& error) {
    const bool ok = id>=0x8000 ? read_archive(id,output,error) :
        domain == FileIdDomain::CurrentRomFat ? read_fat(id, output, error) : read_source(id, output, error);
    last_error_ = error; return ok;
}

void bind_native_game_files(GameFiles* files, FileIdDomain domain) {
    native_files = files; native_domain = domain;
}
GameFiles* bound_native_game_files() { return native_files; }
} // namespace sm64ds

#ifdef __3DS__
extern "C" bool sm64ds_game_file_span(uint16_t file_id,const void** output,uint32_t* size) {
    auto* files = sm64ds::native_files;
    if (!files || !output || !size) return false;
    static std::vector<uint8_t> bytes;
    std::string error;
    if (!files->read(file_id, sm64ds::native_domain, bytes, error)) return false;
    *output=bytes.data();*size=static_cast<uint32_t>(bytes.size());return true;
}
extern "C" void sm64ds_game_file_allocation_failed() {
    if(sm64ds::native_files) sm64ds::native_files->record_error("Native game heap allocation failed");
}
#endif
