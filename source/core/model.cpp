#include "sm64ds/model.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
namespace { sm64ds::AssetPack* current=nullptr; }
namespace ntr {
int32_t fs_size(int32_t id) {
    return current && id>=0 && size_t(id)<current->info().files.size() ? int32_t(current->info().files[id].size) : -1;
}
int32_t fs_id_by_path(const char* path) {return current ? current->find(path) : -1;}
int32_t fs_read(int32_t id,void* dst,int32_t capacity) {
    if(!current || !dst || capacity<0)return -1;
    std::vector<uint8_t> b;std::string error;
    if(!current->read(size_t(id),b,error) || b.size()>size_t(capacity))return -1;
    memcpy(dst,b.data(),b.size());return int32_t(b.size());
}
}
namespace sm64ds {
bool ModelPreview::load(AssetPack& pack,const char* path,std::string& error) {
    triangles.clear();model={};current=&pack;
    bool loaded=ntr::bmd_load_path(path,model);current=nullptr;
    if(!loaded) {error=std::string("Modelo no compatible: ")+path;return false;}
    ntr::gx_reset();
    ntr::gx_set_light(0,0.2f,-0.8f,-0.5f,0x7fff);ntr::gx_enable_lights(1);
    ntr::bmd_set_animation(nullptr);ntr::bmd_submit(model);
    size_t count=0;const auto* data=ntr::gx_polygons(count);
    if(!count || count>65536 || !ntr::gx_asset_stream_valid()) {error="Display list no compatible con el visor nativo";return false;}
    triangles.assign(data,data+count);
    float lo[3]={INFINITY,INFINITY,INFINITY},hi[3]={-INFINITY,-INFINITY,-INFINITY};
    for(auto& t:triangles)for(auto& v:t.v) {
        float p[]={v.vx,v.vy,v.vz};
        for(int i=0;i<3;++i) {if(!std::isfinite(p[i])) {error="Geometria no finita";return false;}lo[i]=std::min(lo[i],p[i]);hi[i]=std::max(hi[i],p[i]);}
    }
    radius=0;
    for(int i=0;i<3;++i) {center[i]=(hi[i]+lo[i])*0.5f;radius=std::max(radius,(hi[i]-lo[i])*0.5f);}
    if(radius<1e-5f) {error="Modelo degenerado";return false;}
    return true;
}
}
