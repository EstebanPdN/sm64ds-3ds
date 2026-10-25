#pragma once
#include "wall_contact.hpp"
#include <array>
#include <cstddef>
#include <climits>

namespace movement_geometry {
// Resident scene-owned storage. Allocate the owner outside the simulation
// stack; rebuild explicitly on scene changes before activating a solver.
// Static geometry is decoded once; movers refresh at each explicit tick. The
// original DS filter revalidates any contact before it is returned.
template<std::size_t Capacity> class WorldWalls {
    struct Stamp {int count=0;DsWorldGroundHit first{};uint32_t kcl=0;};
    struct Entry {DsWorldGroundHit hit{};WorldTriangle triangle{};bool moving=false;};
    std::array<Stamp,24> stamps{};
    std::array<Entry,Capacity> entries{};
    std::size_t used=0;
    bool loaded=false;
    bool tick_ready=false;
    bool registry_changed=false;
    uint32_t reads=0;
    bool read(unsigned slot,unsigned index,DsWorldGroundHit& hit,DsWorldPrism& prism) {
        ++reads;return sm64ds_world_prism_at(slot,index,&hit,&prism)==1;
    }
    static bool identity(const DsWorldGroundHit& a,const DsWorldGroundHit& b) {
        return a.collider==b.collider && a.actor==b.actor && a.owner_id==b.owner_id && a.slot==b.slot;
    }
public:
    WorldWalls()=default;
    WorldWalls(const WorldWalls&)=delete;
    WorldWalls& operator=(const WorldWalls&)=delete;
    void clear() {loaded=false;tick_ready=false;used=0;registry_changed=false;}
    void invalidate_tick() {tick_ready=false;}
    std::size_t size() const{return used;}
    bool generation_changed()const{return registry_changed;}
    uint32_t prism_reads() const{return reads;}
    bool build() {
        clear();reads=0;
        for(unsigned slot=0;slot<24;++slot) {
            auto& stamp=stamps[slot];stamp=Stamp{};
            stamp.count=sm64ds_world_prism_count(slot);
            if(stamp.count<0)return false;
            for(unsigned index=1;index<=unsigned(stamp.count);++index) {
                DsWorldGroundHit hit;DsWorldPrism prism;
                if(!read(slot,index,hit,prism))return false;
                if(index==1){stamp.first=hit;stamp.kcl=prism.kcl;}
                // Movers can rotate a former floor into a wall. Retain all
                // their triangles, not just their current classification.
                if(!prism.kind && (hit.normal[1]<-40 || hit.normal[1]>40))continue;
                if(used==Capacity)return false;
                WorldTriangle world;
                const auto decoded=decode_world(prism,hit.normal,world);
                if(decoded!=WorldDecode::Ok&&decoded!=WorldDecode::Degenerate)return false;
                entries[used++]={hit,world,prism.kind!=0};
            }
        }
        loaded=true;return true;
    }
    bool begin_tick() {
        reads=0;tick_ready=false;registry_changed=false;
        if(!loaded)return false;
        // Detect unloaded/replaced/added registry objects even when a query
        // happens far from them. No stale geometry is silently consulted.
        for(unsigned slot=0;slot<24;++slot) {
            const auto& stamp=stamps[slot];const int count=sm64ds_world_prism_count(slot);
            if(count<0)return false;
            if(count!=stamp.count){registry_changed=true;return false;}
            if(count) {
                DsWorldGroundHit hit;DsWorldPrism prism;
                if(!read(slot,1,hit,prism))return false;
                if(!identity(hit,stamp.first) || prism.kcl!=stamp.kcl){registry_changed=true;return false;}
            }
        }
        for(std::size_t i=0;i<used;++i) {
            auto& entry=entries[i];
            if(entry.moving) {
                DsWorldGroundHit hit;DsWorldPrism prism;WorldTriangle triangle;
                if(!read(entry.hit.slot,entry.hit.triangle,hit,prism) || !identity(hit,entry.hit))return false;
                const auto decoded=decode_world(prism,hit.normal,triangle);
                if(decoded!=WorldDecode::Ok&&decoded!=WorldDecode::Degenerate)return false;
                entry.hit=hit;entry.triangle=triangle;
            }
        }
        tick_ready=true;return true;
    }
    // Queries within one simulation-thread step share this snapshot. No actor
    // updates, scene mutations or collider writes may interleave; invalidate
    // and begin again after such a boundary. Actual hits still revalidate DS.
    int query_prepared(const DsWorldWallInput* input,void* actor,uint8_t flags,DsWorldWallResult* result) {
        if(!loaded || !tick_ready || !input || !result || input->radius<0)return -1;
        for(float v:{input->x,input->y,input->z,input->offset_y,input->radius})if(!std::isfinite(v))return -1;
        if(!std::isfinite(input->y+input->offset_y))return -1;
        DsWorldWallResult staged{};staged.x=input->x;staged.z=input->z;
        const WallCylinder cylinder{input->x,input->y,input->z,input->offset_y,input->radius};
        for(std::size_t i=0;i<used;++i) {
            const auto& entry=entries[i];
            WallPush push;const auto status=wall_contact(entry.triangle,cylinder,push);
            if(status==WallContact::Invalid)return -1;
            if(status==WallContact::Missing)continue;
            const int pass=sm64ds_contact_pass_through(&entry.hit,actor,flags,1);
            if(pass<0)return -1;
            if(pass)continue;
            if(status==WallContact::NeedsDs)return -2;
            staged.x+=push.x;staged.z+=push.z;
            if(!std::isfinite(staged.x) || !std::isfinite(staged.z) || staged.count==INT_MAX)return -1;
            ++staged.count;
            if(staged.stored<4)staged.surfaces[staged.stored++]=entry.hit;
        }
        if(!staged.count)return 0;
        *result=staged;return 1;
    }
    int query(const DsWorldWallInput* input,void* actor,uint8_t flags,DsWorldWallResult* result) {
        return begin_tick()?query_prepared(input,actor,flags,result):-1;
    }
};
}
