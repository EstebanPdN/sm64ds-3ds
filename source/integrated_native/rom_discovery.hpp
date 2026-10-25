#pragma once
// Read-only, non-recursive discovery before the original game's DS heap exists.
// A matching header is only a candidate: ensure() must still verify the exact
// ROM SHA/CRC before creating or modifying its private installation cache.
#include "boot_allocator.hpp"
#include "rom_install_manifest.hpp"
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <new>
#include <sys/stat.h>
#include <unistd.h>

namespace rom_discovery {
enum class Failure { None, Directory, Memory };
struct Result {
    bool found=false;
    Failure failure=Failure::None;
    boot_memory::String path;
    // These count the whole directory on every successful scan, independently
    // of after_path. nds_files excludes directories, links and special files.
    unsigned nds_files=0,compatible_headers=0;
    char error[256]{};
};

namespace detail {
inline bool nds_extension(const char* name){
    if(!name)return false;
    const std::size_t size=std::strlen(name);
    if(size<4||name[size-4]!='.')return false;
    const unsigned char n=static_cast<unsigned char>(name[size-3]),
        d=static_cast<unsigned char>(name[size-2]),s=static_cast<unsigned char>(name[size-1]);
    return (n=='n'||n=='N')&&(d=='d'||d=='D')&&(s=='s'||s=='S');
}
inline bool compatible(const uint8_t* header,std::size_t length){
    return header&&length>=0x20&&!std::memcmp(header+0x0c,"ASMP",4)&&header[0x1e]==0;
}
inline void error(Result& out,Failure kind,const char* operation,const char* path,int code){
    out.found=false;out.failure=kind;
    boot_memory::String().swap(out.path);
    std::snprintf(out.error,sizeof out.error,"%s: %s: %s",operation,path?path:"(null)",std::strerror(code));
}
struct Directory {
    DIR* handle=nullptr;
    ~Directory(){if(handle)::closedir(handle);}
    bool close(){DIR* old=handle;handle=nullptr;return !old||::closedir(old)==0;}
    Directory()=default;Directory(const Directory&)=delete;Directory& operator=(const Directory&)=delete;
};
struct File {
    std::FILE* handle=nullptr;
    ~File(){if(handle)std::fclose(handle);}
    bool close(){std::FILE* old=handle;handle=nullptr;return !old||std::fclose(old)==0;}
    File()=default;File(const File&)=delete;File& operator=(const File&)=delete;
};
inline void increment(unsigned& count){if(count<UINT_MAX)++count;}
} // namespace detail

// Paths use byte lexicographic order, independent of directory enumeration,
// locale and filename encoding. At most two path buffers are alive per scan;
// there is no list proportional to the number of directory entries.
inline Result next(const char* directory,const char* after_path=""){
    Result out;
    try{
        if(!directory||!*directory){detail::error(out,Failure::Directory,"Invalid ROM directory",directory,EINVAL);return out;}
        detail::Directory dir;
        dir.handle=::opendir(directory);
        if(!dir.handle){detail::error(out,Failure::Directory,"Open ROM directory",directory,errno);return out;}
        boot_memory::String candidate(directory);
        if(candidate.back()!='/')candidate.push_back('/');
        const std::size_t prefix=candidate.size();
        for(;;){
            // readdir signals EOF with nullptr and errno unchanged. A previous
            // lstat/fopen must never turn a clean EOF into a spurious scan error.
            errno=0;
            const dirent* entry=::readdir(dir.handle);
            if(!entry){
                if(errno){detail::error(out,Failure::Directory,"Read ROM directory",directory,errno);return out;}
                break;
            }
            if(!detail::nds_extension(entry->d_name))continue;
            candidate.resize(prefix);candidate.append(entry->d_name);
            struct stat st{};
            if(::lstat(candidate.c_str(),&st)!=0){
                const int code=errno;
                if(code==ENOENT)continue; // A concurrently removed entry.
                detail::error(out,Failure::Directory,"Inspect ROM candidate",candidate.c_str(),code);return out;
            }
            if(!S_ISREG(st.st_mode))continue;
            detail::increment(out.nds_files);
            if(st.st_size<0||uint64_t(st.st_size)!=rom_install_manifest::rom_size)continue;
            detail::File file;
            file.handle=std::fopen(candidate.c_str(),"rb");
            if(!file.handle){
                const int code=errno;
                if(code==ENOENT)continue;
                detail::error(out,Failure::Directory,"Open ROM candidate",candidate.c_str(),code);return out;
            }
            struct stat current{};
            if(::fstat(::fileno(file.handle),&current)!=0){detail::error(out,Failure::Directory,"Inspect open ROM",candidate.c_str(),errno);return out;}
            if(!S_ISREG(current.st_mode)||current.st_size<0||uint64_t(current.st_size)!=rom_install_manifest::rom_size){
                if(!file.close()){detail::error(out,Failure::Directory,"Close changed ROM candidate",candidate.c_str(),errno);return out;}
                continue;
            }
            uint8_t header[0x20];const std::size_t length=std::fread(header,1,sizeof header,file.handle);
            if(std::ferror(file.handle)){const int code=errno?errno:EIO;detail::error(out,Failure::Directory,"Read ROM header",candidate.c_str(),code);return out;}
            if(!file.close()){detail::error(out,Failure::Directory,"Close ROM candidate",candidate.c_str(),errno);return out;}
            if(!detail::compatible(header,length))continue;
            detail::increment(out.compatible_headers);
            if(after_path&&std::strcmp(candidate.c_str(),after_path)<=0)continue;
            if(!out.found||candidate.compare(out.path)<0){out.path=candidate;out.found=true;}
        }
        if(!dir.close())detail::error(out,Failure::Directory,"Close ROM directory",directory,errno);
    }catch(const std::bad_alloc&){
        // The error path itself must not touch global new or allocate a string.
        out.found=false;out.failure=Failure::Memory;boot_memory::String().swap(out.path);
        std::snprintf(out.error,sizeof out.error,"Insufficient libc memory while scanning the ROM directory");
    }
    return out;
}
} // namespace rom_discovery
