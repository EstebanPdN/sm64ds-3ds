#pragma once
#include <cstdio>
#include "sd_writer.hpp"
namespace graphics_report {
bool begin(const char* directory);
bool finish(bool complete);
bool active();
FILE* open_member(const char* path,const char* mode);

// Full-memory acquisition needs SdWriter's seekable stream. Only the quick
// graphics acquisition routes its sequential files into the ZIP.
class Writer {
    native_diagnostics::SdWriter sd_;
    FILE* member_=nullptr;
public:
    Writer(void* scratch,size_t size):sd_(scratch,size){}
    ~Writer(){if(member_)std::fclose(member_);}
    bool open(const char* path,uint64_t size=0){
        if(!active())return sd_.open(path,size);
        member_=open_member(path,"wb");return member_!=nullptr;
    }
    bool write(const void* p,size_t n){
        return member_?std::fwrite(p,1,n,member_)==n:sd_.write(p,n);
    }
    bool close(){
        if(!member_)return sd_.close();
        FILE* f=member_;member_=nullptr;return std::fclose(f)==0;
    }
};
}
