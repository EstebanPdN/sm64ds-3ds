        bool same=memo.valid && memo.spans.size()==merged;
        for(unsigned i=0;same && i<merged;++i)same=memo.spans[i].address==memo_spans[i].address && memo.spans[i].size==memo_spans[i].size;
        // BUILD129: a rotating or scrolling map changes only its affine BG's
        // PA..PD/X/Y registers (16 bytes). Without a mode-1 overlay blend
        // (whose plane samples the affine layer) every other input equals
        // the memo, so raster, palettes and overlay are the current
        // descriptor's: rebuild only its affine fields instead of converting
        // palettes, comparing every layer and re-snapshotting ~100 KB of
        // inputs (BUILD123 hardware: ~1.5 ms per turning update).
        // PPU_NO_AFFINE_FAST=1 disables it; PPU_AFFINE_FAST_CHECK=1 also runs
        // the full path and counts descriptor differences.
        static const bool affine_fast=std::getenv("PPU_NO_AFFINE_FAST")==nullptr;
        static const bool affine_check=std::getenv("PPU_AFFINE_FAST_CHECK")!=nullptr;
        const uint32_t affine_regs=em.reg_base+0x20u+uint32_t(only>=2?only-2:0)*0x10u;
        bool affine_only=false;size_t affine_offset=0;
        size_t offset=0;
        // BUILD134 cost cut: a hit compared every span whole (~73 KB for one
        // 8bpp map: 64 KB of characters) with the library memcmp. The plan
        // built with the memo (ppu_affine_memo_plan.inc) names what a map can
        // show: registers, VRAMCNT, palettes, every map, and only the tiles
        // those maps name; compared by words with the lines requested ahead.
        // Bytes of a character block no map entry names never reach a pixel:
        // update_text_layer finds none of its map entries using a changed
        // tile, rasters nothing and keeps the generation, so the full path
        // would have published this same descriptor. Its incremental tile
        // update is what absorbs them later (PPU_NO_TILE_INCREMENTAL keeps
        // the whole compare). Any other difference misses as before.
        // The one case where the full path would not have matched: another
        // engine-B configuration re-snapshotted these BG-only layer caches
        // since the memo was stored (ppu_prepare_background_candidates_cached
        // with the map off) and such a byte changed meanwhile. It would
        // re-raster the same texels under a new generation (or refuse once,
        // after a colour snapshot); the hit keeps the current descriptor,
        // whose raster was built from these very maps, tiles and palettes.
        if(same && ppu_memo_cut() && memo_plan.valid){
            PPU_CUT_COUNT(0);
            for(const auto& range:memo_plan.ranges){
                const auto* live=static_cast<const uint8_t*>(host_ptr(range.address));const uint8_t* kept=memo.bytes.data()+range.offset;
                if(ppu_equal_bytes(kept,live,range.size))continue;
                const bool inside=affine_fast && !blend1 && only>=2 && !affine_only &&
                    range.address<=affine_regs && size_t(affine_regs-range.address)+16u<=range.size;
                const size_t at=inside?size_t(affine_regs-range.address):0;
                if(inside && !std::memcmp(kept,live,at) && !std::memcmp(kept+at+16,live+at+16,range.size-at-16)){affine_only=true;affine_offset=range.offset+at;}
                else{same=false;break;}
            }
            if(same)PPU_CUT_COUNT(1);
        }else
        for(unsigned i=0;same && i<merged;++i){
            const auto& span=memo_spans[i];
            const auto* live=static_cast<const uint8_t*>(host_ptr(span.address));const uint8_t* kept=memo.bytes.data()+offset;
            if(std::memcmp(kept,live,span.size)!=0){
                const bool inside=affine_fast && !blend1 && only>=2 && !affine_only &&
                    span.address<=affine_regs && size_t(affine_regs-span.address)+16u<=span.size;
                const size_t at=inside?size_t(affine_regs-span.address):0;
                if(inside && !std::memcmp(kept,live,at) && !std::memcmp(kept+at+16,live+at+16,span.size-at-16)){affine_only=true;affine_offset=offset+at;}
                else same=false;
            }
            offset+=span.size;
        }
        if(same && affine_only){
            same=false; // unless the descriptor can be rebuilt below
            if(s.generation && s.source[s.current]==cache.layers[only].generation){
                const unsigned target=s.current^1;const auto& cur=s.layer[s.current];
                PpuGpuAffineLayer next=cur;
                next.ref_x=c.ref_x;next.ref_y=c.ref_y;next.pa=int16_t(c.pa);next.pb=int16_t(c.pb);next.pc=int16_t(c.pc);next.pd=int16_t(c.pd);
                const bool unchanged=cur.ref_x==next.ref_x && cur.ref_y==next.ref_y && cur.pa==next.pa && cur.pb==next.pb && cur.pc==next.pc && cur.pd==next.pd;
                // Raster bytes already in the other buffer: the full path would
                // only swap descriptors, with no band dirty.
                const bool swap=!unchanged && s.source[target]==s.source[s.current] && s.raster[target].size()==s.raster[s.current].size();
                if(affine_check && (unchanged || swap)){affine_expected[eng]=next;affine_expect[eng]=true;}
                else if(unchanged || swap){
                    if(swap){next.raster=s.raster[target].data();next.base_generation=s.source[s.current];next.dirty_bands[0]=next.dirty_bands[1]=0;
                        s.layer[target]=next;s.current=target;++s.generation;}
                    std::memcpy(memo.bytes.data()+affine_offset,host_ptr(affine_regs),16);
@MEMO_HIT_RETURN@
                }
            }
        }
        if(same && s.generation){
