#include "sm64ds/game_data.hpp"
#include "rom_profile.hpp"
#include <algorithm>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace sm64ds::game_data {
namespace {
uint16_t u16(const uint8_t* p) { return uint16_t(p[0]) | uint16_t(p[1])<<8; }
uint32_t u32(const uint8_t* p) { return uint32_t(u16(p)) | uint32_t(u16(p+2))<<16; }
bool fail(std::string& error, const char* message) { error=message; return false; }
bool read_named(AssetPack& pack, const char* name, std::vector<uint8_t>& out,
                std::string& error) {
    int index=pack.find(name);
    return index>=0 ? pack.read(size_t(index),out,error)
                    : fail(error,"Falta un segmento de la ROM en assets.pak");
}
constexpr uint32_t max_image=4u*1024u*1024u;
}

bool OverlayImage::range(uint32_t address, size_t length, size_t& offset) const {
    if(address<base) return false;
    offset=address-base;
    return offset<=bytes.size() && length<=bytes.size()-offset;
}

bool decode_blz(const std::vector<uint8_t>& packed, uint32_t expected_size,
                std::vector<uint8_t>& output, std::string& error) {
    if(packed.size()<8 || expected_size>max_image || expected_size<packed.size())
        return fail(error,"Tamano BLZ no valido");
    const uint32_t footer=u32(packed.data()+packed.size()-8);
    const size_t header=footer>>24, compressed=footer&0xffffff;
    const uint32_t extra=u32(packed.data()+packed.size()-4);
    if(header<8 || header>compressed || compressed>packed.size() ||
       uint64_t(packed.size())+extra!=expected_size)
        return fail(error,"Cabecera BLZ incompatible con la tabla de overlays");
    for(size_t i=packed.size()-header;i<packed.size()-8;++i)
        if(packed[i]!=0xff) return fail(error,"Relleno BLZ no valido");
    const size_t begin=packed.size()-compressed;
    size_t source=packed.size()-header, dest=expected_size;
    std::vector<uint8_t> decoded(expected_size);
    std::copy_n(packed.begin(),begin,decoded.begin());
    while(dest>begin) {
        if(source<=begin) return fail(error,"BLZ truncado antes de sus indicadores");
        uint8_t flags=packed[--source];
        for(unsigned bit=0;bit<8 && dest>begin;++bit,flags<<=1) {
            if(flags&0x80) {
                if(source-begin<2) return fail(error,"BLZ truncado en una referencia");
                const unsigned hi=packed[--source],lo=packed[--source];
                const size_t length=(hi>>4)+3, distance=((hi&15)<<8)+lo+3;
                if(length>dest-begin || distance>decoded.size()-dest)
                    return fail(error,"Referencia BLZ fuera de limites");
                for(size_t n=0;n<length;++n) {
                    --dest;
                    decoded[dest]=decoded[dest+distance];
                }
            } else {
                if(source<=begin) return fail(error,"BLZ truncado en un literal");
                decoded[--dest]=packed[--source];
            }
        }
    }
    if(source!=begin) return fail(error,"BLZ contiene bytes comprimidos sobrantes");
    output=std::move(decoded);
    return true;
}

bool load_overlay(AssetPack& pack, uint32_t id, OverlayImage& result,
                  std::string& error) {
    std::vector<uint8_t> table;
    if(!read_named(pack,"__rom/arm9_overlays.bin",table,error)) return false;
    if(table.empty() || table.size()%32 || table.size()>4096*32)
        return fail(error,"Tabla de overlays no valida");
    const uint8_t* row=nullptr;
    for(size_t off=0;off<table.size();off+=32) {
        if(u32(table.data()+off)==id) {
            if(row) return fail(error,"ID de overlay duplicado");
            row=table.data()+off;
        }
    }
    if(!row) return fail(error,"Overlay ausente");
    OverlayImage image;
    image.id=id; image.base=u32(row+4); image.bss_size=u32(row+12);
    const uint32_t ram_size=u32(row+8), file_id=u32(row+24), flags=u32(row+28);
    if(image.base<0x02000000 || ram_size>max_image || image.bss_size>max_image ||
       uint64_t(image.base)+ram_size+image.bss_size>0x02400000)
        return fail(error,"Rango de overlay fuera de la RAM DS");
    size_t asset=pack.info().files.size();
    for(size_t i=0;i<pack.info().files.size();++i)
        if(pack.info().files[i].id==file_id) {
            if(asset!=pack.info().files.size()) return fail(error,"ID FAT duplicado");
            asset=i;
        }
    if(asset==pack.info().files.size()) return fail(error,"Archivo de overlay ausente");
    std::vector<uint8_t> packed;
    if(!pack.read(asset,packed,error)) return false;
    if(flags&(1u<<24)) {
        if((flags&0xffffff)!=packed.size())
            return fail(error,"Longitud comprimida de overlay incorrecta");
        if(!decode_blz(packed,ram_size,image.bytes,error)) return false;
    } else {
        if(packed.size()!=ram_size) return fail(error,"Longitud de overlay incorrecta");
        image.bytes=std::move(packed);
    }
    // BSS is intentionally not appended: it contains runtime state, not ROM data.
    result=std::move(image);
    return true;
}

bool load_arm9(AssetPack& pack, OverlayImage& result, std::string& error) {
    std::vector<uint8_t> header,packed;
    if(!read_named(pack,"__rom/header.bin",header,error) ||
       !read_named(pack,"__rom/arm9.bin",packed,error)) return false;
    if(header.size()<0x30 || packed.size()<8 || packed.size()!=u32(header.data()+0x2c))
        return fail(error,"Segmento ARM9 incompatible con su cabecera");
    OverlayImage image;
    image.id=std::numeric_limits<uint32_t>::max();
    image.base=u32(header.data()+0x28);
    const uint32_t extra=u32(packed.data()+packed.size()-4);
    const uint64_t decoded_size=uint64_t(packed.size())+extra;
    if(image.base<0x02000000 || decoded_size>max_image || image.base+decoded_size>0x02400000)
        return fail(error,"ARM9 fuera del rango de RAM DS");
    // This entry point is deliberately limited to the investigated compressed
    // ASME0/ASMP0 image shapes. Appended ARM9 trailers/uncompressed variants need an
    // explicit format profile; guessing compression from arbitrary bytes is unsafe.
    if(!rom_profile(pack.info()))
        return fail(error,"Perfil ARM9 regional aun no implementado");
    if(!decode_blz(packed,uint32_t(decoded_size),image.bytes,error)) return false;
    result=std::move(image);
    return true;
}

bool load_handles(AssetPack& pack, HandleTable& result, std::string& error) {
    OverlayImage image;
    if(!load_overlay(pack,0,image,error)) return false;
    std::unordered_map<std::string,size_t> by_path;
    for(size_t i=0;i<pack.info().files.size();++i) {
        const auto& item=pack.info().files[i];
        if(item.path.compare(0,6,"__rom/")!=0) by_path.emplace(item.path,i);
    }
    std::vector<HandleTable> matches;
    HandleTable run;
    auto finish=[&] {
        // SM64DS's catalog is one large contiguous table. Short pointer lists
        // in code/data do not establish handle numbering and are rejected.
        if(run.asset_indices.size()>=1024) matches.push_back(std::move(run));
        run={};
    };
    for(size_t off=0;off+4<=image.bytes.size();off+=4) {
        size_t str_offset=0;
        size_t index=pack.info().files.size();
        if(image.range(u32(image.bytes.data()+off),1,str_offset)) {
            const uint8_t* begin=image.bytes.data()+str_offset;
            size_t available=std::min(size_t(256),image.bytes.size()-str_offset);
            const auto* end=static_cast<const uint8_t*>(std::memchr(begin,0,available));
            if(end) {
                auto found=by_path.find(std::string(reinterpret_cast<const char*>(begin),end-begin));
                if(found!=by_path.end()) index=found->second;
            }
        }
        if(index==pack.info().files.size()) finish();
        else {
            if(run.asset_indices.empty()) run.original_address=image.base+uint32_t(off);
            run.asset_indices.push_back(index);
        }
    }
    finish();
    if(matches.size()!=1) return fail(error,"Catalogo de handles ausente o ambiguo");
    std::unordered_set<size_t> unique(matches[0].asset_indices.begin(),matches[0].asset_indices.end());
    if(unique.size()!=matches[0].asset_indices.size())
        return fail(error,"Catalogo de handles con entradas duplicadas");
    result=std::move(matches[0]);
    return true;
}

namespace {
bool object_table(LevelData& level, uint32_t address, int area) {
    size_t off;
    if(!level.image.range(address,8,off)) return false;
    const auto& bytes=level.image.bytes;
    uint16_t count=u16(bytes.data()+off);
    uint32_t entries=u32(bytes.data()+off+4);
    if(count>1024 || !level.image.range(entries,size_t(count)*8,off)) return false;
    for(size_t i=0;i<count;++i) {
        const auto* p=bytes.data()+off+i*8;
        ObjectGroup group;
        group.type=p[0]&31; group.scenario=p[0]>>5; group.count=p[1];
        group.area=area; group.original_address=u32(p+4);
        if(group.type>14) return false;
        size_t record_off;
        if(group.count && !level.image.range(group.original_address,1,record_off)) return false;
        level.groups.push_back(group);
        if(group.type==1 && group.count) {
            if(!level.image.range(group.original_address,size_t(group.count)*16,record_off)) return false;
            for(unsigned n=0;n<group.count;++n) {
                const auto* e=bytes.data()+record_off+n*16;
                Entrance entrance;
                entrance.object=u16(e); entrance.parameter=u16(e+14);
                entrance.area=area; entrance.scenario=group.scenario;
                for(unsigned axis=0;axis<3;++axis) {
                    entrance.position[axis]=int16_t(u16(e+2+axis*2));
                    entrance.rotation[axis]=int16_t(u16(e+8+axis*2));
                }
                level.entrances.push_back(entrance);
            }
        }
    }
    return true;
}
}

bool load_level(AssetPack& pack, uint32_t overlay, const HandleTable& handles,
                const std::string& model_path, LevelData& result,
                std::string& error) {
    for(size_t index:handles.asset_indices)
        if(index>=pack.info().files.size()) return fail(error,"Indice de handle fuera de limites");
    OverlayImage image;
    if(!load_overlay(pack,overlay,image,error)) return false;
    std::vector<size_t> matches;
    for(size_t off=0;off+28<=image.bytes.size();off+=4) {
        uint16_t model=u16(image.bytes.data()+off+8);
        if(model<handles.asset_indices.size() &&
           pack.info().files[handles.asset_indices[model]].path==model_path) matches.push_back(off);
    }
    std::vector<LevelData> valid;
    for(size_t off:matches) {
        LevelData candidate;
        candidate.image=image;
        candidate.original_address=image.base+uint32_t(off);
        const auto* p=image.bytes.data()+off;
        bool okay=true;
        for(unsigned i=0;i<4;++i) {
            candidate.handles[i]=u16(p+8+i*2);
            okay=okay && candidate.handles[i]<handles.asset_indices.size();
        }
        if(!okay) continue;
        auto path=[&](unsigned i)->const std::string& {
            return pack.info().files[handles.asset_indices[candidate.handles[i]]].path;
        };
        const std::string folder=model_path.substr(0,model_path.find_last_of('/')+1);
        if(path(1).compare(0,folder.size(),folder) || path(1).size()<4 ||
           path(1).substr(path(1).size()-4)!=".kcl") continue;
        candidate.clps_address=u32(p);
        size_t clps;
        if(!image.range(candidate.clps_address,8,clps) ||
           std::memcmp(image.bytes.data()+clps,"CLPS",4)) continue;
        const size_t clps_stride=u16(image.bytes.data()+clps+4);
        const size_t clps_count=u16(image.bytes.data()+clps+6);
        if(clps_stride!=8 || !image.range(candidate.clps_address,8+clps_stride*clps_count,clps)) continue;
        if(!object_table(candidate,u32(p+4),-1)) continue;
        const uint8_t areas=p[20];
        size_t tables;
        if(areas>64 || !image.range(u32(p+16),size_t(areas)*12,tables)) continue;
        for(unsigned i=0;i<areas && okay;++i) {
            const uint32_t table=u32(image.bytes.data()+tables+i*12);
            if(table) okay=object_table(candidate,table,int(i));
        }
        if(okay) valid.push_back(std::move(candidate));
    }
    if(valid.size()!=1) return fail(error,"Descriptor de nivel ausente o ambiguo");
    result=std::move(valid[0]);
    return true;
}
} // namespace sm64ds::game_data
