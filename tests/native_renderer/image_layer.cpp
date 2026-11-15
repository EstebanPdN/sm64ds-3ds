#include "scene_frame.hpp"
#include <cassert>
#include <limits>
using namespace sm64ds::render;
int main(){
    ImageLayer image;image.bank_id=4096;image.rectangle={0,160,320,20};image.screen=Screen::bottom;image.order=2;
    assert(valid(image));
    ImageLayer panel;panel.source=LayerSource::solid;panel.rectangle={40,12,320,72};panel.material.tint=0x00000080u;panel.material.blend=Blend::alpha;
    assert(valid(panel));panel.bank_id=4096;assert(!valid(panel));panel.bank_id=0;
    panel.vertices=4;assert(!valid(panel));panel.vertices=0;assert(valid(panel));
    auto canvas=image;canvas.source=LayerSource::canvas;canvas.bank_id=5001;
    assert(valid(canvas));canvas.part=1023;assert(valid(canvas));canvas.part=1024;assert(!valid(canvas));
    canvas.part=0;canvas.image=1;assert(!valid(canvas));canvas.image=0;canvas.source=LayerSource(2);assert(!valid(canvas));
    ImageLayer layers[127];for(auto& l:layers)l=image;
    layers[0].screen=Screen::top;layers[0].order=7;
    layers[1].order=1;layers[2].order=2;
    OrderedLayers ordered;
    assert(order_layers(layers,3,Screen::bottom,2,ordered)==Error::none);
    assert(ordered.top==1&&ordered.bottom==3&&ordered.index[0]==0&&ordered.index[1]==1&&ordered.index[2]==OrderedLayers::scene&&ordered.index[3]==2);
    auto bad=[&](Error expected){OrderedLayers out;out.top=999;out.index.fill(1234);
        assert(order_layers(layers,127,Screen::top,2,out)==expected);assert(out.top==999&&out.index[0]==1234);};
    bad(Error::capacity);layers[100].material.texture.generation=1;bad(Error::invalid_data);layers[100]=image;
    for(unsigned i=0;i<64;++i)layers[i].screen=Screen::top;
    assert(order_layers(layers,127,Screen::bottom,2,ordered)==Error::none&&ordered.top==64&&ordered.bottom==64);
    assert(order_layers(nullptr,1,Screen::top,0,ordered)==Error::invalid_data);
    assert(order_layers(nullptr,0,Screen::top,0,ordered)==Error::none);
    layers[0].rectangle.x=std::numeric_limits<float>::infinity();assert(!valid(layers[0]));
    Occurrence occurrence[1];PreparedPart parts[1];ImageLayer storage[2];
    SceneFrame frame(occurrence,1,parts,1,nullptr,0,nullptr,0,{},storage,2);SceneView view;
    assert(frame.append_image(image)==Error::invalid_phase);
    assert(frame.begin(3,4)==Error::none&&frame.presentation(Screen::bottom,19)==Error::none);
    image.clip={7,13,300,230};assert(frame.append_image(image)==Error::none);image.clip={};image.bank_id=1;image.rectangle.y=4;
    assert(frame.seal(true,0)==Error::none&&frame.append_image(image)==Error::invalid_phase);
    assert(frame.presentation(Screen::top,7)==Error::invalid_phase&&frame.acquire(view)==Error::none);
    assert(view.image_layers==1&&view.image_layer[0].bank_id==4096&&view.image_layer[0].rectangle.y==160);
    assert(view.image_layer[0].clip.left==7&&view.image_layer[0].clip.bottom==230);
    assert(view.scene_order==19&&view.scene_screen==Screen::bottom&&frame.release()==Error::none);
    assert(frame.begin(4,5)==Error::none&&frame.captured_images()==0&&frame.captured_scene_screen()==Screen::top);
    assert(frame.append_image(image)==Error::none&&frame.append_image(image)==Error::none);
    assert(frame.append_image(image)==Error::capacity&&frame.seal(true,0)==Error::capacity&&frame.captured_images()==0);
    assert(frame.acquire(view)==Error::invalid_phase&&frame.begin(4,6)==Error::none);
    image.image=128;assert(frame.append_image(image)==Error::invalid_data&&frame.seal(true,0)==Error::invalid_data);
    assert(frame.begin(4,7)==Error::none&&frame.seal(true,0)==Error::none&&frame.release()==Error::none);
    assert(frame.begin(5,8)==Error::none&&frame.append_image(panel)==Error::none);
    assert(frame.seal(true,0)==Error::none&&frame.acquire(view)==Error::none);
    assert(view.image_layers==1&&view.image_layer[0].source==LayerSource::solid&&view.image_layer[0].material.tint==0x80u);
    assert(frame.release()==Error::none);
}
