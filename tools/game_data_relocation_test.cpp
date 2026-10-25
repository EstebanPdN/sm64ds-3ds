#include "sm64ds/game_data.hpp"
#include <cstdio>
#include <cstring>

extern "C" uint32_t data_ov007_02103290[6];
extern "C" int32_t data_ov007_02103260;
extern "C" uint32_t data_ov007_02103254;
extern "C" uint32_t data_ov007_02103258;

int main(int argc,char** argv) {
    using namespace sm64ds::game_data;
    auto check=[](bool okay,const char* message) {
        if(!okay) std::fprintf(stderr,"FAIL %s\n",message);
        return okay;
    };
    const std::vector<uint8_t> source={0,1,0,2,0,2,0,2};
    std::vector<Relocation> relocs={
        {0,0x02000100,"arm9","array",12,SymbolKind::data},
        {4,0x02000200,"ov007","callback",0,SymbolKind::function}};
    auto resolver=[](const char* module,const char* symbol,NativeSymbol& out) {
        if(!std::strcmp(module,"arm9") && !std::strcmp(symbol,"array")) {
            out={0x14000000,16,SymbolKind::data};return true;
        }
        if(!std::strcmp(module,"ov007") && !std::strcmp(symbol,"callback")) {
            out={0x00100001,0,SymbolKind::function};return true; // Thumb tag preserved
        }
        return false;
    };
    std::vector<uint8_t> output={99};std::string error;
    if(!check(relocate_block(source,relocs,resolver,output,error),"valid relocations")) return 1;
    if(!check(output==std::vector<uint8_t>({12,0,0,0x14,1,0,0x10,0}),"interior offset and Thumb bit")) return 1;
    const auto good=output;
    auto bad=relocs;bad[1].module="ov006";
    if(!check(!relocate_block(source,bad,resolver,output,error)&&output==good,"module identity and atomic failure")) return 1;
    bad=relocs;bad[0].addend=16;
    if(!check(!relocate_block(source,bad,resolver,output,error),"one-past data pointer")) return 1;
    bad=relocs;bad[1].addend=4;
    if(!check(!relocate_block(source,bad,resolver,output,error),"function interior offset")) return 1;
    bad=relocs;bad[0].original_pointer++;
    if(!check(!relocate_block(source,bad,resolver,output,error),"wrong source pointer")) return 1;
    bad=relocs;bad[1].offset=0;
    if(!check(!relocate_block(source,bad,resolver,output,error),"overlapping relocation")) return 1;
    auto overflow=[](const char*,const char*,NativeSymbol& out) {out={0xfffffff8,16,SymbolKind::data};return true;};
    if(!check(!relocate_block(source,{relocs[0]},overflow,output,error),"pointer addition overflow")) return 1;
    auto identity=[](const char*,const char*,NativeSymbol& out) {out={0x020000f4,16,SymbolKind::data};return true;};
    if(!check(!relocate_block(source,{relocs[0]},identity,output,error),"raw DS passthrough")) return 1;
    if(argc==2) {
        sm64ds::AssetPack pack;
        if(!check(pack.open(argv[1],error),"open real ROM pack")) return 1;
        unsigned index=0;
        const char* expected[]={"func_ov007_020ccab4","func_ov007_020cca98",
            "func_ov007_020cca80","func_ov007_020cca74","func_ov007_020cca68",
            "func_ov007_020cc600","func_ov007_020c3e4c","func_ov007_020c3e64"};
        auto menu_resolver=[&](const char* module,const char* symbol,NativeSymbol& out) {
            if(std::strcmp(module,"ov007")) return false;
            if(index>=8 || std::strcmp(symbol,expected[index])) return false;
            out={0x00102000u+4u*index++,0,SymbolKind::function};return true;
        };
        if(!check(initialize_menu_callbacks(pack,menu_resolver,error),error.c_str())) return 1;
        if(!check(index==8 && data_ov007_02103260==-1,"eight callbacks and original initial state")) return 1;
        if(!check(data_ov007_02103254==0x00102018 && data_ov007_02103258==0x0010201c,
                  "default allocation callbacks")) return 1;
        uint32_t before[6];std::memcpy(before,data_ov007_02103290,sizeof before);
        for(unsigned i=0;i<6;++i)
            if(!check(before[i]==0x00102000u+4u*i,"actual table relocated slot")) return 1;
        auto missing=[](const char*,const char*,NativeSymbol&) {return false;};
        if(!check(!initialize_menu_callbacks(pack,missing,error) &&
                  !std::memcmp(before,data_ov007_02103290,sizeof before),"unresolved callback preserves table")) return 1;
        std::printf("%s rev%u original menu callback record extraction/relocation PASS; callbacks not executed\n",
                    pack.info().code.c_str(),pack.info().revision);
    }
    std::puts("relocation bounds/module/transaction tests PASS");
    return 0;
}
