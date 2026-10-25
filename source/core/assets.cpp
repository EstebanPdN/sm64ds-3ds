#include "sm64ds/assets.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <limits>
#include <memory>
#include <set>
#include <sys/stat.h>

namespace sm64ds {
namespace {
using File = std::unique_ptr<FILE, decltype(&fclose)>;
constexpr uint32_t HEADER = 64, RECORD = 272, MAX_ROM = 256u * 1024 * 1024;
constexpr uint32_t MAX_FILES = 32768, MAX_FNT = 2 * 1024 * 1024;
uint16_t u16(const uint8_t* p) { return p[0] | (uint16_t(p[1]) << 8); }
uint32_t u32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
void put32(uint8_t* p, uint32_t v) { for (int i=0;i<4;++i) p[i]=uint8_t(v>>(i*8)); }
bool range(uint32_t off, uint32_t len, uint32_t total) { return off <= total && len <= total-off; }
bool read_at(FILE* f, uint32_t off, void* data, size_t n) {
    return fseek(f, long(off), SEEK_SET)==0 && fread(data,1,n,f)==n;
}
bool file_size(FILE* f, uint32_t& n) {
    if (fseek(f,0,SEEK_END)) return false;
    long v=ftell(f);
    if (v<0 || uint64_t(v)>MAX_ROM+32u*1024*1024) return false;
    n=uint32_t(v); return true;
}
bool safe_component(const std::string& s) {
    if (s.empty() || s=="." || s=="..") return false;
    for (unsigned char c:s) if (c<32 || c==127 || c=='/' || c=='\\' || c==':') return false;
    return true;
}
bool safe_path(const std::string& s) {
    if (s.empty() || s.size()>255) return false;
    size_t start=0;
    for (;;) {
        auto end=s.find('/',start);
        if (!safe_component(s.substr(start,end-start))) return false;
        if (end==std::string::npos) return true;
        start=end+1;
    }
}
uint16_t crc16(const uint8_t* p, size_t n) {
    uint16_t c=0xffff;
    while(n--) { c^=*p++; for(int i=0;i<8;++i)c=(c>>1)^((c&1)?0xa001:0); }
    return c;
}
bool valid_code(const std::string& s) {
    return s=="ASME" || s=="ASMP" || s=="ASMJ" || s=="ASMK" || s=="ASMC";
}
bool walk_names(const std::vector<uint8_t>& fnt, unsigned dir, unsigned parent,
                const std::string& prefix, std::vector<AssetEntry>& files,
                std::set<unsigned>& visited, unsigned dir_count, unsigned depth) {
    if (depth>32 || dir>=dir_count || !visited.insert(dir).second) return false;
    const auto* rec=fnt.data()+dir*8;
    if (dir && u16(rec+6)!=0xf000+parent) return false;
    uint32_t off=u32(rec), id=u16(rec+4);
    if (off<dir_count*8) return false;
    while(off<fnt.size()) {
        unsigned type=fnt[off++];
        if (!type) return true;
        unsigned len=type&127;
        if (!len || !range(off,len,uint32_t(fnt.size()))) return false;
        std::string name(reinterpret_cast<const char*>(fnt.data()+off),len);
        off+=len;
        if (!safe_component(name) || prefix.size()+name.size()>255) return false;
        if (type&128) {
            if (!range(off,2,uint32_t(fnt.size()))) return false;
            unsigned child=u16(fnt.data()+off);off+=2;
            if (child<0xf000 || !walk_names(fnt,child-0xf000,dir,prefix+name+"/",files,visited,dir_count,depth+1)) return false;
        } else {
            if (id>=files.size() || !files[id].path.empty()) return false;
            files[id++].path=prefix+name;
        }
    }
    return false;
}
bool block_crc(FILE* f, uint32_t offset, uint32_t size, uint32_t& crc) {
    std::array<uint8_t,32768> buffer;
    if(fseek(f,long(offset),SEEK_SET)) return false;
    crc=0;
    while(size) {
        size_t n=std::min<size_t>(size,buffer.size());
        if(fread(buffer.data(),1,n,f)!=n) return false;
        crc=crc32(buffer.data(),n,crc);size-=uint32_t(n);
    }
    return true;
}
}

uint32_t crc32(const void* data, size_t n, uint32_t previous) {
    static const auto table=[] {
        std::array<uint32_t,256> t{};
        for(unsigned i=0;i<256;++i) {
            uint32_t v=i;
            for(int bit=0;bit<8;++bit)v=(v>>1)^((v&1)?0xedb88320u:0);
            t[i]=v;
        }
        return t;
    }();
    auto* p=static_cast<const uint8_t*>(data);uint32_t c=~previous;
    while(n--)c=(c>>8)^table[(c^*p++)&255];
    return ~c;
}

bool inspect_rom(const std::string& path, RomInfo& info, std::string& error) {
    info={};error.clear();
    File f(fopen(path.c_str(),"rb"),fclose);
    if(!f) {error="No se puede abrir la ROM: "+path;return false;}
    std::array<uint8_t,512> h{};
    if(!file_size(f.get(),info.size) || info.size<512 || info.size>MAX_ROM || !read_at(f.get(),0,h.data(),h.size())) {
        error="ROM truncada o demasiado grande";return false;
    }
    info.code=std::string(reinterpret_cast<char*>(h.data()+12),4);info.revision=h[30];
    if(!valid_code(info.code) || h[18]!=0 || std::memcmp(h.data(),"S.MARIO64DS",10)) {
        error="El archivo no es una ROM NDS reconocida de Super Mario 64 DS";return false;
    }
    if(crc16(h.data(),0x15e)!=u16(h.data()+0x15e)) {error="CRC de cabecera incorrecto";return false;}
    uint32_t fo=u32(h.data()+0x40),fn=u32(h.data()+0x44);
    uint32_t fa=u32(h.data()+0x48),fs=u32(h.data()+0x4c);
    if(fn<8 || fn>MAX_FNT || !fs || fs%8 || fs/8>MAX_FILES ||
       !range(fo,fn,info.size) || !range(fa,fs,info.size)) {error="Tablas NitroFS no validas";return false;}
    std::vector<uint8_t> names(fn),fat(fs);
    if(!read_at(f.get(),fo,names.data(),fn) || !read_at(f.get(),fa,fat.data(),fs)) {error="Lectura NitroFS incompleta";return false;}
    info.files.resize(fs/8);
    for(size_t i=0;i<info.files.size();++i) {
        auto& e=info.files[i]; e.id=uint32_t(i);e.offset=u32(fat.data()+i*8);
        uint32_t end=u32(fat.data()+i*8+4);
        if(end<e.offset || !range(e.offset,end-e.offset,info.size)) {error="Archivo fuera de la ROM";return false;}
        e.size=end-e.offset;
    }
    unsigned dirs=u16(names.data()+6);std::set<unsigned> visited;
    if(!dirs || dirs>4096 || dirs*8>names.size() || !walk_names(names,0,0,"",info.files,visited,dirs,0) || visited.size()!=dirs) {
        error="Arbol NitroFS no valido";return false;
    }
    for(auto& e:info.files) if(e.path.empty()) e.path="__rom/file_"+std::to_string(e.id)+".bin";
    struct Segment {const char* name;unsigned offset,size;};
    const Segment segments[]={{"arm9.bin",0x20,0x2c},{"arm7.bin",0x30,0x3c},
        {"arm9_overlays.bin",0x50,0x54},{"arm7_overlays.bin",0x58,0x5c}};
    for(const auto& s:segments) {
        uint32_t off=u32(h.data()+s.offset),n=u32(h.data()+s.size);
        if(!range(off,n,info.size)) {error="Segmento ROM fuera de limites";return false;}
        if(n) info.files.push_back({0xffffffffu,off,n,0,std::string("__rom/")+s.name});
    }
    info.files.push_back({0xffffffffu,0,512,0,"__rom/header.bin"});
    std::set<std::string> paths;
    uint64_t bytes=HEADER+uint64_t(info.files.size())*RECORD;
    for(auto& e:info.files) {
        if(!safe_path(e.path) || !paths.insert(e.path).second) {error="Rutas NitroFS duplicadas o no validas";return false;}
        bytes+=e.size;
    }
    if(bytes>MAX_ROM+32u*1024*1024) {error="Recursos excesivos";return false;}
    return true;
}

bool import_rom(const std::string& rom, const std::string& pack, std::string& error, const Progress& progress) {
    RomInfo info;
    if(rom==pack) {error="La salida coincide con la ROM";return false;}
    if(!inspect_rom(rom,info,error)) return false;
    const std::string partial=pack+".part";
    // Never follow an unexpected link as an output, including one to the ROM.
    struct stat st{};
    if(lstat(partial.c_str(),&st)==0 && !S_ISREG(st.st_mode)) {error="Destino temporal no regular";return false;}
    if(lstat(pack.c_str(),&st)==0 && !S_ISREG(st.st_mode)) {error="Destino no regular";return false;}
    File in(fopen(rom.c_str(),"rb"),fclose);
    if(!in) {error="No se puede reabrir la ROM";return false;}
    struct stat src{},dst{};
    if(fstat(fileno(in.get()),&src) ||
       (stat(partial.c_str(),&dst)==0 && src.st_dev==dst.st_dev && src.st_ino==dst.st_ino) ||
       (stat(pack.c_str(),&dst)==0 && src.st_dev==dst.st_dev && src.st_ino==dst.st_ino)) {
        error="La salida coincide con la ROM";return false;
    }
    File out(fopen(partial.c_str(),"wb"),fclose);
    if(!out) {error="No se puede crear el paquete temporal";return false;}
    auto fail=[&](const char* msg) {error=msg;out.reset();remove(partial.c_str());return false;};
    std::vector<uint8_t> index(info.files.size()*RECORD,0);
    std::array<uint8_t,HEADER> header{};
    if(fwrite(header.data(),1,header.size(),out.get())!=header.size() || fwrite(index.data(),1,index.size(),out.get())!=index.size()) return fail("No hay espacio para el indice");
    uint32_t offset=HEADER+uint32_t(index.size()),done=0;
    uint64_t total64=info.size;for(auto& e:info.files)total64+=e.size;
    uint32_t total=uint32_t(total64);
    std::array<uint8_t,32768> buffer;
    if(fseek(in.get(),0,SEEK_SET))return fail("No se puede leer la ROM");
    uint32_t remain=info.size;
    while(remain) {
        size_t n=std::min<size_t>(remain,buffer.size());
        if(fread(buffer.data(),1,n,in.get())!=n) return fail("ROM incompleta");
        info.crc=crc32(buffer.data(),n,info.crc);remain-=uint32_t(n);done+=uint32_t(n);
        if(progress && !progress(done,total))return fail("Importacion cancelada");
    }
    for(size_t i=0;i<info.files.size();++i) {
        auto& e=info.files[i];uint32_t remaining=e.size,crc=0;
        if(fseek(in.get(),long(e.offset),SEEK_SET))return fail("No se puede leer un recurso");
        while(remaining) {
            size_t n=std::min<size_t>(remaining,buffer.size());
            if(fread(buffer.data(),1,n,in.get())!=n || fwrite(buffer.data(),1,n,out.get())!=n) return fail("Error al copiar recursos: lectura o espacio SD");
            crc=crc32(buffer.data(),n,crc);remaining-=uint32_t(n);done+=uint32_t(n);
            if(progress && !progress(done,total))return fail("Importacion cancelada");
        }
        auto* r=index.data()+i*RECORD;
        put32(r,e.id);put32(r+4,offset);put32(r+8,e.size);put32(r+12,crc);
        memcpy(r+16,e.path.c_str(),e.path.size());offset+=e.size;
    }
    memcpy(header.data(),"SM64PAK1",8);put32(header.data()+8,1);put32(header.data()+12,uint32_t(info.files.size()));
    memcpy(header.data()+16,info.code.data(),4);header[20]=info.revision;
    put32(header.data()+24,info.size);put32(header.data()+28,info.crc);
    put32(header.data()+32,offset);put32(header.data()+36,crc32(index.data(),index.size()));
    put32(header.data()+40,crc32(header.data(),40));
    if(fseek(out.get(),0,SEEK_SET) || fwrite(header.data(),1,header.size(),out.get())!=header.size() ||
       fwrite(index.data(),1,index.size(),out.get())!=index.size() || fflush(out.get()))return fail("Error al finalizar el paquete");
    if(fclose(out.release())) {remove(partial.c_str());error="Error al cerrar el paquete";return false;}
    // All payload CRCs must validate before the old installation is replaced.
    { AssetPack check;if(!check.open(partial,error) || !check.verify(error)) {remove(partial.c_str());return false;} }
    if(rename(partial.c_str(),pack.c_str())) {remove(partial.c_str());error="No se puede publicar el paquete";return false;}
    return true;
}

AssetPack::~AssetPack() { if(file_)fclose(file_); }
bool AssetPack::open(const std::string& path, std::string& error) {
    if(file_)fclose(file_);
    file_=nullptr;info_={};error.clear();
    File f(fopen(path.c_str(),"rb"),fclose);
    uint32_t size=0;std::array<uint8_t,HEADER> h{};
    if(!f || !file_size(f.get(),size) || !read_at(f.get(),0,h.data(),h.size())) {error="Paquete ausente o incompleto";return false;}
    uint32_t count=u32(h.data()+12);
    if(memcmp(h.data(),"SM64PAK1",8) || u32(h.data()+8)!=1 || !count || count>MAX_FILES+5 ||
       u32(h.data()+32)!=size || u32(h.data()+40)!=crc32(h.data(),40) || !range(HEADER,count*RECORD,size)) {error="Cabecera de paquete no valida";return false;}
    info_.code=std::string(reinterpret_cast<char*>(h.data()+16),4);info_.revision=h[20];
    info_.size=u32(h.data()+24);info_.crc=u32(h.data()+28);
    if(!valid_code(info_.code)) {error="Region de paquete no valida";return false;}
    std::vector<uint8_t> index(count*RECORD);
    if(!read_at(f.get(),HEADER,index.data(),index.size()) || crc32(index.data(),index.size())!=u32(h.data()+36)) {error="Indice de paquete corrupto";return false;}
    uint32_t next=HEADER+uint32_t(index.size());std::set<std::string> paths;
    for(unsigned i=0;i<count;++i) {
        auto* r=index.data()+i*RECORD;
        if(!memchr(r+16,0,256)) {error="Ruta de paquete sin terminar";return false;}
        AssetEntry e{u32(r),u32(r+4),u32(r+8),u32(r+12),reinterpret_cast<char*>(r+16)};
        if(e.offset!=next || !range(e.offset,e.size,size) || !safe_path(e.path) || !paths.insert(e.path).second) {error="Registro de paquete no valido";return false;}
        next+=e.size;info_.files.push_back(std::move(e));
    }
    if(next!=size) {error="Tamano de paquete no valido";return false;}
    file_=f.release();return true;
}
bool AssetPack::read(size_t index, std::vector<uint8_t>& data, std::string& error) {
    if(!file_ || index>=info_.files.size()) {error="Recurso ausente";return false;}
    auto& e=info_.files[index];
    // DS assets fit well below this; keep malformed input from exhausting Old 3DS.
    if(e.size>32u*1024*1024) {error="Recurso excesivo";return false;}
    data.resize(e.size);
    if(!read_at(file_,e.offset,data.data(),data.size()) || crc32(data.data(),data.size())!=e.crc) {error="Recurso corrupto: "+e.path;data.clear();return false;}
    return true;
}
bool AssetPack::verify(std::string& error) {
    if(!file_) {error="Paquete no abierto";return false;}
    for(auto& e:info_.files) {uint32_t c;if(!block_crc(file_,e.offset,e.size,c) || c!=e.crc) {error="Recurso corrupto: "+e.path;return false;}}
    return true;
}
int AssetPack::find(const std::string& path) const {
    for(size_t i=0;i<info_.files.size();++i) if(info_.files[i].path==path)return int(i);
    return -1;
}
}
