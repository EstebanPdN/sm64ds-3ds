// Immutable native map publication: the raster is retained by the PPU map
// cache; the caller owns foreground pixels and copies the descriptor. The
// caller must finish its previous render job before preparing another map.
// The overlay base of a foreground buffer is rebuilt only when the overlay
// changes; otherwise only the boxes that OBJs covered in that same buffer
// last time are restored before OBJs are drawn again. The runtime alternates
// two buffers, so each keeps its own base priorities and previous boxes.
// Only this publication writes a foreground buffer between two preparations.
namespace {
struct NativeAffineTarget {
    const Framebuffer* buffer=nullptr;const uint32_t* overlay=nullptr;uint64_t overlay_generation=0;
    bool valid=false;unsigned age=0,boxes=0;
    // BUILD134: serial of the OBJ inputs (NativeAffineObjInputs) this buffer's
    // sprites were last drawn from; 0 = drawn from inputs that are not keyed.
    uint64_t obj_serial=0;
    int16_t box[128][4];
    uint8_t priority[SCREEN_W*SCREEN_H];
};
NativeAffineTarget native_affine_targets[2];unsigned native_affine_clock=0;
void native_affine_base(const uint32_t* overlay,Framebuffer& out,uint8_t* priority,int x0,int x1,int y0,int y1){
    for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x){const unsigned i=unsigned(y)*SCREEN_W+unsigned(x);
        const uint32_t pixel=overlay?overlay[i]:0;const unsigned code=pixel>>24;
        out.px[y][x]=code?pixel|0xff000000u:0;priority[i]=code?uint8_t(code/32-2):4;}
}
// BUILD134 cost cut: native_affine_base without an overlay writes pixel 0 and
// priority 4 everywhere.
void native_affine_clear(Framebuffer& out,uint8_t* priority,int x0,int x1,int y0,int y1){
    if(x0>=x1)return;
    const size_t n=size_t(x1-x0);
    for(int y=y0;y<y1;++y){std::memset(&out.px[y][x0],0,n*4);std::memset(priority+y*SCREEN_W+x0,4,n);}
}
// BUILD134 cost cut: everything the OBJ pass of a publication reads, as last
// seen: DISPCNT, BLDCNT, BLDALPHA, OAM, the OBJ palette, the tile bytes of
// the sprites drawn, the extended OBJ palettes when a 256-colour sprite uses
// them, and the map descriptor when a semi-transparent sprite blends with
// it. The serial changes whenever any of them does. Over the same overlay
// base the OBJ pass is a function of these inputs alone (it starts from the
// base inside every box it draws in), so a buffer whose sprites were drawn
// from the current serial already holds the result of drawing them again.
constexpr uint32_t kNativeAffineTileBytes=24*1024;
struct NativeAffineObjInputs {
    uint64_t serial=0;bool valid=false,semi=false,extended=false;
    uint32_t disp=0,blend=0,alpha=0,tile_bytes=0;
    const PpuGpuAffineLayer* map=nullptr;uint64_t map_generation=0;
    uint8_t extended_control=0;
    uint8_t oam[1024],pltt[512],tiles[kNativeAffineTileBytes],extended_pltt[8192];
};
NativeAffineObjInputs native_affine_obj_inputs;
// 0: not keyed (bitmap OBJ sample VRAM freely; more tile bytes than kept).
uint64_t native_affine_obj_serial(const uint8_t* oam,uint32_t disp,unsigned blend,unsigned alpha,const uint8_t* drawn,unsigned count,
                                  bool bitmap,bool semi,bool extended,const PpuGpuAffineLayer* map,uint64_t map_generation){
    auto& in=native_affine_obj_inputs;
    if(bitmap){in.valid=false;return 0;}
    const auto* vram=static_cast<const uint8_t*>(host_ptr(0x06600000));
    // The tile bytes each drawn sprite samples, in drawing order (the same
    // ranges and VRAM wrap as the per-sprite key of prepare_scene).
    auto tiles=[&](auto&& visit){
        for(unsigned k=0;k<count;++k)obj_tile_spans(oam,drawn[k],disp,[&](uint32_t at,uint32_t bytes){
            at%=0x20000u;
            const uint32_t first=std::min<uint32_t>(bytes,0x20000u-at);
            visit(vram+at,first);
            if(bytes>first)visit(vram,bytes-first);
        });
    };
    uint32_t total=0;
    tiles([&](const uint8_t*,uint32_t bytes){total+=bytes;});
    if(total>kNativeAffineTileBytes){in.valid=false;return 0;}
    const auto* pltt=static_cast<const uint8_t*>(host_ptr(0x05000600));
    const auto* extended_pltt=static_cast<const uint8_t*>(host_ptr(0x068a0000));
    const uint8_t control=*static_cast<volatile uint8_t*>(host_ptr(0x04000249));
    bool equal=in.valid && in.disp==disp && in.blend==blend && in.alpha==alpha && in.semi==semi && in.extended==extended && in.tile_bytes==total &&
        (!semi || (in.map==map && in.map_generation==map_generation)) &&
        (!extended || (in.extended_control==control && ppu_equal_bytes(in.extended_pltt,extended_pltt,8192))) &&
        ppu_equal_bytes(in.oam,oam,1024) && ppu_equal_bytes(in.pltt,pltt,512);
    if(equal){
        // Same OAM and DISPCNT: the same ranges in the same order.
        uint32_t at=0;
        tiles([&](const uint8_t* live,uint32_t bytes){if(equal && !ppu_equal_bytes(in.tiles+at,live,bytes))equal=false;at+=bytes;});
    }
    if(equal)return in.serial;
    in.disp=disp;in.blend=blend;in.alpha=alpha;in.semi=semi;in.extended=extended;in.tile_bytes=total;
    in.map=map;in.map_generation=map_generation;in.extended_control=control;
    if(extended)std::memcpy(in.extended_pltt,extended_pltt,8192);
    std::memcpy(in.oam,oam,1024);std::memcpy(in.pltt,pltt,512);
    uint32_t at=0;
    tiles([&](const uint8_t* live,uint32_t bytes){std::memcpy(in.tiles+at,live,bytes);at+=bytes;});
    in.valid=true;
    return ++in.serial;
}
// The pixel statement of a publication (one OBJ texel over the foreground),
// held by value so its fields stay in registers across the byte stores.
struct NativeAffinePut {
    uint32_t* pixels;uint8_t* priority;const PpuGpuAffineLayer* layer;unsigned blend,alpha;
    inline __attribute__((always_inline)) void operator()(int x,int y,uint32_t pixel,unsigned p,unsigned object_alpha) const {
        at(unsigned(y*SCREEN_W+x),x,y,pixel,p,object_alpha);
    }
    // i = y*SCREEN_W+x, which a row of texels shares up to its column.
    inline __attribute__((always_inline)) void at(unsigned i,int x,int y,uint32_t pixel,unsigned p,unsigned object_alpha) const {
        // OBJ wins a priority tie against BG; lower OAM wins OBJ ties.
        if(p<=priority[i]){
            // Only pixels actually occupied by a semi-transparent OBJ need
            // the map's integer CPU sample. Every other map texel stays GPU.
            const PpuGpuAffineLayer& map=*layer;
            if(object_alpha&&(!map.overlay||!(map.overlay[i]>>24))){
                int sx=(map.ref_x+x*map.pa+y*map.pb)>>8,sy=(map.ref_y+x*map.pc+y*map.pd)>>8;
                if(map.wrap){sx&=map.width-1;sy&=map.height-1;}
                uint32_t behind=0;
                if(unsigned(sx)<unsigned(map.width)&&unsigned(sy)<unsigned(map.height))behind=map.raster[unsigned(sy)*map.width+unsigned(sx)];
                const unsigned target_layer=(behind>>24)?map.layer:5u;
                if(!(behind>>24))behind=map.backdrop;
                if((blend>>(8+target_layer))&1u){
                    const unsigned a=object_alpha==17?(alpha&31):object_alpha,b=object_alpha==17?((alpha>>8)&31):16-object_alpha;
                    pixel=alpha_blend(pixel,behind,a,b);
                }
            }
            pixels[i]=pixel|0xff000000u;priority[i]=uint8_t(p);
        }
    }
};
// BUILD134 cost cut: scan_obj's fast paths for this publication (engine B,
// tile OBJ only, whole screen), with the pixel statement inlined (scan_obj
// called it through four `bl` sites) and plain sprites decoded a tile row at
// a time: a row of 8 texels is one or two word loads, skipped when empty,
// instead of a tile address per pixel. Same texels, same pixels, same order
// between sprites (a sprite never writes one pixel twice).
struct NativeAffineSprite {
    int x,y,w,h,bw,bh;bool c256,hflip,vflip,mapping1D;uint32_t tile,boundary,priority;
    int pa,pb,pc,pd;const uint32_t* colors;const uint8_t* vram;
};
template<bool SEMI> inline __attribute__((always_inline)) void native_affine_plain_sprite(const NativeAffineSprite& s,const NativeAffinePut put){
    constexpr uint32_t vram_mask=0x1ffffu;constexpr unsigned alpha=SEMI?17u:0u;
    const int x=s.x,y=s.y,w=s.w,h=s.h;const bool c256=s.c256,hflip=s.hflip,vflip=s.vflip,mapping1D=s.mapping1D;
    const uint32_t tile=s.tile,boundary=s.boundary,priority=s.priority;const uint32_t* const colors=s.colors;const uint8_t* const vram=s.vram;
    const int sx0=std::max(0,-x),sx1=std::min(w,SCREEN_W-x);
    const int sy0=std::max(0,-y),sy1=std::min(h,SCREEN_H-y);
    const unsigned columns=unsigned(w)/8u;const uint32_t step=c256?64u:32u;
    const int direction=hflip?-1:1;
    for(int sy=sy0;sy<sy1;++sy){
        const int py=y+sy,ty=vflip?h-1-sy:sy;
        const unsigned trow=unsigned(ty)>>3,fy=unsigned(ty)&7u;
        // First tile of this sprite row; a texel row never straddles the VRAM
        // wrap (it starts on a multiple of its own size).
        const uint32_t row=(mapping1D?tile*boundary+trow*columns*step:tile*32u+trow*1024u)+fy*(c256?8u:4u);
        const int line=py*SCREEN_W+x;
        auto texel=[&](unsigned index,int sx) __attribute__((always_inline)) {if(index)put.at(unsigned(line+sx),x+sx,py,colors[index],priority,alpha);};
        for(unsigned tcol=0;tcol<columns;++tcol){
            // Texel fx of this tile lands on sprite column first+fx (first-fx when flipped).
            const int first=hflip?w-1-int(tcol)*8:int(tcol)*8;
            const int low=hflip?first-7:first;
            if(low+8<=sx0 || low>=sx1)continue;
            const uint8_t* texels=vram+((row+tcol*step)&vram_mask);
            // A whole opaque tile row: the pixel statement without its blend
            // (object_alpha is 0), at constant offsets from the row's pointers.
            const bool whole=!SEMI && low>=sx0 && low+8<=sx1;
            uint32_t* const out=put.pixels+(whole?line+low:0);uint8_t* const front=put.priority+(whole?line+low:0);
            auto opaque=[&](unsigned index,int column) __attribute__((always_inline)) {
                if(index && priority<=front[column]){out[column]=colors[index]|0xff000000u;front[column]=uint8_t(priority);}
            };
            if(c256){
                const uint32_t w0=ppu_word(texels),w1=ppu_word(texels+4);
                if(!(w0|w1))continue;
                if(whole && !hflip){
                    opaque(w0&255u,0);opaque((w0>>8)&255u,1);opaque((w0>>16)&255u,2);opaque(w0>>24,3);
                    opaque(w1&255u,4);opaque((w1>>8)&255u,5);opaque((w1>>16)&255u,6);opaque(w1>>24,7);
                }else if(whole){
                    opaque(w0&255u,7);opaque((w0>>8)&255u,6);opaque((w0>>16)&255u,5);opaque(w0>>24,4);
                    opaque(w1&255u,3);opaque((w1>>8)&255u,2);opaque((w1>>16)&255u,1);opaque(w1>>24,0);
                }else for(int fx=0;fx<8;++fx){
                    const int sx=first+direction*fx;
                    if(sx>=sx0 && sx<sx1)texel(((fx<4?w0:w1)>>((fx&3)*8))&255u,sx);
                }
            }else{
                const uint32_t word=ppu_word(texels);
                if(!word)continue;
                if(whole && !hflip){
                    opaque(word&15u,0);opaque((word>>4)&15u,1);opaque((word>>8)&15u,2);opaque((word>>12)&15u,3);
                    opaque((word>>16)&15u,4);opaque((word>>20)&15u,5);opaque((word>>24)&15u,6);opaque(word>>28,7);
                }else if(whole){
                    opaque(word&15u,7);opaque((word>>4)&15u,6);opaque((word>>8)&15u,5);opaque((word>>12)&15u,4);
                    opaque((word>>16)&15u,3);opaque((word>>20)&15u,2);opaque((word>>24)&15u,1);opaque(word>>28,0);
                }else for(int fx=0;fx<8;++fx){
                    const int sx=first+direction*fx;
                    if(sx>=sx0 && sx<sx1)texel((word>>(fx*4))&15u,sx);
                }
            }
        }
    }
}
template<bool SEMI> inline __attribute__((always_inline)) void native_affine_rotated_sprite(const NativeAffineSprite& s,const NativeAffinePut put){
    constexpr uint32_t vram_mask=0x1ffffu;constexpr unsigned alpha=SEMI?17u:0u;
    const int x=s.x,y=s.y,w=s.w,h=s.h,bw=s.bw,bh=s.bh,pa=s.pa,pb=s.pb,pc=s.pc,pd=s.pd;const bool c256=s.c256,mapping1D=s.mapping1D;
    const uint32_t tile=s.tile,boundary=s.boundary,priority=s.priority;const uint32_t* const colors=s.colors;const uint8_t* const vram=s.vram;
    const int tiles_per_row=w/8;
    const int sx0=std::max(0,-x),sx1=std::min(bw,SCREEN_W-x);
    for(int sy=0;sy<bh;++sy){
        const int py=y+sy;
        if(py<0 || py>=SCREEN_H || sx0>=sx1)continue;
        const int cy=sy-bh/2;
        int ux=pa*(sx0-bw/2)+pb*cy,uy=pc*(sx0-bw/2)+pd*cy;
        for(int sx=sx0;sx<sx1;++sx,ux+=pa,uy+=pc){
            const int tx=(ux>>8)+w/2,ty=(uy>>8)+h/2;
            if(tx<0 || tx>=w || ty<0 || ty>=h)continue;
            const int tcol=tx>>3,trow=ty>>3,fx=tx&7,fy=ty&7;
            const uint32_t tno=trow*tiles_per_row+tcol;
            unsigned index;
            if(c256){
                index=vram[((mapping1D?tile*boundary+tno*64u:tile*32u+trow*1024u+tcol*64u)+fy*8u+fx)&vram_mask];
                if(!index)continue;
                put(x+sx,py,colors[index],priority,alpha);
            }else{
                const uint8_t b=vram[((mapping1D?tile*boundary+tno*32u:tile*32u+trow*1024u+tcol*32u)+fy*4u+fx/2)&vram_mask];
                index=(fx&1)?(b>>4):(b&0xF);
                if(!index)continue;
                put(x+sx,py,colors[index],priority,alpha);
            }
        }
    }
}
// drawn: the OAM entries with an on-screen box (obj_box), ascending; none is
// a bitmap OBJ. Every other entry draws nothing in scan_obj.
__attribute__((noinline)) void native_affine_objects(const uint8_t* oam,uint32_t dispcnt,const uint8_t* drawn,unsigned count,const NativeAffinePut put){
    if(!(dispcnt&(1u<<12)) || (dispcnt&0x80) || ((dispcnt>>16)&3)==0)return;
    // Resolved like scan_obj: the OBJ palette once, each extended palette on first use.
    static uint32_t obj_colors[256];
    {const auto* raw=static_cast<const uint16_t*>(host_ptr(0x05000600));for(unsigned c=0;c<256;++c)obj_colors[c]=bgr555(raw[c]);}
    const bool ext_palettes=(dispcnt&0x80000000u)!=0;
    static uint32_t ext_colors[16][256];bool ext_resolved[16]{};
    NativeAffineSprite s{};
    s.mapping1D=dispcnt&(1u<<4);s.boundary=32u<<((dispcnt>>20)&3);
    s.vram=static_cast<const uint8_t*>(host_ptr(0x06600000));
    for(unsigned k=count;k--;){               // low index on top: draw high first
        const unsigned i=drawn[k];
        const unsigned a0=oam[i*8]|(oam[i*8+1]<<8),a1=oam[i*8+2]|(oam[i*8+3]<<8),a2=oam[i*8+4]|(oam[i*8+5]<<8);
        const bool affine=a0&0x100;
        const int shape=(a0>>14)&3,size=(a1>>14)&3;
        s.w=kObjSizes[shape][size][0];s.h=kObjSizes[shape][size][1];
        const bool dbl=affine && (a0&0x200);
        s.bw=dbl?s.w*2:s.w;s.bh=dbl?s.h*2:s.h;
        s.x=a1&0x1FF;s.y=a0&0xFF;
        if(s.x>=256)s.x-=512;
        if(s.y>=192 && s.y>=256-s.bh)s.y-=256;
        s.c256=a0&0x2000;s.hflip=!affine && (a1&0x1000);s.vflip=!affine && (a1&0x2000);
        s.tile=a2&0x3FF;s.priority=(a2>>10)&3;
        const unsigned pal=(a2>>12)&0xF;
        if(!s.c256)s.colors=obj_colors+pal*16u;
        else if(!ext_palettes)s.colors=obj_colors;
        else{
            if(!ext_resolved[pal]){
                for(unsigned c=0;c<256;++c)ext_colors[pal][c]=bgr555(obj_ext_color(ENGINE_B,(pal*256+c)*2));
                ext_resolved[pal]=true;
            }
            s.colors=ext_colors[pal];
        }
        const bool semi=((a0>>10)&3)==1;
        if(!affine){
            if(semi)native_affine_plain_sprite<true>(s,put);else native_affine_plain_sprite<false>(s,put);
            continue;
        }
        const unsigned group=(a1>>9)&0x1F;
        auto parameter=[&](unsigned entry){return int(int16_t(oam[(group*4+entry)*8+6]|(oam[(group*4+entry)*8+7]<<8)));};
        s.pa=parameter(0);s.pb=parameter(1);s.pc=parameter(2);s.pd=parameter(3);
        if(semi)native_affine_rotated_sprite<true>(s,put);else native_affine_rotated_sprite<false>(s,put);
    }
}
}
bool ppu_prepare_native_affine(Engine eng,Framebuffer& foreground,PpuGpuAffineLayer& layer){
    if(eng!=ENGINE_B)return false;
    sync_vram_views();
    const auto& em=kEngines[eng];const auto disp=rd32(em.reg_base);
    // Keep unsupported effects/windows on the exact existing path. This
    // publication owns a simple opaque map with foreground BG/OBJ layers.
    const unsigned blend=rd16(em.reg_base+0x50);
    if((disp&0xe080)||((disp>>16)&3)!=1||((blend>>6)&3)>1)return false;
    unsigned map_priority=4,overlay_mask=0;
    for(unsigned bg=0;bg<4;++bg){const auto c=read_bg(em,bg,disp);
        if(c.enabled&&c.extended_tiled)map_priority=std::min(map_priority,unsigned(c.priority));
        else if(c.enabled)overlay_mask|=1u<<bg;}
    if(map_priority==4)return false;
    const auto* oam=static_cast<const uint8_t*>(host_ptr(0x07000400));
    // scan_obj never writes outside obj_box (same OAM rules), so these boxes
    // bound every OBJ pixel of this preparation. One pass over OAM: the
    // admission, the boxes and what the OBJ key needs (BUILD134; the boxes
    // were a second obj_box pass).
    int16_t boxes[128][4];uint8_t drawn[128];unsigned count=0;
    bool bitmap=false,semi=false,extended=false;
    if(disp&(1u<<12)){
        for(unsigned i=0;i<128;++i){
            int x0,x1,y0,y1;if(!obj_box(oam,i,x0,x1,y0,y1))continue;
            const auto a0=uint16_t(oam[i*8]|oam[i*8+1]<<8),a2=uint16_t(oam[i*8+4]|oam[i*8+5]<<8);
            if((((a0>>10)&3)!=0&&((blend>>8)&overlay_mask))||((a2>>10)&3)>map_priority)return false;
            boxes[count][0]=int16_t(x0);boxes[count][1]=int16_t(x1);boxes[count][2]=int16_t(y0);boxes[count][3]=int16_t(y1);
            drawn[count++]=uint8_t(i);
            const unsigned mode=(a0>>10)&3;
            bitmap|=mode==3;semi|=mode==1;extended|=mode!=3 && (a0&0x2000) && (disp&0x80000000u);
        }
    }
    native_affine_consumer=true;
    auto view=ppu_prepare_background_affine(eng);
    native_affine_consumer=false;
    if(!view||!view->gpu_affine)return false;
    const auto& map=*view->gpu_affine;
    NativeAffineTarget* target=nullptr;
    for(auto& t:native_affine_targets)if(t.buffer==&foreground)target=&t;
    if(!target){
        target=native_affine_targets[0].age<=native_affine_targets[1].age?&native_affine_targets[0]:&native_affine_targets[1];
        target->buffer=&foreground;target->valid=false;
    }
    target->age=++native_affine_clock;
    uint8_t* const priority=target->priority;
    unsigned restore=0;
    const bool same=target->valid&&target->overlay==map.overlay&&target->overlay_generation==map.overlay_generation;
    const unsigned alpha=rd16(em.reg_base+0x52);
    // BUILD134 cost cut: this buffer already holds these sprites over this
    // base (see NativeAffineObjInputs): nothing to restore, nothing to draw,
    // the same boxes. sin-recortes.txt keeps the BUILD 133 redraw.
    const bool cuts=ppu_cost_cuts();
    // The map descriptor a blended sprite samples is immutable per generation
    // of the affine publication (a rotating map swaps descriptors).
    const uint64_t serial=cuts?native_affine_obj_serial(oam,disp,blend,alpha,drawn,count,bitmap,semi,extended,&map,view->background_generation):0;
    if(!(serial&&same&&target->obj_serial==serial)){
        if(same)for(unsigned b=0;b<target->boxes;++b)restore+=unsigned(target->box[b][1]-target->box[b][0])*unsigned(target->box[b][3]-target->box[b][2]);
        if(cuts&&!map.overlay){
            if(!same||restore>=SCREEN_W*SCREEN_H/2)native_affine_clear(foreground,priority,0,SCREEN_W,0,SCREEN_H);
            else for(unsigned b=0;b<target->boxes;++b)
                native_affine_clear(foreground,priority,target->box[b][0],target->box[b][1],target->box[b][2],target->box[b][3]);
        }
        else if(!same||restore>=SCREEN_W*SCREEN_H/2)native_affine_base(map.overlay,foreground,priority,0,SCREEN_W,0,SCREEN_H);
        else for(unsigned b=0;b<target->boxes;++b)
            native_affine_base(map.overlay,foreground,priority,target->box[b][0],target->box[b][1],target->box[b][2],target->box[b][3]);
        target->overlay=map.overlay;target->overlay_generation=map.overlay_generation;target->valid=true;
        target->boxes=count;std::memcpy(target->box,boxes,sizeof(boxes[0])*count);
        target->obj_serial=serial;
        static const bool fast_obj=std::getenv("PPU_REFERENCE_OBJ")==nullptr;
        if(cuts)PPU_CUT_COUNT(fast_obj&&!bitmap?4:5);
        if(cuts&&fast_obj&&!bitmap)native_affine_objects(oam,disp,drawn,count,NativeAffinePut{&foreground.px[0][0],priority,&map,blend,alpha});
        else scan_obj(eng,[&](int x,int y,uint32_t pixel,unsigned p,unsigned object_alpha){
            const unsigned i=y*SCREEN_W+x;
            // OBJ wins a priority tie against BG; lower OAM wins OBJ ties.
            if(p<=priority[i]){
                // Only pixels actually occupied by a semi-transparent OBJ need
                // the map's integer CPU sample. Every other map texel stays GPU.
                if(object_alpha&&(!map.overlay||!(map.overlay[i]>>24))){
                    int sx=(map.ref_x+x*map.pa+y*map.pb)>>8,sy=(map.ref_y+x*map.pc+y*map.pd)>>8;
                    if(map.wrap){sx&=map.width-1;sy&=map.height-1;}
                    uint32_t behind=0;
                    if(unsigned(sx)<unsigned(map.width)&&unsigned(sy)<unsigned(map.height))behind=map.raster[unsigned(sy)*map.width+unsigned(sx)];
                    const unsigned target_layer=(behind>>24)?map.layer:5u;
                    if(!(behind>>24))behind=map.backdrop;
                    if((blend>>(8+target_layer))&1u){
                        const unsigned a=object_alpha==17?(alpha&31):object_alpha,b=object_alpha==17?((alpha>>8)&31):16-object_alpha;
                        pixel=alpha_blend(pixel,behind,a,b);
                    }
                }
                foreground.px[y][x]=pixel|0xff000000u;priority[i]=uint8_t(p);
            }
        });
    }
    else PPU_CUT_COUNT(3);
    layer=map;layer.overlay=nullptr;layer.overlay_generation=0;
    return true;
}
// BUILD129: the 8-row bands (bit n = rows 8n..8n+7) the OBJs of this
// buffer's last preparation may cover, and the overlay base they were drawn
// over. Outside these bands the buffer equals that base exactly (scan_obj
// never writes outside obj_box), so two publications over the same base
// differ only inside the union of their bands: the map upload compares those
// instead of all 24 bands (~2.4 ms per image on the Old 3DS). Unknown: ~0.
uint32_t ppu_native_affine_bands(const Framebuffer& foreground,const uint32_t** base,uint64_t* base_generation){
    for(const auto& t:native_affine_targets)if(t.buffer==&foreground&&t.valid){
        uint32_t bands=0;
        for(unsigned b=0;b<t.boxes;++b){
            const int y0=std::max<int>(t.box[b][2],0),y1=std::min<int>(t.box[b][3],SCREEN_H);
            for(int band=y0/8;y0<y1&&band<=(y1-1)/8;++band)bands|=1u<<band;
        }
        *base=t.overlay;*base_generation=t.overlay_generation;return bands;
    }
    *base=nullptr;*base_generation=0;return ~0u;
}
