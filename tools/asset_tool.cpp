#include "sm64ds/assets.hpp"
#include <cstdio>
#include <string>
int main(int argc,char** argv) {
    if(argc<3) {fprintf(stderr,"Uso: asset_tool inspect ROM | import ROM PAK | verify PAK | list PAK\n");return 2;}
    std::string error,op=argv[1];
    if(op=="inspect") {
        sm64ds::RomInfo info;
        if(!sm64ds::inspect_rom(argv[2],info,error)) {fprintf(stderr,"%s\n",error.c_str());return 1;}
        printf("%s revision=%u size=%u entries=%zu\n",info.code.c_str(),info.revision,info.size,info.files.size());
    } else if(op=="import" && argc==4) {
        if(!sm64ds::import_rom(argv[2],argv[3],error)) {fprintf(stderr,"%s\n",error.c_str());return 1;}
        printf("Paquete extraido y verificado: %s\n",argv[3]);
    } else if(op=="verify" || op=="list") {
        sm64ds::AssetPack p;
        if(!p.open(argv[2],error) || !p.verify(error)) {fprintf(stderr,"%s\n",error.c_str());return 1;}
        printf("%s revision=%u ROM_CRC32=%08x entries=%zu verified\n",p.info().code.c_str(),p.info().revision,p.info().crc,p.info().files.size());
        if(op=="list")for(auto& e:p.info().files)printf("%u\t%u\t%s\n",e.id,e.size,e.path.c_str());
    } else return 2;
    return 0;
}
