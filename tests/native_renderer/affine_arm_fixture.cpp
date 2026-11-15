#include "game_model_pose.hpp"
using sm64ds::render::game::Affine;
extern "C" void affine_production(const Affine* local,const Affine* parent,Affine* output,int assembly) {
    sm64ds::render::game::use_affine_assembly(assembly!=0);
    *output=sm64ds::render::game::compose(*local,*parent);
}
