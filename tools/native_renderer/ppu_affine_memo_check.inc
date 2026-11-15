    if(affine_expect[eng]){
        affine_expect[eng]=false;++affine_checks;const auto& e=affine_expected[eng];const auto& g=s.layer[s.current];
        const bool equal=e.width==g.width && e.height==g.height && e.raster_generation==g.raster_generation && e.ref_x==g.ref_x && e.ref_y==g.ref_y &&
            e.pa==g.pa && e.pb==g.pb && e.pc==g.pc && e.pd==g.pd && e.wrap==g.wrap && e.backdrop==g.backdrop && e.layer==g.layer &&
            e.priority==g.priority && e.overlay==g.overlay && e.overlay_generation==g.overlay_generation && e.effect_mode==g.effect_mode &&
            e.effect_factor==g.effect_factor && e.effect_layer==g.effect_layer && e.effect_backdrop==g.effect_backdrop;
        if(!equal)++affine_mismatches;
        if(!equal || affine_checks%64==1)std::printf("PPU affine fast check: checks=%u mismatches=%u\n",affine_checks,affine_mismatches);
    }
