#include "image_layer.hpp"
#include <algorithm>
namespace sm64ds::render {
bool valid(const ImageLayer& image) noexcept {
    if(image.bank_id>65535||image.image>=128||!screen_width(image.screen)||image.material.texture||
       image.tone_target>32||(image.tone_target&&(image.tone_target&(image.tone_target-1))))return false;
    if(image.source==LayerSource::images){if(image.part)return false;}
    else if(image.source==LayerSource::canvas){if(image.image||image.part>=1024)return false;}
    else if(image.source==LayerSource::solid){if(image.bank_id||image.image||image.part||image.vertices)return false;}
    else return false;
    if(!valid(image.clip,screen_width(image.screen)))return false;
    if(image.vertices){
        if(image.source!=LayerSource::images||image.first_vertex%4||image.vertices%4)return false;
    }else if(image.first_vertex)return false;
    ScreenLayer layer;layer.rectangle=image.rectangle;layer.material=image.material;layer.clip=image.clip;
    return validate({0xff,&layer,1})==Error::none;
}
Error order_layers(const ImageLayer* images,std::size_t count,Screen side,std::uint16_t scene_order,OrderedLayers& output) noexcept {
    if(count>ImageLayer::max_layers||(count&&!images)||!screen_width(side))return Error::invalid_data;
    std::size_t top=side==Screen::top,bottom=side==Screen::bottom;
    for(std::size_t i=0;i<count;++i){
        if(!valid(images[i]))return Error::invalid_data;
        if(images[i].screen==Screen::top)++top;else ++bottom;
    }
    if(top>ScreenCanvas::max_layers||bottom>ScreenCanvas::max_layers)return Error::capacity;
    OrderedLayers next;next.top=top;next.bottom=bottom;next.index[0]=OrderedLayers::scene;
    const auto less=[&](std::uint16_t a,std::uint16_t b){
        const auto sa=a==OrderedLayers::scene?side:images[a].screen;
        const auto sb=b==OrderedLayers::scene?side:images[b].screen;
        if(sa!=sb)return sa==Screen::top;
        const auto oa=a==OrderedLayers::scene?scene_order:images[a].order;
        const auto ob=b==OrderedLayers::scene?scene_order:images[b].order;
        if(oa!=ob)return oa<ob;
        return (a==OrderedLayers::scene?0:unsigned(a)+1)<(b==OrderedLayers::scene?0:unsigned(b)+1);
    };
    for(std::size_t i=0;i<count;++i)next.index[i+1]=std::uint16_t(i);
    // Sort only compact indices; explicit tie ranks preserve chronology
    // without stable_sort's allocation or copies of complete materials.
    std::sort(next.index.begin(),next.index.begin()+count+1,less);
    output=next;return Error::none;
}
}
