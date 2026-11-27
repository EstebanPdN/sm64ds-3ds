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
        // BUILD142: the opening's clouds are GPU text layers (BUILD141) that
        // scroll every update: only their BGxHOFS/VOFS registers (4 bytes
        // each) change. With the published descriptor layered, those bytes
        // become the layers' offsets, like the affine registers above become
        // its transform; the full path (palette conversion and layer
        // compares, 3.8-4.7 ms per update on the Old 3DS at half clock) only
        // swapped descriptors. Windows: bytes allowed to differ.
        const bool scroll_fast=affine_fast && s.generation && overlays && s.layer[s.current].text_count==overlays &&
            native_affine_consumer && native_affine_layers;
        struct Window {uint32_t address,size;unsigned bit;};
        Window windows[4];unsigned window_count=0;
        if(scroll_fast)for(unsigned k=0;k<overlays;++k)windows[window_count++]={em.reg_base+0x10u+uint32_t(overlay_bg[k])*4u,4u,2u};
        if(affine_fast && !blend1 && only>=2)windows[window_count++]={affine_regs,16u,1u};
        std::sort(windows,windows+window_count,[](const Window& a,const Window& b){return a.address<b.address;});
        unsigned changed_windows=0;
        struct Refresh {uint8_t* kept;const uint8_t* live;uint32_t size;};
        Refresh refresh[4];unsigned refresh_count=0;
        // Every byte outside the windows equal; the windows that differ are
        // noted, with the memo bytes to refresh if the fast path is taken.
        auto only_windows=[&](uint32_t address,uint8_t* kept,const uint8_t* live,size_t size)->bool{
            size_t at=0;
            for(unsigned w=0;w<window_count;++w){
                const auto& win=windows[w];
                if(win.address+win.size<=address || win.address>=address+size)continue;
                const size_t start=win.address>address?size_t(win.address-address):0;
                const size_t end=std::min(size,size_t(win.address+win.size-address));
                if(start>at && std::memcmp(kept+at,live+at,start-at))return false;
                if(std::memcmp(kept+start,live+start,end-start)){
                    if(refresh_count==4)return false;
                    changed_windows|=win.bit;refresh[refresh_count++]={kept+start,live+start,uint32_t(end-start)};
                }
                at=std::max(at,end);
            }
            return at>=size || !std::memcmp(kept+at,live+at,size-at);
        };
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
                const auto* live=static_cast<const uint8_t*>(host_ptr(range.address));uint8_t* kept=memo.bytes.data()+range.offset;
                if(ppu_equal_bytes(kept,live,range.size))continue;
                if(!window_count || !only_windows(range.address,kept,live,range.size)){
                    for(uint32_t b=0;b<range.size;++b)if(kept[b]!=live[b]){ppu_affine_memo_miss=range.address+b;break;}
                    same=false;break;}
            }
            if(same)PPU_CUT_COUNT(1);
        }else
        for(unsigned i=0;same && i<merged;++i){
            const auto& span=memo_spans[i];
            const auto* live=static_cast<const uint8_t*>(host_ptr(span.address));uint8_t* kept=memo.bytes.data()+offset;
            if(std::memcmp(kept,live,span.size)!=0 && (!window_count || !only_windows(span.address,kept,live,span.size))){
                for(uint32_t b=0;b<span.size;++b)if(kept[b]!=live[b]){ppu_affine_memo_miss=span.address+b;break;}
                same=false;}
            offset+=span.size;
        }
        if(same && changed_windows){
            same=false; // unless the descriptor can be rebuilt below
            if(s.generation && s.source[s.current]==cache.layers[only].generation){
                const unsigned target=s.current^1;const auto& cur=s.layer[s.current];
                PpuGpuAffineLayer next=cur;
                if(changed_windows&1u){next.ref_x=c.ref_x;next.ref_y=c.ref_y;next.pa=int16_t(c.pa);next.pb=int16_t(c.pb);next.pc=int16_t(c.pc);next.pd=int16_t(c.pd);}
                if(changed_windows&2u){
                    // The full path's layer order: front to back by priority,
                    // then BG index; text[k] is the k-th layer from the back.
                    int order[3]{};for(unsigned k=0;k<overlays;++k)order[k]=overlay_bg[k];
                    for(unsigned i=1;i<overlays;++i)for(unsigned j=i;j>0 && (configs[order[j]].priority<configs[order[j-1]].priority ||
                        (configs[order[j]].priority==configs[order[j-1]].priority && order[j]<order[j-1]));--j)std::swap(order[j],order[j-1]);
                    for(unsigned k=0;k<overlays;++k){const auto& o=configs[order[overlays-1-k]];next.text[k].hofs=o.hofs;next.text[k].vofs=o.vofs;}
                }
                const bool unchanged=cur.ref_x==next.ref_x && cur.ref_y==next.ref_y && cur.pa==next.pa && cur.pb==next.pb && cur.pc==next.pc && cur.pd==next.pd &&
                    text_layers_equal(cur,next);
                // Raster bytes already in the other buffer: the full path would
                // only swap descriptors, with no band dirty.
                const bool swap=!unchanged && s.source[target]==s.source[s.current] && s.raster[target].size()==s.raster[s.current].size();
                if(affine_check && changed_windows==1u && (unchanged || swap)){affine_expected[eng]=next;affine_expect[eng]=true;}
                else if(unchanged || swap){
                    if(swap){next.raster=s.raster[target].data();next.base_generation=s.source[s.current];next.dirty_bands[0]=next.dirty_bands[1]=0;
                        s.layer[target]=next;s.current=target;++s.generation;}
                    for(unsigned k=0;k<refresh_count;++k)std::memcpy(refresh[k].kept,refresh[k].live,refresh[k].size);
                    if(changed_windows&2u)++ppu_affine_scroll_hits;
@MEMO_HIT_RETURN@
                }
            }
        }
        if(same && s.generation){
