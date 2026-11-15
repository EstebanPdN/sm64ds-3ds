#include "native_batch.hpp"
#include "scene_frame.hpp"
#include <cassert>
#include <limits>
using namespace sm64ds::render;
int main(){
    for(unsigned slot=0;slot<8;++slot){
        const auto packed=packed_palette_rows(std::uint8_t(slot));
        for(unsigned row=0;row<4;++row)assert(((packed>>(row*8))&255u)==slot*4+row);
    }
    int domain;const Mesh mesh{1,1,&domain},other{2,1,&domain};
    static NativeBatchPlan plan;unsigned span;
    // BUILD134: the plan borrows matrices and light sets from the frame it
    // was built from; they stay alive and unchanged until its last draw.
    Material material;const Matrix unit=identity();std::array<Matrix,9> frame;frame.fill(unit);
    auto& matrix=frame[0];auto& moved=frame[1];LightingInput red,green;
    red.material.emission=31;green.material.emission=31<<5;
    plan.reset();
    assert(plan.append({mesh,0,6,0},material,nullptr,&red,span)==Error::invalid_data&&plan.seal()==Error::invalid_data);
    plan.reset();
    assert(plan.append({mesh,0,6,0},material,&matrix,&red,span)==Error::none&&span==0);
    moved.row[0][3]=7;
    assert(plan.append({mesh,6,6,1},material,&moved,&red,span)==Error::none&&span==0);
    assert(plan.append({mesh,12,6,2},material,&moved,&green,span)==Error::none&&span==1);
    red.material.emission=0; // the lighting material is owned
    assert(plan.seal()==Error::none&&plan.spans()==2);
    assert(plan.span(0)->count==12&&plan.span(0)->used==3&&plan.span(0)->palette[0]==&matrix&&plan.span(0)->palette[1]==&moved&&
        plan.span(0)->palette[1]->row[0][3]==7);
    assert(plan.span(0)->lighting.emission==31&&plan.span(1)->lighting.emission==(31<<5));
    assert(plan.append({mesh,18,3,0},material,&matrix,nullptr,span)==Error::invalid_phase);
    plan.reset();
    for(unsigned i=0;i<8;++i){frame[i]=unit;frame[i].row[1][3]=float(i);
        assert(plan.append({mesh,i*3,3,std::uint8_t(i)},material,&frame[i],nullptr,span)==Error::none&&span==0);}
    // Reusing a slot with a different pose starts another draw, even if the
    // immutable geometry ranges are adjacent and materials remain identical.
    frame[8]=unit;frame[8].row[1][3]=99;
    assert(plan.append({mesh,24,3,0},material,&frame[8],nullptr,span)==Error::none&&span==1);
    assert(plan.seal()==Error::none&&plan.span(0)->used==255&&plan.span(0)->palette[7]->row[1][3]==7);
    // An equal matrix at another address shares the slot, as its copy did.
    plan.reset();frame[0]=unit;frame[1]=unit;
    assert(plan.append({mesh,0,3,0},material,&frame[0],nullptr,span)==Error::none&&span==0);
    assert(plan.append({mesh,3,3,0},material,&frame[1],nullptr,span)==Error::none&&span==0);
    assert(plan.seal()==Error::none&&plan.spans()==1&&plan.span(0)->count==6);
    plan.reset();red.material.emission=31;red.normal={-32768,32767,-1,4096,0,64,63,-65,8192};red.material.mask=1;
    LightSet lights,same_lights,other_lights;lights.known=same_lights.known=other_lights.known=1;other_lights.light[0].colour[0]=31;
    red.lights=&lights;green=red;green.normal[0]=32767;green.lights=&same_lights;
    assert(plan.append({mesh,0,3,0},material,&unit,&red,span)==Error::none);
    assert(plan.append({mesh,3,3,1},material,&unit,&green,span)==Error::none&&span==0);
    // The 16-bit basis is owned; the light set is the frame's. Equal lights
    // at another address continue the span, different ones start another.
    red.normal.fill(0);
    green.lights=&other_lights;
    assert(plan.append({mesh,6,3,2},material,&unit,&green,span)==Error::none&&span==1);
    assert(plan.seal()==Error::none&&plan.span(0)->basis[0][0][0]==-32768&&plan.span(0)->basis[1][0][0]==32767);
    assert(plan.span(0)->lights==&lights&&plan.span(1)->lights==&other_lights&&plan.span(0)->lights->light[0].colour[0]==0);
    plan.reset();
    assert(plan.append({mesh,0,3,0},material,&unit,nullptr,span)==Error::none);
    green.normal[0]=32768;
    assert(plan.append({mesh,3,3,1},material,&unit,&green,span)==Error::unsupported);
    green.normal[0]=0;green.material.specular=1;green.material.use_shininess=true;
    assert(plan.append({mesh,3,3,1},material,&unit,&green,span)==Error::unsupported);
    green.material.use_shininess=false;green.material.toon=true;
    assert(plan.append({mesh,3,3,1},material,&unit,&green,span)==Error::unsupported);
    plan.barrier();
    assert(plan.append({mesh,3,3,1},material,&unit,nullptr,span)==Error::none&&span==1);
    assert(plan.append({other,6,3,2},material,&unit,nullptr,span)==Error::none&&span==2);
    assert(plan.append({other,12,3,3},material,&unit,nullptr,span)==Error::none&&span==3); // hidden range hole
    material.blend=Blend::alpha;
    assert(plan.append({other,15,3,4},material,&unit,nullptr,span)==Error::none&&span==4);
    assert(plan.seal()==Error::none&&plan.spans()==5);
    plan.reset();assert(plan.append({mesh,0,3,8},material,&unit,nullptr,span)==Error::invalid_data);
    assert(plan.seal()==Error::invalid_data&&!plan.ready());
    plan.reset();
    for(unsigned i=0;i<NativeBatchPlan::max_spans;++i){plan.barrier();assert(plan.append({mesh,0,3,0},material,&unit,nullptr,span)==Error::none);}
    plan.barrier();assert(plan.append({mesh,0,3,0},material,&unit,nullptr,span)==Error::capacity&&plan.seal()==Error::capacity);

    static DynamicBatchRanges dynamic;
    Occurrence occurrences[5]={{1,0,3,Occurrence::Kind::skin},{1,3,3,Occurrence::Kind::mesh},
        {1,0,4,Occurrence::Kind::quads},{1,4,4,Occurrence::Kind::quads},{1,6,3,Occurrence::Kind::skin}};
    SceneView scene;scene.occurrence=occurrences;scene.occurrences=5;scene.skin_vertices=9;scene.quad_vertices=8;
    assert(dynamic.prepare(scene));
    for(unsigned i=0;i<5;++i)assert(dynamic.eligible(i));
    assert(dynamic.skin()[0]==0&&dynamic.skin()[3]==1&&dynamic.skin()[6]==2&&dynamic.quads()[4]==1);
    occurrences[1].first=0; // overlap excludes both actors, never shares a selector/pose destination
    assert(dynamic.prepare(scene)&&!dynamic.eligible(0)&&!dynamic.eligible(1)&&dynamic.eligible(4));
    assert(dynamic.skin()[0]==0&&dynamic.skin()[6]==0);
    // Differential interval oracle exercises the ordered proof and unsorted
    // fallback against arbitrary overlapping ranges in both separate arenas.
    std::array<Occurrence,32> mixed{};std::uint32_t random=0x53d5;
    scene.occurrence=mixed.data();scene.occurrences=mixed.size();scene.skin_vertices=96;scene.quad_vertices=128;
    for(unsigned trial=0;trial<1024;++trial){
        for(unsigned i=0;i<mixed.size();++i){random=random*1664525u+1013904223u;
            const auto kind=i%2?Occurrence::Kind::quads:Occurrence::Kind::skin;
            const auto stride=i%2?4u:3u;
            const unsigned first=trial%4==0?(i/2)*stride:(random%30)*stride;
            mixed[i]={1,first,stride,kind};
        }
        assert(dynamic.prepare(scene));unsigned skin_slot=0,quad_slot=0;
        for(unsigned i=0;i<mixed.size();++i){const auto& a=mixed[i];bool overlaps=false;
            for(unsigned j=0;j<mixed.size();++j){const auto& b=mixed[j];
                if(i!=j&&a.kind==b.kind&&a.first<b.first+b.count&&b.first<a.first+a.count)overlaps=true;}
            assert(dynamic.eligible(i)==!overlaps);
            if(overlaps)continue;
            auto& slot=a.kind==Occurrence::Kind::quads?quad_slot:skin_slot;
            assert(dynamic.slot(i)==slot%8);
            const auto* selectors=a.kind==Occurrence::Kind::quads?dynamic.quads():dynamic.skin();
            for(unsigned v=a.first;v<a.first+a.count;++v)assert(selectors[v]==slot%8);
            ++slot;
        }
    }
    scene.skin_vertices=DynamicBatchRanges::max_skin_vertices+1;
    assert(!dynamic.prepare(scene)&&!dynamic.skin()&&!dynamic.eligible(9999));
}
