#pragma once
#include "sm64ds/model.hpp"
#include "sm64ds/input.hpp"
#include <citro3d.h>
#include <vector>
namespace sm64ds {
class Renderer {
public:
    bool init(const ModelPreview& model,std::string& error);
    void update(const InputFrame& input,float dt);
    void draw(float slider);
    void exit();
private:
    struct Vertex {float x,y,z; uint32_t color; float u,v;};
    static_assert(sizeof(Vertex)==24);
    struct Batch {int first,count,texture; uint8_t wrap; bool translucent;};
    C3D_RenderTarget* targets_[2]{};
    DVLB_s* dvlb_=nullptr;
    shaderProgram_s program_{};
    bool program_ready_=false,ready_=false,stereo_=false;
    int proj_location_=0,view_location_=0;
    Vertex* vertices_=nullptr;
    std::vector<C3D_Tex> textures_;
    std::vector<Batch> batches_;
    float yaw_=0,pitch_=0.30f,distance_=2.7f,panx_=0,pany_=0;
    void eye(int index,float iod);
};
}
