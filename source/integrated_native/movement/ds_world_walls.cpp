#include "ds_world_query.h"
#include "wall_contact.hpp"
#include <climits>
using namespace movement_geometry;
extern "C" int sm64ds_world_walls_reference(const DsWorldWallInput* input,void* actor,
                                             uint8_t detect_flags,DsWorldWallResult* result) {
    if(!input || !result || input->radius<0)return -1;
    for(float v:{input->x,input->y,input->z,input->offset_y,input->radius})if(!std::isfinite(v))return -1;
    if(!std::isfinite(input->y+input->offset_y))return -1;
    const WallCylinder cylinder{input->x,input->y,input->z,input->offset_y,input->radius};
    DsWorldWallResult staged{};staged.x=input->x;staged.z=input->z;
    for(unsigned slot=0;slot<24;++slot) {
        const int count=sm64ds_world_prism_count(slot);if(count<0)return -1;
        for(unsigned index=1;index<=unsigned(count);++index) {
            DsWorldGroundHit hit;DsWorldPrism prism;
            if(!sm64ds_world_prism_at(slot,index,&hit,&prism))return -1;
            if(hit.normal[1]<-40 || hit.normal[1]>40)continue;
            WorldTriangle triangle;
            const auto decoded=decode_world(prism,hit.normal,triangle);
            if(decoded!=WorldDecode::Ok&&decoded!=WorldDecode::Degenerate)return -1;
            WallPush push;const auto status=wall_contact(triangle,cylinder,push);
            if(status==WallContact::Invalid)return -1;
            if(status==WallContact::Missing)continue;
            const int pass=sm64ds_contact_pass_through(&hit,actor,detect_flags,1);
            if(pass<0)return -1;
            if(pass)continue;
            if(status==WallContact::NeedsDs)return -2;
            staged.x+=push.x;staged.z+=push.z;
            if(!std::isfinite(staged.x) || !std::isfinite(staged.z) || staged.count==INT_MAX)return -1;
            ++staged.count;
            if(staged.stored<4)staged.surfaces[staged.stored++]=hit;
        }
    }
    if(!staged.count)return 0;
    *result=staged;return 1;
}
