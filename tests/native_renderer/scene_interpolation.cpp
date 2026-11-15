#include "scene_interpolation.hpp"
#include <cassert>
#include <memory>
using namespace sm64ds::render;
static Occurrence actor(std::uint64_t owner,std::uint64_t component,unsigned first=0){
    return {261,first,3,Occurrence::Kind::model,0,{owner,component,0}};
}
int main(){
    OccurrenceMatcher matcher;
    Occurrence old[]={actor(11,101),actor(22,102,3),actor(33,103,6)};
    Occurrence now[]={actor(33,103),actor(11,101,3),actor(44,104,6)};
    SceneView a,b;a.occurrence=old;a.occurrences=3;a.scene=1;a.serial=10;
    b=a;b.occurrence=now;b.serial=11;
    assert(matcher.match(a,b)==Error::none);
    auto r=matcher.result();assert(r.continuous&&r.matched==2&&r.unmatched==1);
    assert(r.previous[0]==2&&r.previous[1]==0&&r.previous[2]==OccurrenceMatcher::missing);
    // Identical topology/file and reused object address cannot hide a new UID.
    now[2]=actor(55,103,6);assert(matcher.match(a,b)==Error::none&&matcher.result().matched==2);
    // Multiple render passes of one component require a future explicit pass
    // identity; do not guess based on list position or matrix similarity.
    now[2]=now[1];assert(matcher.match(a,b)==Error::none);
    r=matcher.result();assert(r.matched==1&&r.ambiguous==1);
    assert(r.previous[1]==OccurrenceMatcher::missing&&r.previous[2]==OccurrenceMatcher::missing);
    now[2]=actor(22,102);old[2]=old[0];assert(matcher.match(a,b)==Error::none);
    assert(matcher.result().matched==1&&matcher.result().previous[1]==OccurrenceMatcher::missing);
    old[2]=actor(33,103);now[2]=actor(44,104);
    now[0].count=4;assert(matcher.match(a,b)==Error::none&&matcher.result().matched==1);
    now[0].count=3;now[1].identity={};assert(matcher.match(a,b)==Error::none&&matcher.result().matched==1);
    now[1]=actor(11,101);now[0].file_id=262;assert(matcher.match(a,b)==Error::none&&matcher.result().matched==1);
    now[0].file_id=261;
    for(unsigned cut=0;cut<4;++cut){auto end=b;
        if(cut==0)++end.scene;if(cut==1)++end.serial;
        if(cut==2)end.scene_screen=Screen::bottom;if(cut==3)++end.scene_order;
        assert(matcher.match(a,end)==Error::none&&!matcher.result().continuous&&matcher.result().matched==0);
    }
    // Skin pieces follow their semantic group, independently of global part
    // offsets and vertex offsets changing when another actor appears first.
    old[0].kind=now[1].kind=Occurrence::Kind::skin;
    old[0].identity.piece=now[1].identity.piece=7;old[0].state=4;now[1].state=37;
    assert(matcher.match(a,b)==Error::none&&matcher.result().previous[1]==0);
    now[1].identity.piece=8;assert(matcher.match(a,b)==Error::none&&matcher.result().previous[1]==OccurrenceMatcher::missing);
    std::array<Occurrence,OccurrenceMatcher::capacity> crowded_a{},crowded_b{};
    for(unsigned i=0;i<crowded_a.size();++i){crowded_a[i]=actor(i+1,1000+i);crowded_b[crowded_a.size()-i-1]=crowded_a[i];}
    a.occurrence=crowded_a.data();a.occurrences=crowded_a.size();b.occurrence=crowded_b.data();b.occurrences=crowded_b.size();
    assert(matcher.match(a,b)==Error::none&&matcher.result().matched==crowded_a.size());
    // Full union (births/deaths) still fits the fixed hash table.
    for(auto& o:crowded_b)o.identity.owner+=10000;
    assert(matcher.match(a,b)==Error::none&&matcher.result().matched==0);
    b.occurrences=769;assert(matcher.match(a,b)==Error::capacity);
    b.occurrences=1;b.occurrence=nullptr;assert(matcher.match(a,b)==Error::invalid_data);

    auto poses=std::make_unique<PoseHistory>();
    PreparedPart old_parts[3]{},new_parts[3]{};for(auto& p:old_parts){p.visible=true;p.object_to_clip=identity();}
    for(auto& p:new_parts){p.visible=true;p.object_to_clip=identity();p.object_to_clip.row[0][3]=10;}
    Occurrence old_actor=actor(99,100),new_actor=old_actor;
    SceneView old_frame;old_frame.scene=7;old_frame.serial=1;old_frame.occurrence=&old_actor;old_frame.occurrences=1;old_frame.part=old_parts;old_frame.parts=3;
    auto new_frame=old_frame;new_frame.serial=2;new_frame.occurrence=&new_actor;new_frame.part=new_parts;
    assert(poses->remember(old_frame)==Error::none);
    old_parts[0].object_to_clip.row[0][3]=999;old_actor.identity.owner=123; // history must own both snapshots
    SceneView mid;assert(poses->midpoint(new_frame,mid)==Error::none&&poses->blended_parts()==3);
    assert(mid.part[0].object_to_clip.row[0][3]==5&&new_parts[0].object_to_clip.row[0][3]==10);
    ++new_frame.scene;assert(poses->midpoint(new_frame,mid)==Error::none&&poses->blended_parts()==0);
    assert(mid.part==new_parts);--new_frame.scene;
    poses->reset();assert(poses->midpoint(new_frame,mid)==Error::none&&mid.part==new_parts);
    old_frame.parts=0;assert(poses->remember(old_frame)==Error::invalid_data);
}
