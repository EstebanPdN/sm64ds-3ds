#include "../../source/integrated_native/movement/material_profile.hpp"
#include <cassert>
#include <climits>
int main(){
 using namespace movement_geometry;
 const auto clock_ceiling=material_profile(0x8b000c,0xff);
 assert(clock_ceiling.use==MaterialUse::NeedsDsEffect);
 assert(distant_effect_ceiling(clock_ceiling,-2630*4096,-4710));
 assert(!distant_effect_ceiling(clock_ceiling,160*4096,0));
 assert(distant_effect_ceiling(clock_ceiling,160*4096+1,0));
 assert(!distant_effect_ceiling(clock_ceiling,-160*4096,-160));
 const auto ordinary=material_profile(0x32005,0xff);
 assert(ordinary.use==MaterialUse::Solid);
 assert(!distant_effect_ceiling(ordinary,INT_MAX,INT_MIN));
 assert(distant_effect_ceiling(clock_ceiling,INT_MAX,INT_MIN));
 assert(!distant_effect_ceiling(clock_ceiling,INT_MIN,INT_MAX));
}
