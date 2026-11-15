// Offline census only. Reuses the renderer's actual capture/bank validators.
// No GPU state, source interpreter, render reordering or runtime optimization.
#include "scene_capture.hpp"
#include "model_bank.hpp"
#include SM64DS_COMPONENT_FIXTURE
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <vector>
using namespace sm64ds::render;
namespace {
struct Bank {std::vector<std::uint8_t> bytes;ModelBank model;};
struct Draw {
    PreparedPart part;
    std::uint32_t file=0;
    std::size_t vertices=0,indices=0;
    std::optional<PreparedLighting> light;
    bool normals=false,barrier=false;
};
bool same_source(const PreparedLighting& a,const PreparedLighting& b,LightingPaletteView palette){
    if(!(a.material==b.material))return false;
    const auto& material=a.material;if(!material.mask)return true;
    const auto& x=palette.lights[a.lights];const auto& y=palette.lights[b.lights];
    for(unsigned i=0;i<4;++i)if((material.mask&(1u<<i))&&!(x.light[i]==y.light[i]))return false;
    if(material.specular&&material.use_shininess&&x.shininess!=y.shininess)return false;
    return !(material.toon||material.grey)||x.toon==y.toon;
}
bool eligible(const Draw& d,bool prelit){
    if(d.barrier||d.part.highlight||d.part.material.uv_mode!=UvMode::affine)return false;
    if(!d.light)return true;
    if(!d.normals)return false;
    if(!prelit)for(auto value:d.light->normal)if(value<-32768||value>32767)return false;
    return true;
}
bool compatible(const Draw& a,const Draw& b,bool prelit,LightingPaletteView palette){
    if(!(a.part.material==b.part.material)||a.part.image!=b.part.image)return false;
    if(a.part.image!=ModelBank::no_texture&&a.file!=b.file)return false;
    if(prelit)return true;
    if(bool(a.light)!=bool(b.light))return false;
    return !a.light||same_source(*a.light,*b.light,palette);
}
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    const auto scene=component_fixture::init();
    FILE* check=std::tmpfile();if(!check)return 2;
    const bool valid=write_component_capture(check,scene,{});const bool closed=std::fclose(check)==0;
    if(!valid||!closed){std::cerr<<"Invalid semantic capture\n";return 2;}
    std::map<std::uint32_t,Bank> banks;
    const auto bank=[&](std::uint32_t id)->const ModelBank* {
        const auto old=banks.find(id);if(old!=banks.end())return &old->second.model;
        char name[16];std::snprintf(name,sizeof name,"%05u.nmd",unsigned(id));
        std::ifstream file(std::filesystem::path(argv[1])/name,std::ios::binary|std::ios::ate);
        if(!file)return nullptr;
        const auto length=file.tellg();if(length<128||length>64*1024*1024)return nullptr;
        auto& item=banks[id];item.bytes.resize(std::size_t(length));file.seekg(0);
        if(!file.read(reinterpret_cast<char*>(item.bytes.data()),length)||
           ModelBank::open(item.bytes.data(),item.bytes.size(),item.model)!=Error::none)return nullptr;
        return &item.model;
    };
    std::vector<Draw> draws;unsigned placeholders=0;
    const auto append=[&](const PreparedPart& part,std::uint32_t file,std::size_t vertices,std::size_t indices,bool normals,bool secondary){
        Draw d{part,file,vertices,indices,{},normals,false};
        if(part.lighting!=no_lighting)d.light=scene.lighting.state[part.lighting];
        draws.push_back(d);
        if(secondary&&part.highlight&&d.light){
            d.part.image=ModelBank::no_texture;d.part.material.blend=Blend::highlight;d.part.material.depth_write=false;
            d.light->material.grey=false;d.light->material.toon=true;draws.push_back(d);
        }
    };
    for(std::size_t i=0;i<scene.occurrences;++i){const auto& o=scene.occurrence[i];
        if(o.kind==Occurrence::Kind::quads){Draw d{};d.barrier=true;draws.push_back(d);continue;}
        if(o.kind==Occurrence::Kind::model){
            const auto* model=bank(o.file_id);
            if(!model||model->parts()!=o.count){std::cerr<<"Missing/mismatched bank "<<o.file_id<<'\n';return 2;}
            for(unsigned p=0;p<o.count;++p){const auto& part=scene.part[o.first+p];if(!part.visible)continue;
                if(!model->has_distinct_index_triangle(p)){++placeholders;continue;}
                const auto source=model->part(p);const auto mesh=model->mesh(source.mesh);
                if(part.image!=ModelBank::no_texture&&part.image>=model->images())return 2;
                std::set<std::uint16_t> used;
                for(std::size_t n=source.first;n<source.first+source.count;++n)
                    used.insert(std::uint16_t(mesh.indices[n*2]|unsigned(mesh.indices[n*2+1])<<8));
                append(part,o.file_id,used.size(),source.count,mesh.normals,false);
            }
        }else{
            const auto& part=scene.part[o.state];if(part.visible)append(part,o.file_id,o.count,o.count,true,true);
        }
    }
    unsigned actual=0,barriers=0;std::size_t vertices=0,indices=0;
    for(const auto& d:draws){if(d.barrier){++barriers;continue;}++actual;vertices+=d.vertices;indices+=d.indices;}
    std::cout<<"{\"scope\":\"offline opportunity only; no measured speedup\",\"scene\":"<<scene.scene
        <<",\"serial\":"<<scene.serial<<",\"world_draws\":"<<actual<<",\"quad_barriers\":"<<barriers
        <<",\"empty_parts_omitted\":"<<placeholders<<",\"vertices_per_part_unique\":"<<vertices
        <<",\"indices\":"<<indices<<",\"candidates\":[";
    for(unsigned mode=0;mode<2;++mode){
        unsigned groups=0,candidates=0,run=0,merged=0,max_run=0;const Draw* previous=nullptr;
        for(const auto& d:draws){
            if(!eligible(d,mode)){if(!d.barrier)++groups;previous=nullptr;run=0;continue;}
            ++candidates;
            if(previous&&run<16&&compatible(*previous,d,mode,scene.lighting)){++run;++merged;}
            else{++groups;run=1;}
            max_run=std::max(max_run,run);previous=&d;
        }
        if(mode)std::cout<<',';
        std::cout<<"{\"method\":\""<<(mode?"prelit_palette16":"matrix_normal_attributes")<<"\",\"eligible_draws\":"<<candidates
            <<",\"world_draws_after\":"<<groups<<",\"draws_removed\":"<<merged<<",\"maximum_run\":"<<max_run<<'}';
    }
    std::cout<<"]}\n";
}
