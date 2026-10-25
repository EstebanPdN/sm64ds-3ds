#pragma once
// Console preparation uses the same versioned single-file format as the desktop
// packer. No ROM payload is compiled in, and no game/global-new heap is used.
#include "packed_assets.hpp"
namespace packed_builder {
namespace detail {
using rom_bootstrap::Result;
using rom_bootstrap::Callback;
namespace base=rom_bootstrap::detail;
#if defined(__GNUC__)
__attribute__((noinline))
#endif
inline Result verify(const char* path,Callback cb=nullptr,void* ctx=nullptr){
    return packed_assets::verify(path,cb,ctx);
}
inline bool cancelled(const Result& result){return result.error.find("Cancelled during ")==0;}
inline bool inspect(const boot_memory::String& path,bool& exists,Result& out){
    struct stat st{};exists=false;
    if(::lstat(path.c_str(),&st)){
        if(errno==ENOENT)return true;
        out.error=base::system_error("Inspect package",path);return false;
    }
    exists=true;
    if(!S_ISREG(st.st_mode)){out.error="Refusing non-file package entry: "+path;return false;}
    return true;
}
// Separate non-inlined streaming calls keep their 64 KiB scratch lifetimes
// disjoint from packed_assets::verify's 64 KiB readback scratch.
#if defined(__GNUC__)
__attribute__((noinline))
#endif
inline bool validate_rom(std::FILE* rom,const char* path,Result& out,Callback cb,void* ctx){
    struct stat st{};
    if(::fstat(::fileno(rom),&st)){out.error=base::system_error("Inspect ROM",path);return false;}
    if(!S_ISREG(st.st_mode)||st.st_size<0||uint64_t(st.st_size)!=rom_install_manifest::rom_size){
        out.error="Incorrect ROM size; the supported European revision is required";return false;
    }
    std::array<unsigned char,base::buffer_bytes> scratch;
    base::Sha1 sha;uint32_t crc=0xffffffffu;uint64_t done=0;
    if(!base::notify(cb,ctx,out,"verify-rom",0,rom_install_manifest::rom_size,0,unsigned(packed_assets::count)))return false;
    while(done<rom_install_manifest::rom_size){
        const auto left=rom_install_manifest::rom_size-done;
        const auto n=std::size_t(left<scratch.size()?left:scratch.size());
        if(std::fread(scratch.data(),1,n,rom)!=n){out.error="Could not read the complete ROM";return false;}
        sha.update(scratch.data(),n);crc=base::crc_update(crc,scratch.data(),n);done+=n;
        if(!base::notify(cb,ctx,out,"verify-rom",done,rom_install_manifest::rom_size,0,unsigned(packed_assets::count)))return false;
    }
    const auto digest=sha.finish();
    if((crc^0xffffffffu)!=rom_install_manifest::rom_crc32||std::memcmp(digest.data(),rom_install_manifest::sha1,20)){
        out.error="Unsupported or damaged ROM; the supported European revision is required";return false;
    }
    return true;
}
inline void put32(unsigned char* p,uint32_t value){for(unsigned i=0;i<4;++i)p[i]=static_cast<unsigned char>(value>>(i*8));}
#if defined(__GNUC__)
__attribute__((noinline))
#endif
inline bool write(std::FILE* rom,std::FILE* output,Result& out,Callback cb,void* ctx){
    unsigned char header[packed_assets::header_bytes]{};
    std::memcpy(header,"SM64DSP1",8);put32(header+8,1);put32(header+12,uint32_t(packed_assets::count));
    put32(header+16,uint32_t(rom_install_manifest::total_bytes));put32(header+20,uint32_t(rom_install_manifest::total_bytes>>32));
    std::memcpy(header+24,rom_install_manifest::schema_id,16);std::memcpy(header+40,rom_install_manifest::metadata_sha256,64);
    if(std::fwrite(header,1,sizeof header,output)!=sizeof header){out.error="Cannot write .sm64ds header";return false;}
    std::array<unsigned char,base::buffer_bytes> scratch;uint64_t complete=0;
    for(std::size_t i=0;i<packed_assets::count;++i){const auto& entry=rom_install_manifest::files[i];
        if(!base::safe_relative(entry.path)||(i&&std::strcmp(rom_install_manifest::files[i-1].path,entry.path)>=0)||
           (!entry.generated&&(uint64_t(entry.offset)+entry.size>rom_install_manifest::rom_size||uint64_t(entry.offset)>LONG_MAX))){
            out.error="Invalid packed manifest entry";return false;
        }
        if(!entry.generated&&std::fseek(rom,long(entry.offset),SEEK_SET)){out.error="Cannot seek ROM extraction span";return false;}
        std::size_t done=0;uint32_t crc=0xffffffffu;
        while(done<entry.size){const auto left=entry.size-done;const auto n=left<scratch.size()?left:scratch.size();
            const unsigned char* bytes=entry.generated?entry.generated+done:scratch.data();
            if(!entry.generated&&std::fread(scratch.data(),1,n,rom)!=n){out.error="Cannot read ROM extraction span";return false;}
            crc=base::crc_update(crc,bytes,n);
            if(std::fwrite(bytes,1,n,output)!=n){out.error="Cannot write .sm64ds payload";return false;}
            done+=n;
            if(!base::notify(cb,ctx,out,"extract",complete+done,rom_install_manifest::total_bytes,unsigned(i),unsigned(packed_assets::count)))return false;
        }
        if((crc^0xffffffffu)!=entry.crc32){out.error="ROM extraction checksum mismatch: ";out.error+=entry.path;return false;}
        complete+=entry.size;
    }
    if(complete!=rom_install_manifest::total_bytes){out.error="Invalid packed manifest byte count";return false;}
    return true;
}
inline bool publish(const boot_memory::String& part,const boot_memory::String& output,Result& out){
    bool exists=false;if(!inspect(output,exists,out))return false;
    if(exists){
        // Recheck immediately before replacement. A previously valid package is
        // never removed. FAT replacement requires removing only a verified-bad
        // destination; loss of power in this small window leaves verified .part
        // available for recovery on the next launch.
        auto prior=verify(output.c_str());
        if(prior.ok){out.ok=true;out.reused=unsigned(packed_assets::count);out.root="s64:";return true;}
        if(!base::remove_existing(output,out))return false;
    }
    if(std::rename(part.c_str(),output.c_str())){out.error=base::system_error("Publish package",part);return false;}
    out.ok=true;out.prepared=true;out.written=1;out.root="s64:";return true;
}
inline Result run(const char* rom_path,const char* output_path,Callback cb,void* ctx){
    Result out;
    if(!rom_path||!*rom_path||!output_path||!*output_path||!std::strcmp(rom_path,output_path)){
        out.error="Separate ROM and package paths are required";return out;
    }
    const boot_memory::String output(output_path),part=output+".part";
    if(part==rom_path){out.error="Package temporary path conflicts with ROM";return out;}
    bool exists=false;if(!inspect(output,exists,out))return out;
    if(exists){auto prior=verify(output_path,cb,ctx);if(prior.ok||cancelled(prior))return prior;}
    base::FileHandle rom;rom.file=std::fopen(rom_path,"rb");
    if(!rom.file){out.error=base::system_error("Open ROM",rom_path);return out;}
    if(!validate_rom(rom.file,rom_path,out,cb,ctx))return out;
    // All writes, including stale scratch removal, occur after ROM SHA1+CRC.
    if(!inspect(part,exists,out))return out;
    if(exists){auto recovered=verify(part.c_str(),cb,ctx);
        if(cancelled(recovered))return recovered;
        if(recovered.ok){
            // A complete temporary can originate from a crash before its sync.
            // Re-establish durability even when all its member CRCs already pass.
            base::FileHandle durable;durable.file=std::fopen(part.c_str(),"r+b");
            if(!durable.file){out.error=base::system_error("Open recovered package",part);return out;}
            if(!base::durable_close(durable,part,out)||!rom.close(out,rom_path))return out;
            publish(part,output,out);return out;
        }
        if(!base::remove_existing(part,out))return out;
    }
    {base::FileHandle file;file.file=std::fopen(part.c_str(),"wb");
        if(!file.file){out.error=base::system_error("Create package",part);return out;}
        if(!write(rom.file,file.file,out,cb,ctx)||!base::durable_close(file,part,out))return out;
    }
    if(!rom.close(out,rom_path))return out;
    auto checked=verify(part.c_str(),cb,ctx);
    if(!checked.ok)return checked;
    if(!base::notify(cb,ctx,out,"finish",rom_install_manifest::total_bytes,rom_install_manifest::total_bytes,
                     unsigned(packed_assets::count),unsigned(packed_assets::count)))return out;
    publish(part,output,out);return out;
}
} // namespace detail
inline rom_bootstrap::Result ensure(const char* rom_path,const char* output_path,rom_bootstrap::Callback cb=nullptr,void* ctx=nullptr){
    try{return detail::run(rom_path,output_path,cb,ctx);}
    catch(const std::bad_alloc&){rom_bootstrap::Result out;out.error="Insufficient memory while preparing .sm64ds package";return out;}
}
} // namespace packed_builder
