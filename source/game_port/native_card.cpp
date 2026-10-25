#include "sm64ds/game_files.hpp"
#include "sm64ds/native_runtime.hpp"
#include "sm64ds/native_video.hpp"
#include <algorithm>
#include <climits>
#include <cstring>
#include <list>
#include <string>
#include <vector>

// Native card services share the verified AssetPack with SharedFilePtr, but
// return RAW bytes. Original func_0201834c/LoadCompressedFile still recognize
// and decompress their own LZ77 files. The PC HAL's ownership boundary is
// preserved: NARC mounts own real image bytes and register their actual FAT/FNT
// through the recovered func_0204ee40; no resident flags stand in for a load.
extern "C" {
struct ArchiveRecord { void* image; void* heap; std::uint16_t base,end; const char* name; const char* path; };
extern ArchiveRecord data_0208ecf4[13];
extern void* data_020a0ea0;
extern std::uint8_t data_020a8074[];
extern void* data_020a8048;
extern std::uint8_t data_0209a048[];
extern std::int8_t data_0208eb54;
void* _ZN4Heap9_AllocateEji(void*,unsigned,int);
void _ZN4Heap11_DeallocateEPv(void*,void*);
int func_0204ee40(void*,const char*,void*);
int func_0204ee10(void*);
void func_0205cd34(void*);
int func_0205cc80(void*,const void*,unsigned);
void FS_InitFile(void*);
unsigned func_0201834c(void*,int);
int func_020184e0(void*,void*,unsigned);
}
static_assert(sizeof(ArchiveRecord)==20);

namespace {
struct FileId { void* archive; std::uint32_t id; };
static_assert(sizeof(FileId)==8);
struct Mount {
    void* record=nullptr;
    void* heap=nullptr;
    std::uint8_t* bytes=nullptr;
    std::size_t size=0;
    std::string name;
    std::vector<sm64ds::NarcMember> members;
    unsigned references=0;
    bool unmounted=false;
};
struct File { void* object; Mount* mount; std::vector<std::uint8_t> bytes; };
std::list<Mount> mounts;
std::list<File> files;
bool rom_registry_ready=false;
bool error(const std::string& message) {
    if (auto* service=sm64ds::bound_native_game_files()) service->record_error(message);
    return false;
}
std::uint16_t u16(const std::uint8_t* p) { return p[0]|std::uint16_t(p[1])<<8; }
std::uint32_t u32(const std::uint8_t* p) { return u16(p)|std::uint32_t(u16(p+2))<<16; }
void word(void* object,unsigned offset,std::uint32_t value) {
    std::memcpy(static_cast<std::uint8_t*>(object)+offset,&value,4);
}
std::uint32_t word(const void* object,unsigned offset) {
    std::uint32_t value;std::memcpy(&value,static_cast<const std::uint8_t*>(object)+offset,4);return value;
}
auto find_file(void* object) {
    return std::find_if(files.begin(),files.end(),[&](const auto& f){return f.object==object;});
}
Mount* find_mount(void* record) {
    for (auto& mount:mounts) if(mount.record==record)return &mount;
    return nullptr;
}
void retire(Mount* mount) {
    if (!mount || !mount->unmounted || mount->references)return;
    for (auto i=mounts.begin();i!=mounts.end();++i) if(&*i==mount) {
        _ZN4Heap11_DeallocateEPv(i->heap,i->record);mounts.erase(i);return;
    }
}
void close_file(std::list<File>::iterator i) {
    auto* mount=i->mount;
    if(mount)--mount->references;
    void* object=i->object;
    files.erase(i);
    word(object,8,0);word(object,0x10,0xc);word(object,0xc,word(object,0xc)&~0x31u);
    retire(mount);
}
bool raw_path(const char* path,std::vector<std::uint8_t>& bytes) {
    auto* service=sm64ds::bound_native_game_files();
    if(!service || !path)return error("Native card is not bound or path is null");
    while(*path=='/' || *path=='\\')++path;
    auto& pack=service->asset_pack();
    const auto index=pack.find(path);
    if(index<0)return error(std::string("Original card path not present: ")+path);
    std::string why;
    if(!pack.read(static_cast<std::size_t>(index),bytes,why))return error(why);
    return true;
}
// NARC BTNF uses the same Nitro directory records as the cartridge FNT.
// Numeric member lookup remains valid for archives with an empty name table.
bool archive_path(const Mount& mount,const std::string& path,std::uint32_t& file_id) {
    const auto* bytes=mount.bytes;
    std::size_t at=16,names=0,size=0;
    for(unsigned i=0;i<3;++i) {
        const auto block=u32(bytes+at+4);
        if(!std::memcmp(bytes+at,"BTNF",4)){names=at+8;size=block-8;break;}
        at+=block;
    }
    if(!names || size<8)return false;
    const auto* fnt=bytes+names;
    unsigned directory=0;
    std::size_t position=0;
    for(unsigned depth=0;depth<32;++depth) {
        if(directory*8+8>size)return false;
        std::size_t entry=u32(fnt+directory*8);
        unsigned id=u16(fnt+directory*8+4);
        const auto slash=path.find('/',position);
        const auto length=(slash==std::string::npos?path.size():slash)-position;
        bool descended=false;
        while(entry<size) {
            const unsigned tag=fnt[entry++];if(!tag)break;
            const unsigned n=tag&0x7f;
            if(!n || n>size-entry)return false;
            const bool match=n==length && !std::memcmp(fnt+entry,path.data()+position,n);
            entry+=n;
            if(tag&0x80) {
                if(size-entry<2)return false;
                const auto child=u16(fnt+entry);entry+=2;
                if(match && slash!=std::string::npos && child>=0xf000) {
                    directory=child-0xf000;position=slash+1;descended=true;break;
                }
            } else {
                if(match && slash==std::string::npos && id<mount.members.size()) {file_id=id;return true;}
                ++id;
            }
        }
        if(!descended)return false;
    }
    return false;
}
bool resolve_path(const char* input,FileId& out) {
    auto* service=sm64ds::bound_native_game_files();
    if(!service || !input)return error("Native card path resolver is not ready");
    std::string path=input;
    std::replace(path.begin(),path.end(),'\\','/');
    std::string archive="rom";
    const auto colon=path.find(':');
    if(colon!=std::string::npos){archive=path.substr(0,colon);path.erase(0,colon+1);}
    while(!path.empty()&&path.front()=='/')path.erase(0,1);
    if(archive=="rom") {
        const auto& pack=service->asset_pack();
        const int index=pack.find(path);
        if(index<0)return error("Original card path not present: "+path);
        const auto id=pack.info().files[static_cast<std::size_t>(index)].id;
        if(id>=0x8000)return error("Path does not name an original FAT file");
        out={data_020a8074,id};return true;
    }
    for(auto& mount:mounts) if(!mount.unmounted && mount.name==archive) {
        std::uint32_t id=0;
        if(!archive_path(mount,path,id))return error("NARC path not present: "+path);
        out={mount.record,id};return true;
    }
    return error("Original archive is not mounted: "+archive);
}
bool read_id(const FileId& id,std::vector<std::uint8_t>& output,Mount*& mount) {
    mount=nullptr;
    if(id.archive==data_020a8074) {
        auto* service=sm64ds::bound_native_game_files();
        if(!service)return error("Native card service is not bound");
        auto& pack=service->asset_pack();
        const auto& entries=pack.info().files;
        for(std::size_t i=0;i<entries.size();++i) if(entries[i].id==id.id && id.id<0x8000) {
            std::string why;if(pack.read(i,output,why))return true;return error(why);
        }
        return error("Original FAT file ID not present");
    }
    mount=find_mount(id.archive);
    if(!mount || mount->unmounted || id.id>=mount->members.size())return error("NARC file ID not resident");
    const auto member=mount->members[id.id];
    output.assign(mount->bytes+member.offset,mount->bytes+member.offset+member.size);return true;
}
}

extern "C" {
int sm64ds_native_register_rom_archive() {
    // Original FS_Init installs this permanent first node before checking the
    // FAT/FNT transport. FS_UnregisterArchive unlinks through its predecessor;
    // freeing a NARC as the first node would leave the list head dangling.
    // Native OpenFast/ReadFile own the pack transport, so register the actual
    // ROM identity without inventing SDK loaded flags or card-DMA callbacks.
    constexpr std::uint32_t rom_key=0x006d6f72;
    if(rom_registry_ready) {
        if(data_020a8048!=data_020a8074 || word(data_020a8074,0)!=rom_key ||
           word(data_020a8074,8)!=0)
            return error("Permanent ROM archive registry is inconsistent");
        return 1;
    }
    if(data_020a8048)
        return error("ROM archive registration must precede NARC mounts");
    if((u32(data_0209a048)&0x00ffffff)!=rom_key)
        return error("Original ROM archive name is invalid");
    func_0205cd34(data_020a8074);
    if(!func_0205cc80(data_020a8074,data_0209a048,3))
        return error("Original ROM archive registration failed");
    rom_registry_ready=true;
    return 1;
}

// Same 8-byte FSFileID returned by the recovered path wrapper. On ARM this
// aggregate reaches OpenFast in r1/r2, matching the original ROM ABI.
int func_0205d644(FileId* out,const char* path) {
    if(!out)return 0;
    FileId value{};if(!resolve_path(path,value))return 0;*out=value;return 1;
}
int func_0205d568(void* object,FileId id) {
    if(!object || !id.archive)return 0;
    std::vector<std::uint8_t> bytes;Mount* mount=nullptr;
    if(!read_id(id,bytes,mount) || bytes.size()>INT_MAX)return 0;
    auto old=find_file(object);if(old!=files.end())close_file(old);
    if(files.size()>=64)return error("Native card has too many open files");
    files.push_back({object,mount,std::move(bytes)});
    if(mount)++mount->references;
    const auto size=files.back().bytes.size();
    word(object,8,static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(id.archive)));
    word(object,0xc,(word(object,0xc)|0x10)&~0x21u);word(object,0x10,6);word(object,0x14,0);
    word(object,0x20,0);word(object,0x24,static_cast<std::uint32_t>(size));word(object,0x28,0);
    word(object,0x2c,static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(id.archive)));
    word(object,0x30,id.id);return 1;
}
// Recover the real two-register FSFileID arguments. Taking a struct through
// &a / &b in the MWCC source assumes contiguous spilled arguments, which GCC
// does not promise. These retain the original operation and failure policy.
int func_02018dc4(void* object,FileId id) {
    if(!func_0205d568(object,id))sm64ds_native_missing("Original FS_OpenFileFast failed");
    return 1;
}
unsigned func_020182ec(FileId id,int flag) {
    std::uint32_t file[0x11]{};
    FS_InitFile(file);
    func_02018dc4(file,id);
    return func_0201834c(file,flag);
}
int func_02018474(FileId id,void* destination,unsigned maximum) {
    std::uint32_t file[0x11]{};
    FS_InitFile(file);
    func_02018dc4(file,id);
    return func_020184e0(file,destination,maximum);
}
int FS_ReadFile(void* object,void* destination,int count) {
    auto i=find_file(object);
    if(i==files.end() || count<0 || (!destination && count))return -1;
    const auto position=word(object,0x28);
    if(position>i->bytes.size())return -1;
    const auto amount=std::min<std::size_t>(static_cast<unsigned>(count),i->bytes.size()-position);
    if(amount) {
        // Original LoadFileAt passes DS palette/VRAM addresses through the FS
        // API. They are display aliases, so retain the raw byte transfer while
        // resolving it through native storage instead of dereferencing DS RAM.
        const auto address=reinterpret_cast<std::uintptr_t>(destination);
        if(address>=0x04000000 && address<0x08000000) {
            if(!sm64ds::native_video_copy(static_cast<std::uint32_t>(address),
                                        i->bytes.data()+position,amount))return -1;
        } else std::memcpy(destination,i->bytes.data()+position,amount);
    }
    word(object,0x28,position+static_cast<std::uint32_t>(amount));word(object,0x10,0);word(object,0x14,0);
    word(object,0xc,word(object,0xc)&~1u);
    return static_cast<int>(amount);
}
int func_0205d688(void* object,void* destination,int count,int asynchronous) {
    (void)asynchronous; // An asynchronous request may complete synchronously.
    return FS_ReadFile(object,destination,count);
}
int FS_CloseFile(void* object) {
    auto i=find_file(object);if(i==files.end())return 0;close_file(i);return 1;
}
int func_0205d3f4(void* object) {
    // Native pack reads finish before returning; there is no DS card DMA wait.
    return object && find_file(object)!=files.end() && word(object,0x14)==0;
}
void* func_02018934(const char* name,const char* path,void* heap) {
    if(!name || !path || !heap){error("Invalid original archive mount parameters");return nullptr;}
    for(const auto& mounted:mounts) if(!mounted.unmounted && mounted.name==name) {
        error("Archive name is already mounted");return nullptr;
    }
    std::vector<std::uint8_t> bytes;
    std::vector<sm64ds::NarcMember> members;
    std::string why;
    if(!raw_path(path,bytes) || !sm64ds::parse_narc(bytes,members,why)) {
        if(!why.empty())error(why);
        return nullptr;
    }
    if(bytes.size()>INT_MAX-0x70){error("NARC image exceeds original heap allocation range");return nullptr;}
    const auto length=(static_cast<unsigned>(bytes.size())+15)&~15u;
    void* record=_ZN4Heap9_AllocateEji(heap,length+0x60,-0x10);
    if(!record){error("Original heap could not allocate the NARC image");return nullptr;}
    auto* payload=static_cast<std::uint8_t*>(record)+0x60;
    std::memcpy(payload,bytes.data(),bytes.size());
    if(!func_0204ee40(record,name,payload)) {
        _ZN4Heap11_DeallocateEPv(heap,record);error("Original NARC registration failed");return nullptr;
    }
    mounts.push_back({record,heap,payload,bytes.size(),name,std::move(members),0,false});
    return record;
}
void func_02018908(void* record,void* heap) {
    auto* mount=find_mount(record);
    if(!mount || mount->heap!=heap || mount->unmounted)
        sm64ds_native_missing("Native archive unload: unknown ownership");
    if(!func_0204ee10(record))sm64ds_native_missing("Original archive unregister failed");
    mount->unmounted=true;
    // The original table releases residency now. Open files retain their own
    // raw buffers and a reference to this record until their final close.
    retire(mount);
}
int LoadArchive(int index) {
    if(index<0 || index>=13)return error("Original archive index outside 0..12");
    auto& row=data_0208ecf4[index];
    if(!row.image) {
        row.heap=data_020a0ea0;
        row.image=func_02018934(row.name,row.path,row.heap);
    }
    return row.image!=nullptr;
}
void UnloadArchive(int index) {
    if(index<0 || index>=13)sm64ds_native_missing("Original archive unload index outside 0..12");
    auto& row=data_0208ecf4[index];
    if(!row.image)return;
    func_02018908(row.image,row.heap);row.image=nullptr;row.heap=nullptr;
}
void func_02018770() {
    // MWCC kept the just-read archive index in r0 for UnloadArchive(). GCC
    // requires it explicitly; see upstream/port/hal/stage_slot0.cpp's ABI note.
    const int index=data_0208eb54;
    if(index<0)return;
    UnloadArchive(index);
    data_0208eb54=-1;
}
} // extern "C"
