// Host contract for the exact same streaming bootstrap included in the ARM build.
// --test ROM FRESH_CACHE_DIRECTORY CANONICAL_REPO runs fault/recovery coverage.
// ROM CACHE_DIRECTORY [--cancel-after N] performs one installation and prints JSON.
#include "rom_bootstrap.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {
bool forbid_game_heap=false;
unsigned forbidden_game_allocations=0,guarded_ensure_calls=0;
struct ForbidGameHeap {
    bool previous;
    ForbidGameHeap():previous(forbid_game_heap){forbid_game_heap=true;++guarded_ensure_calls;}
    ~ForbidGameHeap(){forbid_game_heap=previous;}
};
}
// The port's global new uses the DS heap before it exists at this boot stage.
// Abort on any such allocation while ensure runs, including its return value.
void* operator new(std::size_t bytes) {
    if(forbid_game_heap){++forbidden_game_allocations;std::fputs("FAIL: ROM bootstrap touched the unavailable DS/global C++ heap\n",stderr);std::abort();}
    if(void* p=std::malloc(bytes?bytes:1))return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes){return ::operator new(bytes);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t)noexcept{std::free(p);}

namespace {
void require(bool condition,const boot_memory::String& what) {
    if (!condition) throw std::runtime_error(what.c_str());
}
bool exists(const boot_memory::String& path) {
    struct stat st{}; return ::lstat(path.c_str(),&st)==0;
}
boot_memory::String root(const boot_memory::String& cache) {
    return cache+"/"+rom_install_manifest::schema_id+"/repo";
}
void write_text(const boot_memory::String& path,const boot_memory::String& text) {
    FILE* f=std::fopen(path.c_str(),"wb"); require(f!=nullptr,"open "+path);
    const bool wrote=std::fwrite(text.data(),1,text.size(),f)==text.size();
    const bool closed=std::fclose(f)==0; require(wrote && closed,"write "+path);
}
boot_memory::String read_text(const boot_memory::String& path) {
    FILE* f=std::fopen(path.c_str(),"rb"); require(f!=nullptr,"open "+path);
    boot_memory::String out; char buffer[1024]; size_t got;
    while ((got=std::fread(buffer,1,sizeof(buffer),f))!=0) out.append(buffer,got);
    const bool good=!std::ferror(f); const bool closed=std::fclose(f)==0;
    require(good && closed,"read "+path); return out;
}
uint64_t compare(const boot_memory::String& a,const boot_memory::String& b) {
    FILE* left=std::fopen(a.c_str(),"rb"); require(left!=nullptr,"open "+a);
    FILE* right=std::fopen(b.c_str(),"rb"); require(right!=nullptr,"open "+b);
    std::array<unsigned char,65536> x{},y{}; uint64_t bytes=0;
    bool equal=true,good=true;
    for (;;) {
        const size_t n=std::fread(x.data(),1,x.size(),left);
        const size_t m=std::fread(y.data(),1,y.size(),right);
        if (n!=m || std::memcmp(x.data(),y.data(),n)!=0) { equal=false; break; }
        bytes+=n; if (!n) break;
    }
    good=!std::ferror(left) && !std::ferror(right);
    const bool close_left=std::fclose(left)==0,close_right=std::fclose(right)==0;
    require(equal && good && close_left && close_right,"byte mismatch: "+a+" vs "+b);
    return bytes;
}
uint64_t compare_all(const boot_memory::String& output,const boot_memory::String& canonical) {
    uint64_t bytes=0;
    for (const auto& file:rom_install_manifest::files)
        bytes+=compare(output+"/"+file.path,canonical+"/"+file.path);
    require(bytes==rom_install_manifest::total_bytes,"total compared bytes");
    return bytes;
}
void copy_file(const boot_memory::String& from,const boot_memory::String& to) {
    FILE* in=std::fopen(from.c_str(),"rb"); require(in!=nullptr,"open copy source");
    FILE* out=std::fopen(to.c_str(),"wb"); require(out!=nullptr,"open copy destination");
    std::array<unsigned char,65536> buffer{}; bool good=true;
    while (const size_t got=std::fread(buffer.data(),1,buffer.size(),in)) {
        if (std::fwrite(buffer.data(),1,got,out)!=got) { good=false; break; }
    }
    good=good && !std::ferror(in);
    const bool closed_in=std::fclose(in)==0,closed_out=std::fclose(out)==0;
    require(good && closed_in && closed_out,"copy ROM");
}
void flip(const boot_memory::String& path,long at) {
    FILE* f=std::fopen(path.c_str(),"r+b"); require(f!=nullptr,"open mutate");
    require(std::fseek(f,at,SEEK_SET)==0,"seek mutate"); const int byte=std::fgetc(f);
    require(byte!=EOF,"read mutate"); require(std::fseek(f,at,SEEK_SET)==0,"seek mutate write");
    require(std::fputc(byte^0x5a,f)!=EOF,"write mutate"); require(std::fclose(f)==0,"close mutate");
}
struct Monitor {
    const char* cancel_phase=nullptr;
    unsigned cancel_file=0;
    uint64_t cancel_bytes=0;
    uint64_t calls=0;
    bool saw_rom=false,saw_extract=false,saw_cache=false,saw_finish=false;
    static bool update(const rom_bootstrap::Progress& p,void* context) {
        auto& self=*static_cast<Monitor*>(context); ++self.calls;
        require(p.done<=p.total,"bounded progress bytes");
        require(p.files_done<=p.files_total,"bounded progress files");
        require(p.files_total==rom_install_manifest::file_count,"progress file count");
        if (std::strcmp(p.phase,"verify-rom")==0) self.saw_rom=true;
        else if (std::strcmp(p.phase,"extract")==0) self.saw_extract=true;
        else if (std::strcmp(p.phase,"verify-cache")==0) self.saw_cache=true;
        else if (std::strcmp(p.phase,"finish")==0) self.saw_finish=true;
        else require(false,"known progress phase");
        return !self.cancel_phase || std::strcmp(p.phase,self.cancel_phase)!=0 ||
               p.files_done<self.cancel_file || p.done<self.cancel_bytes;
    }
};
rom_bootstrap::Result install(const boot_memory::String& rom,const boot_memory::String& cache,Monitor* monitor=nullptr) {
    ForbidGameHeap no_ds_heap;
    return rom_bootstrap::ensure(rom.c_str(),cache.c_str(),monitor?Monitor::update:nullptr,monitor);
}
void primitive_tests() {
    const unsigned char abc[]={'a','b','c'};
    const unsigned char expected[20]={0xa9,0x99,0x3e,0x36,0x47,0x06,0x81,0x6a,0xba,0x3e,
        0x25,0x71,0x78,0x50,0xc2,0x6c,0x9c,0xd0,0xd8,0x9d};
    rom_bootstrap::detail::Sha1 sha;
    sha.update(abc,1); sha.update(abc+1,2); const auto sum=sha.finish();
    require(std::memcmp(sum.data(),expected,20)==0,"SHA-1 published abc vector");
    const unsigned char empty_expected[20]={0xda,0x39,0xa3,0xee,0x5e,0x6b,0x4b,0x0d,0x32,0x55,
        0xbf,0xef,0x95,0x60,0x18,0x90,0xaf,0xd8,0x07,0x09};
    rom_bootstrap::detail::Sha1 empty; const auto empty_sum=empty.finish();
    require(std::memcmp(empty_sum.data(),empty_expected,20)==0,"SHA-1 published empty vector");
    const unsigned char digits[]={'1','2','3','4','5','6','7','8','9'};
    require(rom_bootstrap::detail::crc(digits,sizeof(digits))==0xcbf43926u,"CRC-32 published vector");
    for (const char* path:{"","/abs","../a","a/../b","a//b","a/","a/./b","sdmc:/a","a\\b","a\nfile"})
        require(!rom_bootstrap::detail::safe_relative(path),boot_memory::String("unsafe path rejected: ")+path);
    require(rom_bootstrap::detail::safe_relative("extracted/dsd/data/a.bmd"),"normal relative path");
}
int suite(const boot_memory::String& rom,const boot_memory::String& base,const boot_memory::String& canonical) {
    primitive_tests();
    require(!exists(base),"test output directory must be fresh; never remove existing evidence");
    require(::mkdir(base.c_str(),0777)==0,"create test evidence root");
    const boot_memory::String main=base+"/main";
    Monitor first;
    const auto initial=install(rom,main,&first);
    require(initial.ok,"initial preparation: "+initial.error);
    require(initial.prepared && initial.written==rom_install_manifest::file_count && initial.reused==0,"first preparation counts");
    require(first.saw_rom && first.saw_extract && first.saw_cache && first.saw_finish,"first progress phases");
    require(exists(initial.root+"/.ready"),"first ready marker");
    uint64_t compared=compare_all(initial.root,canonical);
    const auto second=install(rom,main);
    require(second.ok && !second.prepared && second.written==0 && second.reused==rom_install_manifest::file_count,"reboot reuses exact cache");
    // Values adjacent to the versioned repo belong to the user, not bootstrap.
    write_text(main+"/user-test.sav","private save sentinel\n");
    write_text(main+"/settings.ini","settings sentinel\n");
    write_text(main+"/rom.nds","ROM sentinel\n");
    const auto& missing=rom_install_manifest::files[0];
    const auto& truncated=rom_install_manifest::files[1];
    const auto& corrupt=rom_install_manifest::files[2];
    require(std::remove((initial.root+"/"+missing.path).c_str())==0,"remove missing test entry");
    write_text(initial.root+"/"+truncated.path,"short");
    flip(initial.root+"/"+corrupt.path,0);
    write_text(initial.root+"/"+missing.path+".part","interrupted bytes");
    const auto repaired=install(rom,main);
    require(repaired.ok && repaired.prepared && repaired.written==3 && repaired.reused==rom_install_manifest::file_count-3,"repair missing/truncated/same-size corruption");
    compared+=compare_all(initial.root,canonical);
    require(read_text(main+"/user-test.sav")=="private save sentinel\n","save preserved");
    require(read_text(main+"/settings.ini")=="settings sentinel\n","settings preserved");
    require(read_text(main+"/rom.nds")=="ROM sentinel\n","unrelated ROM preserved");
    // Cancel while a new file is incomplete; only that private .part may remain.
    const boot_memory::String interrupted=base+"/interrupted";
    Monitor stop; stop.cancel_phase="extract"; stop.cancel_file=3; stop.cancel_bytes=1;
    const auto partial=install(rom,interrupted,&stop);
    require(!partial.ok && partial.written==3,"cancel extraction after complete files");
    require(!exists(root(interrupted)+"/.ready"),"no ready marker after extraction cancellation");
    const auto resumed=install(rom,interrupted);
    require(resumed.ok && resumed.reused==3 && resumed.written==rom_install_manifest::file_count-3,"resume partial preparation");
    require(!exists(resumed.root+"/"+rom_install_manifest::files[3].path+".part"),"partial file promoted on resume");
    compared+=compare_all(resumed.root,canonical);
    // Cancellation while revalidating an old cache must not leave ready trusted.
    Monitor revalidate; revalidate.cancel_phase="verify-cache"; revalidate.cancel_file=5;
    const auto canceled_reuse=install(rom,main,&revalidate);
    require(!canceled_reuse.ok && !exists(initial.root+"/.ready"),"revalidation cancellation removes ready");
    const auto revalidated=install(rom,main);
    require(revalidated.ok && revalidated.prepared && revalidated.written==0 && revalidated.reused==rom_install_manifest::file_count,"all files reused after canceled revalidation");
    Monitor final_stop; final_stop.cancel_phase="finish";
    const auto no_marker=install(rom,main,&final_stop);
    require(!no_marker.ok && !exists(initial.root+"/.ready"),"finish cancellation has no ready marker");
    const auto finish_again=install(rom,main);
    require(finish_again.ok && finish_again.written==0,"resume before marker publication");
    // Exact ROM validation always precedes any cache creation.
    const auto absent=install(base+"/absent.nds",base+"/absent-cache");
    require(!absent.ok && !exists(base+"/absent-cache"),"missing ROM rejected without cache writes");
    write_text(base+"/short.nds","not a ROM");
    const auto short_rom=install(base+"/short.nds",base+"/short-cache");
    require(!short_rom.ok && !exists(base+"/short-cache"),"incorrect ROM size rejected before cache");
    copy_file(rom,base+"/wrong-revision.nds"); flip(base+"/wrong-revision.nds",77777);
    const auto wrong=install(base+"/wrong-revision.nds",base+"/wrong-cache");
    require(!wrong.ok && !exists(base+"/wrong-cache"),"same-size wrong ROM rejected before cache");
    // Invalid ROM cannot alter an existing complete cache or its ready marker.
    const boot_memory::String old_marker=read_text(initial.root+"/.ready");
    const auto wrong_existing=install(base+"/wrong-revision.nds",main);
    require(!wrong_existing.ok && read_text(initial.root+"/.ready")==old_marker,"invalid ROM preserves existing ready cache");
    Monitor hash_stop; hash_stop.cancel_phase="verify-rom"; hash_stop.cancel_bytes=65536;
    const auto hash_canceled=install(rom,base+"/hash-canceled",&hash_stop);
    require(!hash_canceled.ok && !exists(base+"/hash-canceled"),"hash cancellation precedes directory creation");
    // A non-directory cache path and a hostile host symlink fail closed.
    write_text(base+"/file-cache","keep this file");
    const auto file_cache=install(rom,base+"/file-cache");
    require(!file_cache.ok && read_text(base+"/file-cache")=="keep this file","non-directory cache base preserved");
    require(::symlink(main.c_str(),(base+"/symlink-cache").c_str())==0,"create host symlink fixture");
    const auto symlink_cache=install(rom,base+"/symlink-cache");
    require(!symlink_cache.ok,"cache symlink rejected");
    require(read_text(initial.root+"/.ready")==old_marker,"symlink target untouched");
    // A symlink inside an otherwise valid cache must fail even if its bytes match.
    const boot_memory::String asset_parent=initial.root+"/build/assets";
    const boot_memory::String real_assets=initial.root+"/build/assets-real";
    require(std::rename(asset_parent.c_str(),real_assets.c_str())==0,"move internal symlink fixture");
    require(::symlink(real_assets.c_str(),asset_parent.c_str())==0,"create internal symlink fixture");
    const auto linked_parent=install(rom,main);
    require(!linked_parent.ok && !exists(initial.root+"/.ready"),"internal parent symlink rejected before reuse");
    require(std::remove(asset_parent.c_str())==0,"remove internal test symlink");
    require(std::rename(real_assets.c_str(),asset_parent.c_str())==0,"restore internal test directory");
    const auto restored=install(rom,main);
    require(restored.ok && restored.written==0 && restored.reused==rom_install_manifest::file_count,"reuse after symlink fixture restoration");
    // A non-file scratch target fails without touching it or publishing ready.
    const boot_memory::String blocked_path=initial.root+"/"+missing.path;
    require(std::remove(blocked_path.c_str())==0,"remove scratch collision fixture final");
    require(::mkdir((blocked_path+".part").c_str(),0777)==0,"create scratch collision directory");
    const auto blocked=install(rom,main);
    require(!blocked.ok && !exists(initial.root+"/.ready") && exists(blocked_path+".part"),"non-file scratch collision fails closed");
    require(::rmdir((blocked_path+".part").c_str())==0,"remove empty test scratch directory");
    const auto unblocked=install(rom,main);
    require(unblocked.ok && unblocked.written==1,"repair after scratch collision removed");
    const uint64_t scratch_compared=compare(blocked_path,canonical+"/"+missing.path);
    require(scratch_compared==missing.size,"scratch recovery canonical bytes");
    compared+=scratch_compared;
    std::printf("{\"status\":\"passed\",\"schema_id\":\"%s\",\"files\":%zu,\"bytes_per_cache\":%llu,"
        "\"canonical_compared_bytes\":%llu,\"first_written\":%u,\"reboot_reused\":%u,\"repaired\":%u,"
        "\"resumed_reused\":%u,\"progress_calls_first\":%llu,\"cases\":21,"
        "\"DS_global_heap_allocations_during_ensure\":%u,\"guarded_ensure_calls\":%u,\"physical_hardware\":false}\n",
        rom_install_manifest::schema_id,rom_install_manifest::file_count,
        static_cast<unsigned long long>(rom_install_manifest::total_bytes),
        static_cast<unsigned long long>(compared),initial.written,second.reused,repaired.written,resumed.reused,
        static_cast<unsigned long long>(first.calls),forbidden_game_allocations,guarded_ensure_calls);
    return 0;
}
void print_json(const rom_bootstrap::Result& r) {
    auto escaped=[](const boot_memory::String& value) {
        boot_memory::String out;
        for (unsigned char c:value) {
            if (c=='"' || c=='\\') { out+='\\'; out+=char(c); }
            else if (c=='\n') out+="\\n";
            else if (c=='\r') out+="\\r";
            else if (c=='\t') out+="\\t";
            else if (c>=32) out+=char(c);
        }
        return out;
    };
    std::printf("{\"ok\":%s,\"prepared\":%s,\"written\":%u,\"reused\":%u,\"root\":\"%s\",\"error\":\"%s\"}\n",
        r.ok?"true":"false",r.prepared?"true":"false",r.written,r.reused,escaped(r.root).c_str(),escaped(r.error).c_str());
}
} // namespace
int main(int argc,char** argv) {
    try {
        if (argc==5 && std::strcmp(argv[1],"--test")==0) return suite(argv[2],argv[3],argv[4]);
        if (argc==3 || (argc==5 && std::strcmp(argv[3],"--cancel-after")==0)) {
            Monitor monitor;
            if (argc==5) { monitor.cancel_phase="extract"; monitor.cancel_file=unsigned(std::strtoul(argv[4],nullptr,10)); monitor.cancel_bytes=1; }
            const auto result=install(argv[1],argv[2],&monitor); print_json(result); return result.ok?0:1;
        }
        std::fprintf(stderr,"Usage: %s --test ROM FRESH_CACHE_DIRECTORY CANONICAL_REPO\n       %s ROM CACHE_DIRECTORY [--cancel-after N]\n",argv[0],argv[0]);
        return 2;
    } catch (const std::exception& error) {
        std::fprintf(stderr,"FAIL: %s\n",error.what()); return 1;
    }
}
