#pragma once
// Prepare the native port's private, versioned cache from the user's exact ROM.
// This header contains no ROM payload. The generated manifest is build-local.
#include "rom_install_manifest.hpp"
#include "boot_allocator.hpp"
#include <array>
#include <cerrno>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace rom_bootstrap {
struct Progress {
    const char* phase;
    uint64_t done, total;
    unsigned files_done, files_total;
};
using Callback = bool (*)(const Progress&, void*);
struct Result {
    bool ok = false;
    bool prepared = false;
    boot_memory::String root, error;
    unsigned written = 0, reused = 0;
};

namespace detail {
inline constexpr size_t buffer_bytes = 64 * 1024;
inline constexpr auto crc_table = [] {
    std::array<uint32_t, 256> table{};
    for (unsigned i = 0; i != table.size(); ++i) {
        uint32_t value = i;
        for (unsigned bit = 0; bit != 8; ++bit)
            value = (value >> 1) ^ ((value & 1) ? 0xedb88320u : 0u);
        table[i] = value;
    }
    return table;
}();
inline uint32_t crc_update(uint32_t state, const unsigned char* bytes, size_t size) {
    for (size_t i = 0; i != size; ++i)
        state = crc_table[(state ^ bytes[i]) & 255u] ^ (state >> 8);
    return state;
}
inline uint32_t crc(const unsigned char* bytes, size_t size) {
    return crc_update(0xffffffffu, bytes, size) ^ 0xffffffffu;
}

// Standard SHA-1, streamed through the same 64 KiB scratch buffer as extraction.
struct Sha1 {
    uint32_t h[5] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u, 0xc3d2e1f0u};
    unsigned char block[64]{};
    size_t used = 0;
    uint64_t bytes = 0;
    static uint32_t rotate(uint32_t value, unsigned bits) {
        return (value << bits) | (value >> (32 - bits));
    }
    void transform() {
        uint32_t words[80];
        for (unsigned i = 0; i != 16; ++i)
            words[i] = (uint32_t(block[4*i]) << 24) | (uint32_t(block[4*i+1]) << 16) |
                       (uint32_t(block[4*i+2]) << 8) | uint32_t(block[4*i+3]);
        for (unsigned i = 16; i != 80; ++i)
            words[i] = rotate(words[i-3] ^ words[i-8] ^ words[i-14] ^ words[i-16], 1);
        uint32_t a=h[0], b=h[1], c=h[2], d=h[3], e=h[4];
        for (unsigned i = 0; i != 80; ++i) {
            uint32_t function, constant;
            if (i < 20) { function=(b&c)|((~b)&d); constant=0x5a827999u; }
            else if (i < 40) { function=b^c^d; constant=0x6ed9eba1u; }
            else if (i < 60) { function=(b&c)|(b&d)|(c&d); constant=0x8f1bbcdcu; }
            else { function=b^c^d; constant=0xca62c1d6u; }
            const uint32_t next=rotate(a,5)+function+e+constant+words[i];
            e=d; d=c; c=rotate(b,30); b=a; a=next;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e;
    }
    void update(const unsigned char* data, size_t size) {
        bytes += size;
        while (size) {
            const size_t room = sizeof(block)-used;
            const size_t count = size < room ? size : room;
            std::memcpy(block+used, data, count);
            used += count; data += count; size -= count;
            if (used == sizeof(block)) { transform(); used=0; }
        }
    }
    std::array<unsigned char,20> finish() {
        const uint64_t bits = bytes * 8;
        const unsigned char one=0x80, zero=0;
        update(&one,1);
        while (used != 56) update(&zero,1);
        unsigned char length[8];
        for (unsigned i=0; i!=8; ++i) length[i]=static_cast<unsigned char>(bits >> (56-8*i));
        update(length,sizeof(length));
        std::array<unsigned char,20> digest{};
        for (unsigned i=0; i!=5; ++i)
            for (unsigned j=0; j!=4; ++j) digest[4*i+j]=static_cast<unsigned char>(h[i] >> (24-8*j));
        return digest;
    }
};

inline boot_memory::String system_error(const char* operation, const boot_memory::String& path, int code=errno) {
    return boot_memory::String(operation)+": "+path+": "+std::strerror(code);
}
inline bool notify(Callback callback, void* context, Result& out, const char* phase,
                   uint64_t done, uint64_t total, unsigned files_done, unsigned files_total) {
    if (!callback || callback(Progress{phase,done,total,files_done,files_total},context)) return true;
    out.error = boot_memory::String("Cancelled during ")+phase;
    return false;
}
inline bool safe_relative(const char* path) {
    if (!path || !*path || *path=='/') return false;
    const char* component=path;
    for (const char* p=path;;++p) {
        const unsigned char c=static_cast<unsigned char>(*p);
        if (c=='\\' || c==':' || (c && c<32) || c==127) return false;
        if (c=='/' || !c) {
            const size_t size=size_t(p-component);
            if (!size || (size==1 && component[0]=='.') ||
                (size==2 && component[0]=='.' && component[1]=='.')) return false;
            if (!c) return true;
            component=p+1;
        }
    }
}
inline bool regular(const boot_memory::String& path, struct stat& st, Result& out, bool missing_ok) {
    if (::lstat(path.c_str(),&st) != 0) {
        if (missing_ok && errno==ENOENT) return false;
        out.error=system_error("stat",path); return false;
    }
    if (!S_ISREG(st.st_mode)) { out.error="Expected regular cache file: "+path; return false; }
    return true;
}
inline bool directory(const boot_memory::String& path, Result& out) {
    struct stat st{};
    if (::lstat(path.c_str(),&st)==0) {
        if (S_ISDIR(st.st_mode)) return true;
        out.error="Expected cache directory: "+path; return false;
    }
    if (errno!=ENOENT) { out.error=system_error("stat",path); return false; }
    if (::mkdir(path.c_str(),0777)!=0) { out.error=system_error("mkdir",path); return false; }
    return true;
}
// The trusted caller chooses cache_base; every manifest component is validated.
// Check each component so a host symlink cannot redirect writes outside the cache.
inline bool directories(const boot_memory::String& path, Result& out) {
    for (size_t i=0; i<path.size(); ++i) {
        if (path[i]!='/' || i==0 || path[i-1]==':') continue;
        if (!directory(path.substr(0,i),out)) return false;
    }
    return directory(path,out);
}
inline bool remove_existing(const boot_memory::String& path, Result& out) {
    struct stat st{};
    if (::lstat(path.c_str(),&st)!=0) {
        if (errno==ENOENT) return true;
        out.error=system_error("stat",path); return false;
    }
    if (!S_ISREG(st.st_mode)) { out.error="Refusing non-file cache entry: "+path; return false; }
    if (std::remove(path.c_str())==0) return true;
    out.error=system_error("remove",path); return false;
}
struct FileHandle {
    FILE* file=nullptr;
    ~FileHandle() { if(file) std::fclose(file); }
    bool close(Result& out,const boot_memory::String& path) {
        FILE* closing=file; file=nullptr;
        if (std::fclose(closing)==0) return true;
        out.error=system_error("close",path); return false;
    }
};
inline bool durable_close(FileHandle& file,const boot_memory::String& path,Result& out) {
    if (std::fflush(file.file)!=0) { out.error=system_error("flush",path); return false; }
    if (::fsync(::fileno(file.file))!=0) { out.error=system_error("sync",path); return false; }
    return file.close(out,path);
}
inline bool publish_part(const boot_memory::String& part,const boot_memory::String& path,Result& out) {
    // FAT/sdmc rename does not replace. Only a missing or verified-bad cache file
    // reaches this function; .ready has already been removed for the transaction.
    if (!remove_existing(path,out)) return false;
    if (std::rename(part.c_str(),path.c_str())==0) return true;
    out.error=system_error("rename",part); return false;
}
inline bool manifest_valid(Result& out,uint64_t& total,unsigned& count) {
    const size_t actual=sizeof(rom_install_manifest::files)/sizeof(rom_install_manifest::files[0]);
    if (actual>UINT_MAX || !actual || actual!=rom_install_manifest::file_count) {
        out.error="Invalid ROM installation file count"; return false;
    }
    count=unsigned(actual); total=0;
    if (!safe_relative(rom_install_manifest::schema_id) ||
        std::strchr(rom_install_manifest::schema_id,'/')) {
        out.error="Invalid ROM installation schema"; return false;
    }
    for (size_t i=0; i<actual; ++i) {
        const auto& entry=rom_install_manifest::files[i];
        if (!safe_relative(entry.path) || std::strcmp(entry.path,".ready")==0 ||
            std::strcmp(entry.path,".ready.part")==0) {
            out.error="Unsafe ROM installation path"; return false;
        }
        for (size_t j=0; j<i; ++j) {
            if (std::strcmp(entry.path,rom_install_manifest::files[j].path)==0) {
                out.error="Duplicate ROM installation path"; return false;
            }
        }
        if (entry.generated) {
            if (crc(entry.generated,entry.size)!=entry.crc32) {
                out.error="Invalid generated installation metadata"; return false;
            }
        } else if (uint64_t(entry.offset)+entry.size>uint64_t(rom_install_manifest::rom_size) ||
                   uint64_t(entry.offset)>uint64_t(LONG_MAX)) {
            out.error="ROM installation span is outside the ROM"; return false;
        }
        total+=entry.size;
    }
    if (total!=rom_install_manifest::total_bytes) {
        out.error="Invalid ROM installation byte count"; return false;
    }
    return true;
}

inline Result run(const char* rom_path,const char* cache_base,Callback callback,void* context) {
    Result out;
    if (!rom_path || !*rom_path || !cache_base || !*cache_base) {
        out.error="ROM path and private cache directory are required"; return out;
    }
    uint64_t total=0; unsigned count=0;
    if (!manifest_valid(out,total,count)) return out;
    out.root=boot_memory::String(cache_base)+"/"+rom_install_manifest::schema_id+"/repo";
    const boot_memory::String ready=out.root+"/.ready";
    std::array<unsigned char,buffer_bytes> buffer{};
    FileHandle rom;
    rom.file=std::fopen(rom_path,"rb");
    if (!rom.file) { out.error=system_error("Open ROM",rom_path); return out; }
    struct stat rom_stat{};
    if (::fstat(::fileno(rom.file),&rom_stat)!=0) { out.error=system_error("stat ROM",rom_path); return out; }
    if (!S_ISREG(rom_stat.st_mode) || rom_stat.st_size<0 ||
        uint64_t(rom_stat.st_size)!=uint64_t(rom_install_manifest::rom_size)) {
        out.error="Incorrect ROM size; the supported European revision is required"; return out;
    }
    Sha1 sha; uint32_t rom_crc=0xffffffffu; uint64_t read=0;
    if (!notify(callback,context,out,"verify-rom",0,rom_install_manifest::rom_size,0,count)) return out;
    while (read<uint64_t(rom_install_manifest::rom_size)) {
        const uint64_t remaining=uint64_t(rom_install_manifest::rom_size)-read;
        const size_t amount=size_t(remaining<buffer.size()?remaining:buffer.size());
        if (std::fread(buffer.data(),1,amount,rom.file)!=amount) {
            out.error="Could not read the complete ROM"; return out;
        }
        sha.update(buffer.data(),amount); rom_crc=crc_update(rom_crc,buffer.data(),amount); read+=amount;
        if (!notify(callback,context,out,"verify-rom",read,rom_install_manifest::rom_size,0,count)) return out;
    }
    const auto digest=sha.finish();
    if ((rom_crc^0xffffffffu)!=rom_install_manifest::rom_crc32 ||
        std::memcmp(digest.data(),rom_install_manifest::sha1,digest.size())!=0) {
        out.error="Unsupported or damaged ROM; the supported European revision is required"; return out;
    }
    // No cache directory or output has been written before exact ROM validation.
    if (!directories(out.root,out)) return out;
    struct stat marker_stat{};
    const bool had_ready=::lstat(ready.c_str(),&marker_stat)==0 && S_ISREG(marker_stat.st_mode);
    if (!remove_existing(ready,out)) return out;
    uint64_t complete=0;
    boot_memory::String checked_parent;
    for (unsigned index=0; index<count; ++index) {
        const auto& entry=rom_install_manifest::files[index];
        const boot_memory::String path=out.root+"/"+entry.path;
        if (!notify(callback,context,out,"verify-cache",complete,total,index,count)) return out;
        const size_t separator=path.rfind('/');
        if (separator==boot_memory::String::npos) { out.error="Missing cache parent directory"; return out; }
        const boot_memory::String parent=path.substr(0,separator);
        // Validate parents on reuse, too: never read through a host symlink.
        // Consecutive files share most parents in the sorted generated manifest.
        if (parent!=checked_parent) {
            if (!directories(parent,out)) return out;
            checked_parent=parent;
        }
        bool reuse=false;
        struct stat st{};
        if (regular(path,st,out,true)) {
            if (st.st_size>=0 && uint64_t(st.st_size)==entry.size) {
                FileHandle file; file.file=std::fopen(path.c_str(),"rb");
                if (!file.file) { out.error=system_error("Open cache",path); return out; }
                uint32_t checksum=0xffffffffu; size_t done=0;
                while (done<entry.size) {
                    const size_t remaining=entry.size-done;
                    const size_t amount=remaining<buffer.size()?remaining:buffer.size();
                    if (std::fread(buffer.data(),1,amount,file.file)!=amount) {
                        out.error="Could not read complete cache file: "+path; return out;
                    }
                    checksum=crc_update(checksum,buffer.data(),amount); done+=amount;
                    if (!notify(callback,context,out,"verify-cache",complete+done,total,index,count)) return out;
                }
                if (!file.close(out,path)) return out;
                reuse=(checksum^0xffffffffu)==entry.crc32;
            }
        } else if (!out.error.empty()) return out;
        if (reuse) { ++out.reused; complete+=entry.size; continue; }
        const boot_memory::String part=path+".part";
        // Stale .part data is private scratch, but never follow a host symlink.
        if (!remove_existing(part,out)) return out;
        FileHandle output; output.file=std::fopen(part.c_str(),"wb");
        if (!output.file) { out.error=system_error("Create cache",part); return out; }
        if (!entry.generated && std::fseek(rom.file,long(entry.offset),SEEK_SET)!=0) {
            out.error=system_error("Seek ROM",rom_path); return out;
        }
        size_t done=0; uint32_t checksum=0xffffffffu;
        while (done<entry.size) {
            const size_t remaining=entry.size-done;
            const size_t amount=remaining<buffer.size()?remaining:buffer.size();
            const unsigned char* bytes=entry.generated ? entry.generated+done : buffer.data();
            if (!entry.generated && std::fread(buffer.data(),1,amount,rom.file)!=amount) {
                out.error="Could not read ROM extraction span"; return out;
            }
            checksum=crc_update(checksum,bytes,amount);
            if (std::fwrite(bytes,1,amount,output.file)!=amount) {
                out.error=system_error("Write cache",part); return out;
            }
            done+=amount;
            if (!notify(callback,context,out,"extract",complete+done,total,index,count)) return out;
        }
        if ((checksum^0xffffffffu)!=entry.crc32) {
            out.error="ROM extraction checksum mismatch: "+path; return out;
        }
        if (!durable_close(output,part,out) || !publish_part(part,path,out)) return out;
        ++out.written; complete+=entry.size;
    }
    // Closing the original ROM is part of successful preparation, too.
    if (!rom.close(out,rom_path)) return out;
    if (!notify(callback,context,out,"finish",total,total,count,count)) return out;
    const boot_memory::String marker_part=ready+".part";
    if (!remove_existing(marker_part,out)) return out;
    FileHandle marker; marker.file=std::fopen(marker_part.c_str(),"wb");
    if (!marker.file) { out.error=system_error("Create marker",marker_part); return out; }
    const boot_memory::String receipt=boot_memory::String("SM64DS-ROM-CACHE-1\n")+rom_install_manifest::schema_id+"\n"+
        rom_install_manifest::metadata_sha256+"\n";
    if (std::fwrite(receipt.data(),1,receipt.size(),marker.file)!=receipt.size()) {
        out.error=system_error("Write marker",marker_part); return out;
    }
    if (!durable_close(marker,marker_part,out) || !publish_part(marker_part,ready,out)) return out;
    out.ok=true; out.prepared=out.written!=0 || !had_ready;
    return out;
}
} // namespace detail

inline Result ensure(const char* rom_path,const char* cache_base,Callback progress=nullptr,void* context=nullptr) {
    try { return detail::run(rom_path,cache_base,progress,context); }
    catch (const std::bad_alloc&) { Result out; out.error="Insufficient memory while preparing the ROM cache"; return out; }
}
} // namespace rom_bootstrap
