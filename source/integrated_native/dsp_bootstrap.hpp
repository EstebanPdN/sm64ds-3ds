// Automatic, memory-only DSP component preparation for the integrated port.
// Home Menu title IDs, ExeFS lowpaths and DSP1 section layout are based on
// zoogie/DSP1 commit 9b0180772d16dc688ad8b3527df294c11bb736a1 (MIT):
// https://github.com/zoogie/DSP1
// Copyright (C) 2017 zoogie
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// The bounded parser/BLZ/SHA implementation below is independent of DSP1's
// unchecked extraction loop. No firmware, game data or signature is embedded.
// Section SHA256 proves integrity; DSP_LoadComponent still authenticates the
// system's RSA signature. No NAND writes, process access or SD dump occurs.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include "boot_allocator.hpp"
#ifdef __3DS__
#include <3ds.h>
#endif

namespace dsp_bootstrap {
inline constexpr std::size_t MaxCode = 12u * 1024u * 1024u;
inline constexpr std::size_t MaxComponent = 512u * 1024u;
struct Result {
    bool ok = false, prepared = false;
    const char* source = "none";
    int32_t code = 0;
    char error[192]{};
};
struct Component {
    std::size_t offset = 0, size = 0;
    unsigned sections = 0;
};
inline bool failure(char* error, std::size_t cap, const char* message) {
    if (error && cap) std::snprintf(error, cap, "%s", message);
    return false;
}
inline uint32_t read32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
inline bool span(std::size_t total, std::size_t offset, std::size_t size) {
    return offset <= total && size <= total - offset;
}

// Portable SHA256 avoids depending on FS service hashing on older firmware.
class Sha256 {
    uint32_t state[8] = {0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
                         0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};
    uint8_t block[64]{};
    uint64_t bytes = 0;
    std::size_t used = 0;
    static uint32_t rr(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }
    void transform(const uint8_t* p) {
        static constexpr uint32_t k[64] = {
            0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
            0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
            0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
            0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
            0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
            0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
            0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
            0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};
        uint32_t w[64];
        for (unsigned i = 0; i < 16; ++i)
            w[i] = (uint32_t(p[i*4]) << 24) | (uint32_t(p[i*4+1]) << 16) |
                   (uint32_t(p[i*4+2]) << 8) | p[i*4+3];
        for (unsigned i = 16; i < 64; ++i) {
            const uint32_t a = w[i-15], b = w[i-2];
            w[i] = w[i-16] + (rr(a,7)^rr(a,18)^(a>>3)) + w[i-7] + (rr(b,17)^rr(b,19)^(b>>10));
        }
        uint32_t a=state[0],b=state[1],c=state[2],d=state[3],e=state[4],f=state[5],g=state[6],h=state[7];
        for (unsigned i=0; i<64; ++i) {
            const uint32_t t1=h+(rr(e,6)^rr(e,11)^rr(e,25))+((e&f)^(~e&g))+k[i]+w[i];
            const uint32_t t2=(rr(a,2)^rr(a,13)^rr(a,22))+((a&b)^(a&c)^(b&c));
            h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
        }
        state[0]+=a;state[1]+=b;state[2]+=c;state[3]+=d;
        state[4]+=e;state[5]+=f;state[6]+=g;state[7]+=h;
    }
public:
    void update(const uint8_t* p, std::size_t size) {
        bytes += size;
        while (size) {
            const std::size_t count = size < 64-used ? size : 64-used;
            std::memcpy(block+used,p,count);used+=count;p+=count;size-=count;
            if (used==64) { transform(block);used=0; }
        }
    }
    std::array<uint8_t,32> finish() {
        const uint64_t bits=bytes*8;
        block[used++]=0x80;
        if (used>56) { std::memset(block+used,0,64-used);transform(block);used=0; }
        std::memset(block+used,0,56-used);
        for (unsigned i=0;i<8;++i) block[63-i]=uint8_t(bits>>(i*8));
        transform(block);
        std::array<uint8_t,32> out{};
        for (unsigned i=0;i<32;++i) out[i]=uint8_t(state[i/4]>>(24-(i%4)*8));
        return out;
    }
};
inline std::array<uint8_t,32> sha256(const uint8_t* p, std::size_t size) {
    Sha256 hash;hash.update(p,size);return hash.finish();
}

inline bool validate_component(const uint8_t* p, std::size_t size,
                               Component& view, char* error=nullptr, std::size_t cap=0) {
    if (!p || size<0x300 || size>MaxComponent || std::memcmp(p+0x100,"DSP1",4) || read32(p+0x104)!=size)
        return failure(error,cap,"invalid DSP1 magic or component size");
    const unsigned sections=p[0x10e];
    if (!sections || sections>10 || (p[0x10f]&~3u))
        return failure(error,cap,"invalid DSP1 section count or flags");
    for (unsigned i=0x118;i<0x120;++i)
        if (p[i]) return failure(error,cap,"nonzero DSP1 reserved header bytes");
    if (p[0x10f]&2) {
        const unsigned type=p[0x10d];
        const uint32_t start=read32(p+0x110), length=read32(p+0x114), limit=type==0?0x20000u:0x10000u;
        if (type>2 || start>=limit || length>uint64_t(limit-start)*2)
            return failure(error,cap,"invalid DSP1 special segment span");
    }
    for (unsigned i=0;i<sections;++i) {
        const uint8_t* record=p+0x120+i*0x30;
        const uint32_t offset=read32(record), start=read32(record+4), length=read32(record+8);
        const unsigned type=record[15];
        const uint32_t limit=type==0?0x20000u:0x10000u;
        if (offset<0x300 || !length || !span(size,offset,length) || type>2 ||
            start>=limit || length>uint64_t(limit-start)*2)
            return failure(error,cap,"invalid DSP1 section source or destination span");
        for (unsigned j=0;j<i;++j) {
            const uint8_t* previous=p+0x120+j*0x30;
            const uint32_t a=read32(previous), n=read32(previous+8);
            if (offset<a+n && a<offset+length)
                return failure(error,cap,"overlapping DSP1 file sections");
        }
        const auto digest=sha256(p+offset,length);
        uint8_t difference=0;
        for (unsigned j=0;j<32;++j) difference|=digest[j]^record[0x10+j];
        if (difference) return failure(error,cap,"DSP1 section SHA256 mismatch");
    }
    view={0,size,sections};return true;
}

inline bool find_component(const uint8_t* p, std::size_t size, Component& result,
                           char* error=nullptr, std::size_t cap=0) {
    if (!p || size<0x300 || size>MaxCode) return failure(error,cap,"invalid Home Menu code size");
    bool found=false;
    for (std::size_t magic=0x100; magic+8<=size; ++magic) {
        if (std::memcmp(p+magic,"DSP1",4)) continue;
        const std::size_t base=magic-0x100, length=read32(p+magic+4);
        if (!span(size,base,length)) continue;
        Component candidate;
        if (!validate_component(p+base,length,candidate)) continue;
        if (found) return failure(error,cap,"multiple validated DSP1 components in Home Menu code");
        result={base,length,candidate.sections};found=true;
    }
    return found || failure(error,cap,"no component with valid DSP1 section hashes in Home Menu code");
}

struct BlzLayout { std::size_t stored=0, decoded=0, stop=0, input=0; };
inline bool blz_layout(const uint8_t* footer, std::size_t stored, BlzLayout& result,
                       char* error=nullptr, std::size_t cap=0) {
    if (!footer || stored<8 || stored>MaxCode) return failure(error,cap,"invalid BLZ stored size");
    const uint32_t fields=read32(footer), extra=read32(footer+4);
    const std::size_t header=fields>>24, compressed=fields&0xffffffu;
    if (header<8 || header>stored || compressed<=header || compressed>stored ||
        !extra || extra>MaxCode-stored)
        return failure(error,cap,"invalid BLZ footer or decompressed size");
    result={stored,stored+extra,stored-compressed,stored-header};return true;
}
inline bool decompress_blz_in_place(uint8_t* buffer, std::size_t capacity,
                                    const BlzLayout& layout,
                                    char* error=nullptr, std::size_t cap=0) {
    if (!buffer || layout.decoded>capacity || layout.decoded>MaxCode ||
        layout.stored<8 || layout.stored>layout.decoded || layout.input>layout.stored-8 ||
        layout.stop>=layout.input)
        return failure(error,cap,"invalid BLZ work buffer");
    BlzLayout checked;
    if (!blz_layout(buffer+layout.stored-8,layout.stored,checked) ||
        checked.decoded!=layout.decoded || checked.stop!=layout.stop || checked.input!=layout.input)
        return failure(error,cap,"BLZ layout does not match footer");
    for (std::size_t i=layout.input;i<layout.stored-8;++i)
        if (buffer[i]!=0xff) return failure(error,cap,"invalid BLZ header padding");
    std::size_t input=layout.input, output=layout.decoded;
    while (output>layout.stop) {
        if (input<=layout.stop) return failure(error,cap,"truncated BLZ control byte");
        const uint8_t control=buffer[--input];
        for (unsigned mask=0x80; mask && output>layout.stop; mask>>=1) {
            if (control&mask) {
                if (input-layout.stop<2) return failure(error,cap,"truncated BLZ back reference");
                input-=2;
                const unsigned pair=unsigned(buffer[input]) | (unsigned(buffer[input+1])<<8);
                const std::size_t length=(pair>>12)+3, distance=(pair&0xfff)+3;
                if (length>output-layout.stop || distance>layout.decoded-output)
                    return failure(error,cap,"invalid BLZ back reference span");
                for (std::size_t i=0;i<length;++i) {
                    --output;
                    if (output<input) return failure(error,cap,"BLZ output overlaps unread input");
                    buffer[output]=buffer[output+distance];
                }
            } else {
                if (input<=layout.stop) return failure(error,cap,"truncated BLZ literal");
                const uint8_t byte=buffer[--input];
                --output;
                if (output<input) return failure(error,cap,"BLZ literal overlaps unread input");
                buffer[output]=byte;
            }
        }
    }
    return input==layout.stop || failure(error,cap,"BLZ input remains after complete output");
}

inline uint64_t home_menu_title(unsigned region) {
    // CFG region values: JPN, USA, EUR, AUS (uses EUR title), CHN, KOR, TWN.
    static constexpr uint32_t low[]={0x8202,0x8f02,0x9802,0x9802,0xa102,0xa902,0xb102};
    return region<7 ? (uint64_t(0x00040030u)<<32)|low[region] : 0;
}
inline const char* home_menu_source(unsigned region) {
    static constexpr const char* names[]={"home-menu-jpn","home-menu-usa","home-menu-eur",
        "home-menu-eur-aus","home-menu-chn","home-menu-kor","home-menu-twn"};
    return region<7?names[region]:"home-menu-unknown";
}

namespace detail {
// Global operator new belongs to the original DS heap, which is not initialized
// yet. Both the temporary code image and retained component use libc directly.
inline boot_memory::Vector<uint8_t> retained;
inline const char* retained_source="none";
inline bool copy_component(const boot_memory::Vector<uint8_t>& image, const Component& view,
                           const char* source, Result& result) {
    if (!view.size || view.size>MaxComponent || !span(image.size(),view.offset,view.size)) {
        result.code=-1;return failure(result.error,sizeof result.error,"invalid retained DSP component span");
    }
    try {
        boot_memory::Vector<uint8_t> copy(image.begin()+view.offset,image.begin()+view.offset+view.size);
        retained.swap(copy);
    } catch (const std::bad_alloc&) {
        result.code=-1;return failure(result.error,sizeof result.error,"DSP component allocation failed");
    }
    retained_source=source;return true;
}
#ifdef __3DS__
inline bool register_component(const boot_memory::Vector<uint8_t>& image, const Component& view,
                               const char* source, Result& result) {
    if (!copy_component(image,view,source,result)) return false;
    ndspUseComponent(retained.data(),uint32_t(retained.size()),0xff,0xff);
    result.ok=result.prepared=true;result.source=source;return true;
}
struct FileHandle {
    Handle value=0;
    ~FileHandle() { if (value) FSFILE_Close(value); }
};
inline bool read_exact(Handle file, uint64_t offset, uint8_t* p, std::size_t size, Result& result) {
    while (size) {
        const uint32_t count=uint32_t(size>65536?65536:size);
        uint32_t read=0;
        const ::Result rc=FSFILE_Read(file,&read,offset,p,count);
        if (R_FAILED(rc) || read!=count) {
            result.code=R_FAILED(rc)?rc:-1;
            return failure(result.error,sizeof result.error,"cannot read complete Home Menu code");
        }
        offset+=count;p+=count;size-=count;
    }
    return true;
}
#endif
} // namespace detail

// Call on the main thread before the game's first ndspInit. It prepares bytes
// only: no DSP initialization, worker creation, hook, or preflight init/exit.
inline Result ensure() {
    Result result;
#ifdef __3DS__
    if (!detail::retained.empty()) {
        result.ok=result.prepared=true;result.source=detail::retained_source;return result;
    }
    // Preserve libctru's established providers. This is an availability check,
    // not a claim that a supplied SD component has passed hardware validation;
    // the game's ndspInit/DSP_LoadComponent remains authoritative for those.
    // In particular, existing private emulator fixtures are never repackaged.
    if (std::FILE* file=std::fopen("sdmc:/3ds/dspfirm.cdc","rb")) {
        std::fclose(file);result.ok=true;result.source="sd-libctru";return result;
    }
    if (envGetHandle("hb:ndsp")) {
        result.ok=true;result.source="hb:ndsp";return result;
    }
    ::Result rc=cfguInit();
    if (R_FAILED(rc)) {
        result.code=rc;failure(result.error,sizeof result.error,"cannot initialize system region service");return result;
    }
    uint8_t region=0xff;
    rc=CFGU_SecureInfoGetRegion(&region);cfguExit();
    const uint64_t title=home_menu_title(region);
    if (R_FAILED(rc) || !title) {
        result.code=R_FAILED(rc)?rc:-1;
        failure(result.error,sizeof result.error,"cannot determine a supported Home Menu region");return result;
    }
    result.source=home_menu_source(region);
    uint32_t archive_words[]={uint32_t(title),uint32_t(title>>32),MEDIATYPE_NAND,0};
    const uint32_t file_words[]={0,0,2,0x646f632e,0x00000065};
    const FS_Path archive_path={PATH_BINARY,sizeof archive_words,archive_words};
    const FS_Path file_path={PATH_BINARY,sizeof file_words,file_words};
    detail::FileHandle file;
    rc=FSUSER_OpenFileDirectly(&file.value,ARCHIVE_SAVEDATA_AND_CONTENT,archive_path,file_path,FS_OPEN_READ,0);
    if (R_FAILED(rc)) {
        result.code=rc;
        failure(result.error,sizeof result.error,"cannot read Home Menu ExeFS (title content permission or title missing)");return result;
    }
    uint64_t stored64=0;
    rc=FSFILE_GetSize(file.value,&stored64);
    if (R_FAILED(rc) || stored64<0x300 || stored64>MaxCode) {
        result.code=R_FAILED(rc)?rc:-1;
        failure(result.error,sizeof result.error,"Home Menu code size is invalid or exceeds the 12 MiB cap");return result;
    }
    const std::size_t stored=std::size_t(stored64);
    uint8_t footer[8];
    if (!detail::read_exact(file.value,stored-8,footer,sizeof footer,result)) return result;
    BlzLayout layout;
    const bool compressed=blz_layout(footer,stored,layout);
    const std::size_t capacity=compressed?layout.decoded:stored;
    try {
        boot_memory::Vector<uint8_t> image(capacity);
        if (!detail::read_exact(file.value,0,image.data(),stored,result)) return result;
        Component view;
        if (find_component(image.data(),stored,view)) {
            detail::register_component(image,view,result.source,result);return result;
        }
        if (!compressed || !decompress_blz_in_place(image.data(),image.size(),layout,
                                                   result.error,sizeof result.error)) {
            if (!result.error[0]) failure(result.error,sizeof result.error,"Home Menu code is neither a valid DSP1 source nor supported BLZ");
            result.code=-1;return result;
        }
        if (!find_component(image.data(),image.size(),view,result.error,sizeof result.error)) {
            result.code=-1;return result;
        }
        detail::register_component(image,view,result.source,result);
    } catch (const std::bad_alloc&) {
        result.code=-1;failure(result.error,sizeof result.error,"Home Menu code allocation failed");
    }
#else
    failure(result.error,sizeof result.error,"DSP preparation requires the 3DS system services");
#endif
    return result;
}

// The caller MUST first stop all audio and complete ndspExit. The retained
// component is needed by libctru throughout sleep/resume and cannot move.
inline void release_after_audio_shutdown() {
    if (!detail::retained.empty()) {
#ifdef __3DS__
        ndspUseComponent(nullptr,0,0xff,0xff);
#endif
        boot_memory::Vector<uint8_t>().swap(detail::retained);
        detail::retained_source="none";
    }
}
} // namespace dsp_bootstrap
