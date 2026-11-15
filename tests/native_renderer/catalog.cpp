#include "model_catalog.hpp"
#include "native_quads.hpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
using namespace sm64ds::render;
int main(int argc,char** argv) {
    const auto catalog=prepared_model_catalog();
    assert(argc==2 && catalog.size()>0);
    unsigned checked=0,image_choices=0,sprite_choices=0;
    for(unsigned id=0;id<65536;++id){
        const auto* definition=catalog.find(id);if(!definition)continue;
        char name[32];std::snprintf(name,sizeof(name),"/%05u.nmd",id);
        const auto path=std::string(argv[1])+name;
        std::ifstream input(path,std::ios::binary);
        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});
        ModelBank bank;assert(ModelBank::open(bytes.data(),bytes.size(),bank)==Error::none);
        assert(validate(bank,*definition)==Error::none);++checked;
        for(unsigned i=0;i<definition->images;++i){const auto& image=definition->image[i];
            if(image.use==ImageUse::sprite){assert(sprite_image(*definition,image.texture,image.palette)==&image);++sprite_choices;}}
        // Exercise every prepared logical identity through the production
        // instance capture, including images added only by BTP preparation.
        // Poses/materials/handles are authored; this is not actor acceptance.
        std::vector<Matrix> poses(definition->bones,identity());
        std::vector<LiveMaterial> materials(definition->source_materials);
        std::vector<Texture> handles(definition->images);
        for(unsigned i=0;i<handles.size();++i)handles[i]={1,std::uint16_t(i),definition};
        std::vector<PartInstance> captured(definition->parts);
        std::vector<NormalBasis> normal(definition->bones);
        for(auto& b:normal)b.value[0]=b.value[4]=b.value[8]=4096;
        std::vector<PreparedLighting> lighting(definition->parts);LightSet lights[1];
        LightingPalette palette(lights,1,lighting.data(),lighting.size());
        for(unsigned i=0;i<definition->materials;++i){const auto& m=definition->material[i];auto& live=materials[m.source_material];
            if(m.normal_uv){live.state.uv_mode=UvMode::normal;live.normal_half_width=m.width/2;live.normal_half_height=m.height/2;
                live.state.normal_uv().width=m.width;live.state.normal_uv().height=m.height;}}
        const InstanceInputs occurrence{poses.data(),poses.data(),poses.size(),materials.data(),materials.size(),handles.data(),handles.size(),normal.data()};
        for(unsigned i=0;i<(definition->images?definition->images:1);++i){
            if(definition->images&&definition->image[i].use!=ImageUse::model)continue;
            if(definition->images){const auto& image=definition->image[i];
                for(auto& material:materials){material.texture=image.texture;material.palette=image.palette;}}
            palette.reset();
            assert(capture_instance(*definition,occurrence,captured.data(),captured.size(),&palette)==Error::none);
            for(const auto& part:captured){assert(part.visible);
                assert(part.material.texture==(definition->images?handles[definition->image[i].image]:Texture{}));}
            if(definition->images)++image_choices;
        }
        auto broken=*definition;broken.source_sha256[0]^=1;
        assert(validate(bank,broken)==Error::invalid_data);
        ModelCatalog retained=catalog;
        ModelBindings duplicate[2]={*definition,*definition};
        assert(ModelCatalog::open(duplicate,2,retained)==Error::invalid_data && retained.size()==catalog.size());
    }
    assert(checked==catalog.size() && !catalog.find(65536));
    std::printf("Prepared source bindings matched %u native banks, %u model image choices and %u sprite image choices; mismatched provenance and duplicate IDs refused\n",checked,image_choices,sprite_choices);
}
