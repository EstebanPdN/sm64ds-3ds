#include "sm64ds/native_data.hpp"
#include <cstddef>
#include <cstring>
#include <limits>

namespace sm64ds {
namespace {
bool range(const void* pointer, std::size_t bytes) {
    const auto start=reinterpret_cast<std::uintptr_t>(pointer);
    return pointer && bytes && bytes<=std::numeric_limits<std::uintptr_t>::max()-start;
}
bool table_size(std::size_t count, std::size_t element_size) {
    return count<=SIZE_MAX/element_size;
}
bool overlaps(const void* a, std::size_t na, const void* b, std::size_t nb) {
    if(!na || !nb) return false;
    const auto aa=reinterpret_cast<std::uintptr_t>(a),bb=reinterpret_cast<std::uintptr_t>(b);
    return aa<bb+nb && bb<aa+na; // inputs have already passed range()
}
uint32_t word(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1])<<8 | uint32_t(p[2])<<16 | uint32_t(p[3])<<24;
}
}

extern "C" NativeDataResult sm64ds_restore_native_data(
    const NativeDataSection* sections, uint32_t section_count,
    const NativeDataFixup* fixups, uint32_t fixup_count) {
    using Result=NativeDataResult;
    if(!section_count || !table_size(section_count,sizeof(*sections)) ||
       !table_size(fixup_count,sizeof(*fixups)) ||
       !range(sections,std::size_t(section_count)*sizeof(*sections)) ||
       (reinterpret_cast<std::uintptr_t>(sections)%alignof(NativeDataSection)) ||
       (fixup_count && (!range(fixups,std::size_t(fixup_count)*sizeof(*fixups)) ||
        reinterpret_cast<std::uintptr_t>(fixups)%alignof(NativeDataFixup)))) return Result::invalid_table;
    const auto section_bytes=std::size_t(section_count)*sizeof(*sections);
    const auto fixup_bytes=std::size_t(fixup_count)*sizeof(*fixups);
    // Validate every range before pairwise overlap checks, including later
    // sections. Reject aliases that could change a future source or descriptor.
    for(uint32_t i=0;i<section_count;++i) {
        const auto& s=sections[i];
        if(!range(s.destination,s.size) || s.zero_fill>1 ||
           (s.zero_fill ? s.source!=nullptr : !range(s.source,s.size))) return Result::invalid_section;
    }
    for(uint32_t i=0;i<section_count;++i) {
        const auto& s=sections[i];
        if(overlaps(s.destination,s.size,sections,section_bytes) ||
           overlaps(s.destination,s.size,fixups,fixup_bytes)) return Result::overlapping_storage;
        for(uint32_t j=0;j<section_count;++j) {
            const auto& other=sections[j];
            if((i!=j && overlaps(s.destination,s.size,other.destination,other.size)) ||
               (!other.zero_fill && overlaps(s.destination,s.size,other.source,other.size)))
                return Result::overlapping_storage;
        }
    }
    for(uint32_t i=0;i<fixup_count;++i) {
        const auto& f=fixups[i];
        if(f.section>=section_count || !f.replacement) return Result::invalid_fixup;
        const auto& s=sections[f.section];
        if(s.size<4 || f.offset>s.size-4 || (f.offset&3u)) return Result::invalid_fixup;
        if(i && (f.section<fixups[i-1].section || (f.section==fixups[i-1].section &&
                 f.offset<=fixups[i-1].offset))) return Result::invalid_fixup;
        const auto original=s.zero_fill ? 0u : word(s.source+f.offset);
        if(original!=f.expected) return Result::unexpected_source_word;
    }
    for(uint32_t i=0;i<section_count;++i) {
        const auto& s=sections[i];
        if(s.zero_fill) std::memset(s.destination,0,s.size);
        else std::memcpy(s.destination,s.source,s.size);
    }
    for(uint32_t i=0;i<fixup_count;++i) {
        const auto& f=fixups[i];
        auto* destination=sections[f.section].destination+f.offset;
        for(unsigned byte=0;byte<4;++byte) destination[byte]=uint8_t(f.replacement>>(byte*8));
    }
    return Result::success;
}
}
