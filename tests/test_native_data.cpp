#include "sm64ds/native_data.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <limits>
using namespace sm64ds;
int main() {
    using Result=NativeDataResult;
    const uint8_t source[]={0x78,0x56,0x34,0x12,0xfe,0xed,0xfa,0xce};
    std::array<uint8_t,8> first,second;
    first.fill(0xa5);second.fill(0xa5);
    const auto unchanged=first;
    NativeDataSection sections[]={{first.data(),source,8,0},{second.data(),nullptr,8,1}};
    NativeDataFixup fixups[]={{0,0,0x12345678,0x100004},{1,4,0,0x200008}};
    auto rejected=[&](Result expected) {
        assert(sm64ds_restore_native_data(sections,2,fixups,2)==expected);
        assert(first==unchanged && second==unchanged);
    };
    fixups[1].expected=1;rejected(Result::unexpected_source_word);fixups[1].expected=0;
    fixups[1].offset=8;rejected(Result::invalid_fixup);fixups[1].offset=4;
    fixups[1].offset=1;rejected(Result::invalid_fixup);fixups[1].offset=4;
    fixups[1].section=2;rejected(Result::invalid_fixup);fixups[1].section=1;
    fixups[1].replacement=0;rejected(Result::invalid_fixup);fixups[1].replacement=0x200008;
    fixups[1]={0,0,0x12345678,0x200008};rejected(Result::invalid_fixup);
    fixups[1]={1,4,0,0x200008};
    std::swap(fixups[0],fixups[1]);rejected(Result::invalid_fixup);std::swap(fixups[0],fixups[1]);
    sections[1].zero_fill=2;rejected(Result::invalid_section);sections[1].zero_fill=1;
    sections[1].source=source;rejected(Result::invalid_section);sections[1].source=nullptr;
    sections[1].destination=first.data()+4;rejected(Result::overlapping_storage);sections[1].destination=second.data();
    sections[0].source=second.data();rejected(Result::overlapping_storage);sections[0].source=source;
    sections[1].destination=reinterpret_cast<uint8_t*>(sections);rejected(Result::overlapping_storage);
    sections[1].destination=reinterpret_cast<uint8_t*>(fixups);rejected(Result::overlapping_storage);
    sections[1].destination=reinterpret_cast<uint8_t*>(std::numeric_limits<uintptr_t>::max()-3);
    rejected(Result::invalid_section);sections[1].destination=second.data();
    sections[1].size=0;rejected(Result::invalid_section);sections[1].size=8;
    assert(sm64ds_restore_native_data(nullptr,1,nullptr,0)==Result::invalid_table);
    assert(sm64ds_restore_native_data(sections,0,nullptr,0)==Result::invalid_table);
    assert(sm64ds_restore_native_data(sections,2,nullptr,2)==Result::invalid_table);
    assert(sm64ds_restore_native_data(sections,2,fixups,2)==Result::success);
    const std::array<uint8_t,8> expected_first={4,0,0x10,0,0xfe,0xed,0xfa,0xce};
    const std::array<uint8_t,8> expected_second={0,0,0,0,8,0,0x20,0};
    assert(first==expected_first && second==expected_second);
    // A second boot restores ROM/BSS state before applying the same fixups.
    first.fill(0xff);second.fill(0xff);
    assert(sm64ds_restore_native_data(sections,2,fixups,2)==Result::success);
    assert(first==expected_first && second==expected_second);
    // Empty relocation table is valid for pointer-free constants.
    assert(sm64ds_restore_native_data(sections,2,nullptr,0)==Result::success);
    assert(std::memcmp(first.data(),source,8)==0);
    for(auto byte:second) assert(byte==0);
}
