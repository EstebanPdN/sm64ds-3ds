#include "scene_capture.hpp"
#include SM64DS_COMPONENT_ROUNDTRIP_FIXTURE
#include <cassert>

int main(int argc,char** argv){
    assert(argc==2);
    const auto scene=sm64ds::render::component_fixture::init();
    FILE* file=std::fopen(argv[1],"wb");assert(file);
    assert(sm64ds::render::write_component_capture(file,scene,{121,9,true,true}));
    assert(std::fclose(file)==0);
}
