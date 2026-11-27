#include "graphics_report.hpp"
#include <minizip/zip.h>
#include <cstring>
#include <unistd.h>
#include <initializer_list>

namespace graphics_report {
namespace {
zipFile archive=nullptr;
bool failed=false;
constexpr const char* names[]={"top-screen.bmp","bottom-screen.bmp","graphics.zip","info.txt"};
char pending[4][224],final_path[4][224];

int write_entry(void*,const char* data,int bytes){
    if(bytes<0||zipWriteInFileInZip(archive,data,unsigned(bytes))!=ZIP_OK){failed=true;return -1;}
    return bytes;
}
int close_entry(void*){
    if(zipCloseFileInZip(archive)!=ZIP_OK){failed=true;return -1;}
    return 0;
}
}
bool active(){return archive!=nullptr;}
bool begin(const char* directory){
    if(archive)return false;
    for(unsigned i=0;i<4;++i){
        if(std::snprintf(final_path[i],sizeof final_path[i],"%s.%s",directory,names[i])>=int(sizeof final_path[i])||
           std::snprintf(pending[i],sizeof pending[i],"%s.part",final_path[i])>=int(sizeof pending[i]))return false;
    }
    failed=false;
    archive=zipOpen(pending[2],APPEND_STATUS_CREATE);
    return archive!=nullptr;
}
FILE* open_member(const char* path,const char* mode){
    if(!archive)return std::fopen(path,mode);
    const char* slash=std::strrchr(path,'/');
    const char* name=slash?slash+1:path;
    for(unsigned i:{0u,1u,3u})if(std::strcmp(name,names[i])==0)return std::fopen(pending[i],mode);
    // Level 1 and a 4 KiB window bound CPU and heap use on Old 3DS.
    if(zipOpenNewFileInZip3(archive,name,nullptr,nullptr,0,nullptr,0,nullptr,
           Z_DEFLATED,1,0,-12,4,Z_DEFAULT_STRATEGY,nullptr,0)!=ZIP_OK){failed=true;return nullptr;}
    FILE* out=funopen(nullptr,nullptr,write_entry,nullptr,close_entry);
    if(!out){failed=true;close_entry(nullptr);}
    return out;
}
bool finish(bool complete){
    if(!archive)return complete;
    const bool closed=zipClose(archive,nullptr)==ZIP_OK;
    archive=nullptr;
    unsigned moved=0;
    if(complete&&!failed&&closed){
        while(moved<4&&std::rename(pending[moved],final_path[moved])==0)++moved;
        if(moved==4)return true;
    }
    for(unsigned i=0;i<4;++i)std::remove(i<moved?final_path[i]:pending[i]);
    return false;
}
}
