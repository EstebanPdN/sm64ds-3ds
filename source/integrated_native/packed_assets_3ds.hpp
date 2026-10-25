#pragma once
#include "packed_assets.hpp"
#include <sys/iosupport.h>
#include <3ds.h>
#include <atomic>
namespace packed_assets {
inline char mounted_path[1024]{};
inline bool mounted=false;
inline std::atomic<unsigned> live_streams{0};
inline int error(struct _reent* r,int code){r->_errno=code;return -1;}
inline const char* relative(const char* p){
    if(!p||std::strncmp(p,"s64:",4))return nullptr;
    p+=4;if(*p=='/')++p;return p;
}
inline int open_r(struct _reent* r,void* data,const char* path,int flags,int){
    if((flags&O_ACCMODE)!=O_RDONLY||(flags&(O_CREAT|O_TRUNC|O_APPEND|O_EXCL)))return error(r,EROFS);
    const char* name=relative(path);const int index=lookup(name);if(index<0)return error(r,ENOENT);
    auto* stream=new(data) Stream{};
    if(!stream->open(mounted_path,unsigned(index)))return error(r,errno?errno:EIO);
    ++live_streams;return 0;
}
inline int close_r(struct _reent* r,void* data){auto* stream=static_cast<Stream*>(data);const int result=stream->close();--live_streams;return result?error(r,EIO):0;}
inline ssize_t read_r(struct _reent* r,void* data,char* output,std::size_t size){
    auto* stream=static_cast<Stream*>(data);const auto result=stream->read(output,size);
    if(!result&&size&&stream->position<stream->size)return error(r,EIO);
    return ssize_t(result);
}
inline off_t seek_r(struct _reent* r,void* data,off_t offset,int whence){
    const auto result=static_cast<Stream*>(data)->seek(offset,whence);if(result<0)return error(r,EINVAL);return off_t(result);
}
inline void describe(struct stat* st,uint32_t size,bool directory=false){std::memset(st,0,sizeof(*st));st->st_mode=(directory?S_IFDIR:S_IFREG)|0444;st->st_nlink=1;st->st_size=size;}
inline int fstat_r(struct _reent*,void* data,struct stat* st){describe(st,static_cast<Stream*>(data)->size);return 0;}
inline int stat_r(struct _reent* r,const char* path,struct stat* st){
    const auto* name=relative(path);if(!name)return error(r,ENOENT);const int index=lookup(name);
    if(index>=0){describe(st,rom_install_manifest::files[index].size);return 0;}
    const auto length=std::strlen(name);
    if(!length){describe(st,0,true);return 0;}
    if(!rom_bootstrap::detail::safe_relative(name))return error(r,ENOENT);
    for(const auto& entry:rom_install_manifest::files)if(!std::strncmp(name,entry.path,length)&&entry.path[length]=='/'){
        describe(st,0,true);return 0;}
    return error(r,ENOENT);
}
inline const devoptab_t device=[](){devoptab_t d{};d.name="s64";d.structSize=sizeof(Stream);
    d.open_r=open_r;d.close_r=close_r;d.read_r=read_r;d.seek_r=seek_r;d.fstat_r=fstat_r;d.stat_r=stat_r;d.lstat_r=stat_r;return d;}();
// One SD handle for the whole package: a member open is no SD open (each
// one cost ~25 ms in Azahar and several on hardware; a level load opens ~75).
// FSFILE_Read takes the offset, so the game and prefetch threads share it.
inline Handle shared_handle=0;
inline std::size_t read_shared(uint64_t offset,void* output,std::size_t amount){
    uint32_t got=0;
    if(R_FAILED(FSFILE_Read(shared_handle,&got,offset,output,uint32_t(amount)))){errno=EIO;return 0;}
    return got;
}
inline bool mount(const char* path){
    if(mounted||!path||std::strlen(path)>=sizeof(mounted_path))return false;
    std::strcpy(mounted_path,path);
    if(AddDevice(&device)<0){mounted_path[0]=0;return false;}mounted=true;
    if(!std::strncmp(path,"sdmc:/",6) && R_SUCCEEDED(FSUSER_OpenFileDirectly(&shared_handle,ARCHIVE_SDMC,
       fsMakePath(PATH_EMPTY,""),fsMakePath(PATH_ASCII,path+5),FS_OPEN_READ,0)))shared_read=read_shared;
    else{shared_handle=0;std::printf("Package shared handle unavailable: one SD open per file\n");}
    return true;
}
inline void unmount(){
    if(!mounted||live_streams)return;
    RemoveDevice("s64:");mounted=false;mounted_path[0]=0;
    if(shared_read){shared_read=nullptr;FSFILE_Close(shared_handle);shared_handle=0;}
}
} // namespace packed_assets
