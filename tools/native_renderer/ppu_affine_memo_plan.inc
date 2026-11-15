        // BUILD134 cost cut: what a hit of this memo compares (the compare is
        // in ppu_affine_memo.inc): every span byte outside the character
        // blocks, every map, and the tiles each map names in its own block.
        memo_plan.valid=false;
        if(memo.valid && ppu_memo_cut()){
            try{
                static HostVector<BgInputSpan> need_storage[2];auto& need=need_storage[eng];need.clear();
                BgInputSpan blocks[4];unsigned block_count=0;
                auto layer=[&](const BgConfig& bg){
                    const unsigned tile_bytes=bg.bpp8?64u:32u;const size_t map_bytes=size_t(bg.tiles_w)*bg.tiles_h*2;
                    blocks[block_count++]={bg.chars,size_t(tile_bytes)*1024u};
                    need.push_back({bg.screen,map_bytes});
                    uint32_t used[32]{};
                    const auto* map=static_cast<const uint8_t*>(host_ptr(bg.screen));
                    for(size_t i=0;i<map_bytes;i+=2){const unsigned tile=(map[i]|(unsigned(map[i+1])<<8))&0x3ffu;used[tile>>5]|=1u<<(tile&31);}
                    auto named=[&](unsigned tile){return ((used[tile>>5]>>(tile&31))&1u)!=0;};
                    for(unsigned tile=0;tile<1024;){
                        if(!named(tile)){++tile;continue;}
                        unsigned last=tile;while(last+1<1024 && named(last+1))++last;
                        need.push_back({bg.chars+tile*tile_bytes,size_t(last+1-tile)*tile_bytes});
                        tile=last+1;
                    }
                };
                layer(c);for(unsigned k=0;k<overlays;++k)layer(ocfg[k]);
                std::sort(blocks,blocks+block_count,[](const BgInputSpan& a,const BgInputSpan& b){return a.address<b.address;});
                for(const auto& span:memo_spans){
                    uint32_t at=span.address;const uint32_t end=span.address+uint32_t(span.size);
                    for(unsigned b=0;b<block_count && at<end;++b){
                        const uint32_t first=blocks[b].address,last=first+uint32_t(blocks[b].size);
                        if(last<=at)continue;
                        if(first>=end)break;
                        if(first>at)need.push_back({at,size_t(first-at)});
                        at=last;
                    }
                    if(at<end)need.push_back({at,size_t(end-at)});
                }
                std::sort(need.begin(),need.end(),[](const BgInputSpan& a,const BgInputSpan& b){return a.address<b.address;});
                auto& ranges=memo_plan.ranges;ranges.clear();
                for(const auto& n:need){
                    if(!n.size)continue;
                    if(!ranges.empty() && n.address<=ranges.back().address+ranges.back().size){
                        auto& previous=ranges.back();
                        previous.size=std::max(previous.size,uint32_t(n.address-previous.address)+uint32_t(n.size));
                    }else ranges.push_back({n.address,uint32_t(n.size),0u});
                }
                bool located=true;
                for(auto& range:ranges){
                    size_t offset=0;bool found=false;
                    for(const auto& span:memo_spans){
                        if(range.address>=span.address && size_t(range.address-span.address)+range.size<=span.size){
                            range.offset=uint32_t(offset+(range.address-span.address));found=true;break;
                        }
                        offset+=span.size;
                    }
                    located=located && found;
                }
                memo_plan.valid=located;
                if(located)PPU_CUT_COUNT(2);
            }catch(const std::bad_alloc&){memo_plan.valid=false;}
        }
