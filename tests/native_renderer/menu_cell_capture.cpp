#include "menu_cell_capture.hpp"
#include "menu_oam_hooks.hpp"
#include <cassert>
#include <cmath>
using namespace sm64ds::render;
using namespace sm64ds::render::game_hooks;
struct Output {ImageLayer image;unsigned calls=0;bool accept=true;
    static bool submit(const ImageLayer& image,void* user){auto& out=*static_cast<Output*>(user);++out.calls;out.image=image;return out.accept;}
};
int main(){
    assert(menu_cell_affine_identity({4096,0,0,4096}));
    assert(menu_cell_affine_identity({4111,15,15,4111}));
    assert(!menu_cell_affine_identity({4112,0,0,4096}));
    assert(!menu_cell_affine_identity({4096,-1,0,4096}));
    assert(!menu_cell_affine_identity({2048,0,0,4096}));
    assert(!menu_cell_affine_identity({0,-4096,4096,0}));
    // A full source-word wrap leaves precisely the same hardware8.8 matrix.
    assert(menu_cell_affine_identity({4096+(1<<20),0,0,4096}));

    Screen screen=Screen::top;std::uint16_t order=777;
    assert(!menu_presentation_mode(0,screen,order)&&screen==Screen::top&&order==777);
    for(unsigned mode=1;mode<=6;++mode){assert(menu_presentation_mode(mode,screen,order));
        assert(screen==Screen::bottom&&order==((mode==4||mode==5)?2432:1408));
        assert(menu_presentation_mode(mode,screen,order,Screen::top));
        assert(screen==Screen::top&&order==((mode==4||mode==5)?2432:1408));}
    assert(!menu_presentation_mode(7,screen,order));
    MenuCellPart parts[]={{-16,-8,16,16},{0,-8,16,16}};
    MenuCellImage cell{parts,2,3,-16,-8,32,16,128};
    MenuCellBank bank{0,4096,&cell,1,32,1,256,2,64,3};
    assert(validate(MenuCellCatalog{&bank,1})==Error::none);
    ImageAsset assets[4]={{8,8,ImageBank::Alpha::independent},{8,8,ImageBank::Alpha::independent},
        {8,8,ImageBank::Alpha::independent},{32,16,ImageBank::Alpha::independent}};
    ImageBankBinding image_bank{4096,{},assets,4};ImageCatalog images;
    assert(ImageCatalog::open(&image_bank,1,images)==Error::none&&validate(MenuCellCatalog{&bank,1},images)==Error::none);
    image_bank.source_sha256[0]=1;assert(validate(MenuCellCatalog{&bank,1},images)==Error::invalid_data);image_bank.source_sha256[0]=0;
    assets[3].width=64;assert(validate(MenuCellCatalog{&bank,1},images)==Error::invalid_data);assets[3].width=32;
    image_bank.images=3;assert(validate(MenuCellCatalog{&bank,1},images)==Error::invalid_data);image_bank.images=4;
    assert(validate(MenuCellCatalog{&bank,1},ImageCatalog{})==Error::invalid_data);
    cell.character_end=257;assert(validate(MenuCellCatalog{&bank,1})==Error::invalid_data);cell.character_end=128;
    parts[0].x=-17;assert(validate(MenuCellCatalog{&bank,1})==Error::invalid_data);parts[0].x=-16;
    Output out;MenuCellCapture capture;MenuCellEvent event;event.bank=&bank;event.x=128;event.y=136;event.screen=Screen::bottom;event.slot=4;
    assert(capture.part(event)==Error::invalid_phase&&capture.begin(Output::submit,&out)==Error::none);
    assert(capture.begin(Output::submit,&out)==Error::invalid_phase);
    assert(capture.part(event)==Error::none&&out.calls==0);
    assert(capture.finish()==Error::invalid_data&&out.calls==0);
    const auto complete=[&]{assert(capture.part(event)==Error::none);++event.part;++event.slot;
        assert(capture.part(event)==Error::none);event.part=0;--event.slot;};
    assert(capture.begin(Output::submit,&out)==Error::none);complete();
    assert(out.calls==1&&out.image.bank_id==4096&&out.image.image==3&&out.image.order==3707);
    assert(out.image.rectangle.x==140&&out.image.rectangle.y==160&&out.image.rectangle.width==40&&out.image.rectangle.height==20);
    assert(out.image.material.uv[0]==1&&out.image.material.uv[2]==1.f/2048&&capture.finish()==Error::none);
    // Mirroring includes transparent padding and chooses the matching side of
    // nearest-sampling ties. No original part pixels are read here.
    event.flip=0x30000000u;cell.left=-8;parts[0].x=-8;parts[1].x=8;
    assert(capture.begin(Output::submit,&out)==Error::none);complete();
    assert(out.image.rectangle.x==130&&out.image.material.uv[0]==-1&&out.image.material.uv[2]==1-1.f/2048);
    assert(out.image.material.uv[4]==-1&&out.image.material.uv[5]==1-1.f/1024&&capture.finish()==Error::none);
    event.flip=0;cell.left=-16;parts[0].x=-16;parts[1].x=0;
    const auto rejected=[&](Error error){const auto before=out.calls;assert(capture.begin(Output::submit,&out)==Error::none);
        assert(capture.part(event)==error&&capture.part(event)==error&&capture.finish()==error&&out.calls==before);};
    event.affine=0;rejected(Error::unsupported);event.affine=-1;
    event.mosaic=true;rejected(Error::unsupported);event.mosaic=false;
    event.flip=1;rejected(Error::unsupported);event.flip=0;
    event.y=220;assert(capture.begin(Output::submit,&out)==Error::none);const auto hidden_calls=out.calls;
    complete();assert(capture.finish()==Error::none&&out.calls==hidden_calls);
    event.y=264;assert(capture.begin(Output::submit,&out)==Error::none);complete();
    assert(capture.finish()==Error::none&&out.image.rectangle.y==0&&out.image.tone_target==16);event.y=136;
    event.slot=127;rejected(Error::invalid_data);event.slot=4;
    event.part=1;rejected(Error::invalid_data);event.part=0;
    assert(capture.begin(Output::submit,&out)==Error::none&&capture.part(event)==Error::none);
    event.part=1;event.slot=6;assert(capture.part(event)==Error::invalid_data&&capture.finish()==Error::invalid_data);
    event.part=0;event.slot=4;out.accept=false;
    assert(capture.begin(Output::submit,&out)==Error::none&&capture.part(event)==Error::none);
    event.part=1;event.slot=5;assert(capture.part(event)==Error::invalid_data&&capture.finish()==Error::invalid_data);
    out.accept=true;event.part=0;event.slot=4;event.screen=Screen::top;event.priority=3;
    assert(capture.begin(Output::submit,&out)==Error::none);complete();
    assert(out.image.rectangle.x==180&&out.image.order==635&&capture.finish()==Error::none);
    // Diagnostic leniency rejects an unsupported whole cell without hiding
    // later valid cells. The frame still reports its incomplete coverage.
    capture.set_lenient(true);assert(capture.begin(Output::submit,&out)==Error::none);
    event.affine=15;assert(capture.part(event)==Error::unsupported);
    event.part=1;++event.slot;assert(capture.part(event)==Error::unsupported);
    event.part=0;--event.slot;event.affine=-1;const auto previous=out.calls;complete();
    assert(out.calls==previous+1&&capture.finish()==Error::unsupported);
}
