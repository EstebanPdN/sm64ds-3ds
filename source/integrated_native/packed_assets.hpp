#pragma once
// Versioned, read-only packed filesystem. Payload order is the pinned manifest;
// no archive-controlled paths or lengths reach the filesystem or game allocator.
#include "rom_bootstrap.hpp"
#include <array>
#include <fcntl.h>
#include "rom_discovery.hpp"
namespace packed_assets {
inline constexpr uint32_t header_bytes=128;
inline constexpr std::size_t count=rom_install_manifest::file_count;
inline constexpr auto offsets=[](){std::array<uint32_t,count> out{};uint32_t at=header_bytes;
    for(std::size_t i=0;i<count;++i){out[i]=at;at+=rom_install_manifest::files[i].size;}return out;}();
inline uint32_t u32(const unsigned char* p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
inline uint64_t u64(const unsigned char* p){return u32(p)|(uint64_t(u32(p+4))<<32);}
inline bool header_valid(const unsigned char* h,std::size_t n){
    if(!h||n!=header_bytes||std::memcmp(h,"SM64DSP1",8)||u32(h+8)!=1||u32(h+12)!=count||
       u64(h+16)!=rom_install_manifest::total_bytes||std::memcmp(h+24,rom_install_manifest::schema_id,16)||
       std::memcmp(h+40,rom_install_manifest::metadata_sha256,64))return false;
    for(unsigned i=104;i<header_bytes;++i)if(h[i])return false;
    return true;
}
inline bool extension(const char* name){
    if(!name)return false;
    const auto n=std::strlen(name);if(n<7)return false;
    const char* p=name+n-7;const char* expected=".sm64ds";
    for(unsigned i=0;i<7;++i){char c=p[i];if(c>='A'&&c<='Z')c=char(c-'A'+'a');if(c!=expected[i])return false;}return true;
}
inline int lookup(const char* path){
    if(!path||!rom_bootstrap::detail::safe_relative(path))return -1;
    std::size_t lo=0,hi=count;
    while(lo<hi){const auto mid=lo+(hi-lo)/2;const int cmp=std::strcmp(path,rom_install_manifest::files[mid].path);
        if(!cmp)return int(mid);
        if(cmp<0)hi=mid;else lo=mid+1;}
    return -1;
}
inline rom_discovery::Result next(const char* directory,const char* after=""){
    rom_discovery::Result out;
    try{
        rom_discovery::detail::Directory dir;dir.handle=::opendir(directory);
        if(!dir.handle){rom_discovery::detail::error(out,rom_discovery::Failure::Directory,"Open package directory",directory,errno);return out;}
        boot_memory::String path(directory);path+='/';const auto prefix=path.size();
        for(;;){errno=0;const dirent* entry=::readdir(dir.handle);
            if(!entry){if(errno)rom_discovery::detail::error(out,rom_discovery::Failure::Directory,"Read package directory",directory,errno);break;}
            if(!extension(entry->d_name))continue;
            path.resize(prefix);path+=entry->d_name;
            struct stat st{};if(::lstat(path.c_str(),&st)){if(errno==ENOENT)continue;
                rom_discovery::detail::error(out,rom_discovery::Failure::Directory,"Inspect package",path.c_str(),errno);return out;}
            if(!S_ISREG(st.st_mode))continue;
            ++out.nds_files;
            if((!after||std::strcmp(path.c_str(),after)>0)&&(!out.found||path<out.path)){out.path=path;out.found=true;}
        }
        if(!dir.close())rom_discovery::detail::error(out,rom_discovery::Failure::Directory,"Close package directory",directory,errno);
        return out;
    }catch(const std::bad_alloc&){rom_discovery::detail::error(out,rom_discovery::Failure::Memory,"Package discovery memory",directory,ENOMEM);return out;}
}
inline rom_bootstrap::Result verify(const char* path,rom_bootstrap::Callback callback=nullptr,void* context=nullptr){
    rom_bootstrap::Result out;
    try{
        rom_bootstrap::detail::FileHandle file;file.file=std::fopen(path,"rb");
        if(!file.file){out.error="Cannot open .sm64ds package";return out;}
        struct stat st{};
        if(::fstat(::fileno(file.file),&st)||!S_ISREG(st.st_mode)||st.st_size<0||
           uint64_t(st.st_size)!=header_bytes+rom_install_manifest::total_bytes){out.error="Incorrect .sm64ds package size";return out;}
        unsigned char header[header_bytes];
        if(std::fread(header,1,sizeof header,file.file)!=sizeof header||!header_valid(header,sizeof header)){
            out.error="Incompatible .sm64ds package header";return out;}
        std::array<unsigned char,rom_bootstrap::detail::buffer_bytes> buffer{};
        uint64_t done=0;
        for(std::size_t i=0;i<count;++i){const auto& entry=rom_install_manifest::files[i];uint32_t left=entry.size,crc=0xffffffffu;
            if(i&&std::strcmp(rom_install_manifest::files[i-1].path,entry.path)>=0){out.error="Invalid packed manifest order";return out;}
            while(left){const auto n=left<buffer.size()?left:buffer.size();
                if(std::fread(buffer.data(),1,n,file.file)!=n){out.error="Cannot read complete .sm64ds package";return out;}
                crc=rom_bootstrap::detail::crc_update(crc,buffer.data(),n);done+=n;left-=uint32_t(n);
                if(!rom_bootstrap::detail::notify(callback,context,out,"verify-cache",done,rom_install_manifest::total_bytes,unsigned(i),unsigned(count)))return out;}
            if((crc^0xffffffffu)!=entry.crc32){out.error="Damaged .sm64ds package; recreate it on your computer";return out;}
        }
        if(!file.close(out,path))return out;
        out.ok=true;out.reused=unsigned(count);out.root="s64:";return out;
    }catch(const std::bad_alloc&){out.error="Insufficient memory for .sm64ds package";return out;}
}
// Optional reader of the mounted package at absolute offsets, safe to call
// from several threads (3DS: one SD file handle, packed_assets_3ds.hpp). With
// it, opening a member costs no SD open; without it each stream opens the file.
using SharedRead=std::size_t(*)(uint64_t offset,void* output,std::size_t amount);
inline SharedRead shared_read=nullptr;
// Each open has an independent position, so game/prefetch threads never
// share a seek position. Only constant metadata and the mounted path are global.
struct Stream {
    std::FILE* file=nullptr;uint32_t base=0,size=0;int64_t position=0;bool shared=false;
    bool open(const char* archive,unsigned index){
        if(index>=count||file||shared)return false;
        if(shared_read)shared=true;
        else{file=std::fopen(archive,"rb");if(!file)return false;}
        base=offsets[index];size=rom_install_manifest::files[index].size;position=0;return true;
    }
    int close(){shared=false;auto* old=file;file=nullptr;return old?std::fclose(old):0;}
    int64_t seek(int64_t value,int whence){
        int64_t origin;if(whence==SEEK_SET)origin=0;else if(whence==SEEK_CUR)origin=position;else if(whence==SEEK_END)origin=size;else return -1;
        // Bound logical offsets to signed 32-bit newlib off_t, without overflow.
        if(value < -origin || value > INT32_MAX-origin)return -1;
        position=origin+value;return position;
    }
    std::size_t read(void* output,std::size_t amount){
        if(!file&&!shared){errno=EBADF;return 0;}if(position>=size)return 0;
        const auto available=std::size_t(size-position);if(amount>available)amount=available;
        if(shared){const auto got=shared_read(uint64_t(base)+uint64_t(position),output,amount);position+=int64_t(got);return got;}
        if(std::fseek(file,long(base+position),SEEK_SET)){errno=EIO;return 0;}
        const auto got=std::fread(output,1,amount,file);position+=int64_t(got);return got;
    }
};
} // namespace packed_assets
