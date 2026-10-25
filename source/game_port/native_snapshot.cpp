#include "sm64ds/native_snapshot.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#ifdef __3DS__
#include "sm64ds/game_heap.hpp"
#include "sm64ds/native_video.hpp"
#include <3ds.h>
extern "C" {
extern unsigned char sm64ds_diagnostic_rw_start[],sm64ds_diagnostic_rw_end[];
extern void* __ctru_heap;
extern void* __ctru_linear_heap;
struct SnapshotRuntimeSection {
    const unsigned char* destination;
    std::uint32_t module,address,size,zero;
};
extern const SnapshotRuntimeSection sm64ds_runtime_sections[],sm64ds_runtime_sections_end[];
}
static_assert(sizeof(SnapshotRuntimeSection)==20,"Generated runtime table is five ARM32 words");
static_assert(sizeof(std::uintptr_t)==4,"Native snapshot identities are ARM32");
#endif

namespace sm64ds {
namespace {
constexpr std::size_t max_spans=5,max_views=272;
#ifdef __3DS__
constexpr std::uint32_t no_module=UINT32_MAX;
#endif
constexpr std::uint32_t header_bytes=32,span_descriptor_bytes=16,view_descriptor_bytes=28;
bool bounded_text(const char* text,std::size_t limit) {
    if(!text)return false;
    for(std::size_t i=0;i<=limit;++i)if(!text[i])return true;
    return false;
}
bool valid_range(std::uint32_t address,std::uint32_t size) {
    return address && size && size<=UINT32_MAX-address;
}
bool contained(std::uint32_t base,std::uint32_t bytes,std::uint32_t address,std::uint32_t size) {
    return address>=base && size<=bytes && address-base<=bytes-size;
}
std::size_t owner(const NativeSnapshotSpan* spans,std::size_t count,
                  std::uint32_t address,std::uint32_t size) {
    for(std::size_t i=0;i<count;++i)
        if(contained(spans[i].address,spans[i].size,address,size))return i;
    return count;
}
enum class StackCoverage {unavailable,existing,additional};
// OS mapping boundaries describe residency, not exclusive stack allocation.
// A large mapping already captured is safe to index without adding any bytes.
[[maybe_unused]] StackCoverage stack_coverage(const NativeSnapshotSpan* spans,std::size_t count,
    std::uint32_t address,std::uint32_t size,std::uint32_t probe) {
    if(!valid_range(address,size) || !contained(address,size,probe,1))return StackCoverage::unavailable;
    if(owner(spans,count,address,size)<count)return StackCoverage::existing;
    if(size>2*1024*1024 || count>=max_spans)return StackCoverage::unavailable;
    for(std::size_t i=0;i<count;++i)
        if(address<spans[i].address+spans[i].size && spans[i].address<address+size)
            return StackCoverage::unavailable;
    return StackCoverage::additional;
}
bool le32(FILE* file,std::uint32_t value) {
    const unsigned char bytes[]={static_cast<unsigned char>(value),
        static_cast<unsigned char>(value>>8),static_cast<unsigned char>(value>>16),
        static_cast<unsigned char>(value>>24)};
    return std::fwrite(bytes,1,sizeof bytes,file)==sizeof bytes;
}
bool json_text(FILE* file,const char* value) {
    if(std::fputc('"',file)==EOF)return false;
    for(const unsigned char* p=reinterpret_cast<const unsigned char*>(value);*p;++p) {
        if(*p=='"' || *p=='\\') {
            if(std::fputc('\\',file)==EOF || std::fputc(*p,file)==EOF)return false;
        } else if(*p<32 || *p>=127) {
            if(std::fprintf(file,"\\u%04x",unsigned(*p))<0)return false;
        } else if(std::fputc(*p,file)==EOF)return false;
    }
    return std::fputc('"',file)!=EOF;
}
}

const char* native_snapshot_status_name(NativeSnapshotStatus status) {
    switch(status) {
    case NativeSnapshotStatus::complete:return "complete";
    case NativeSnapshotStatus::invalid_argument:return "invalid_argument";
    case NativeSnapshotStatus::invalid_range:return "invalid_range";
    case NativeSnapshotStatus::uncovered_view:return "uncovered_view";
    case NativeSnapshotStatus::open_failed:return "open_failed";
    case NativeSnapshotStatus::write_failed:return "write_failed";
    case NativeSnapshotStatus::close_failed:return "close_failed";
    case NativeSnapshotStatus::unavailable:return "unavailable";
    }
    return "unknown";
}

NativeSnapshotResult native_snapshot_write_ranges(
    const char* binary_path,const char* index_path,
    const NativeSnapshotSpan* spans,std::size_t span_count,
    const NativeSnapshotView* views,std::size_t view_count,
    const NativeSnapshotMetadata& metadata) {
    NativeSnapshotResult result;
    if(!binary_path || !index_path || !std::strcmp(binary_path,index_path) ||
       !spans || !span_count || span_count>max_spans || view_count>max_views ||
       (view_count && !views) || !bounded_text(metadata.phase,160))return result;
    std::array<std::uint32_t,max_spans> offsets{};
    std::uint64_t total=header_bytes+span_count*span_descriptor_bytes+view_count*view_descriptor_bytes;
    for(std::size_t i=0;i<span_count;++i) {
        const auto& span=spans[i];
        if(!span.data || !bounded_text(span.name,96))return result;
        if(!valid_range(span.address,span.size)) {result.status=NativeSnapshotStatus::invalid_range;return result;}
        for(std::size_t j=0;j<i;++j) {
            if(span.address<spans[j].address+spans[j].size &&
               spans[j].address<span.address+span.size) {
                result.status=NativeSnapshotStatus::invalid_range;return result;
            }
        }
        offsets[i]=static_cast<std::uint32_t>(total);total+=span.size;
        if(total>UINT32_MAX) {result.status=NativeSnapshotStatus::invalid_range;return result;}
    }
    for(std::size_t i=0;i<view_count;++i) {
        const auto& view=views[i];
        if(!bounded_text(view.name,96))return result;
        if(!valid_range(view.address,view.size)) {result.status=NativeSnapshotStatus::invalid_range;return result;}
        if(owner(spans,span_count,view.address,view.size)==span_count) {
            result.status=NativeSnapshotStatus::uncovered_view;return result;
        }
    }
    // Invalidate a prior completed index before replacing any payload bytes.
    // A reused prefix followed by short I/O must never expose an old complete
    // manifest describing a new partial binary.
    FILE* index=std::fopen(index_path,"w");
    if(!index) {result.status=NativeSnapshotStatus::open_failed;return result;}
    std::array<char,4096> index_buffer{};
    std::setvbuf(index,index_buffer.data(),_IOFBF,index_buffer.size());
    FILE* binary=std::fopen(binary_path,"wb");
    if(!binary) {std::fclose(index);result.status=NativeSnapshotStatus::open_failed;return result;}
    // Do not allocate another stdio buffer within the heap being captured.
    std::setvbuf(binary,nullptr,_IONBF,0);
    bool good=std::fwrite("SM64RAM1",1,8,binary)==8;
    for(const auto value:std::array<std::uint32_t,6>{1,offsets[0],
        static_cast<std::uint32_t>(span_count),static_cast<std::uint32_t>(view_count),
        static_cast<std::uint32_t>(total),0})good=good && le32(binary,value);
    for(std::size_t i=0;i<span_count && good;++i) {
        for(const auto value:{spans[i].address,spans[i].size,offsets[i],static_cast<std::uint32_t>(i)})
            good=good && le32(binary,value);
    }
    for(std::size_t i=0;i<view_count && good;++i) {
        const auto& view=views[i];const auto parent=owner(spans,span_count,view.address,view.size);
        for(const auto value:{view.address,view.size,offsets[parent]+view.address-spans[parent].address,
            static_cast<std::uint32_t>(parent),view.module,view.original_address,view.flags})
            good=good && le32(binary,value);
    }
    if(good)result.binary_bytes=offsets[0];
    for(std::size_t i=0;i<span_count && good;++i) {
        const auto* data=static_cast<const unsigned char*>(spans[i].data);
        for(std::uint32_t offset=0;offset<spans[i].size;) {
            const auto chunk=std::min<std::uint32_t>(65536,spans[i].size-offset);
            const auto written=std::fwrite(data+offset,1,chunk,binary);
            result.binary_bytes+=static_cast<std::uint32_t>(written);
            if(written!=chunk) {good=false;break;}
            offset+=chunk;
        }
    }
    const bool binary_closed=std::fclose(binary)==0;
    if(!good || !binary_closed) {
        std::fclose(index);
        result.status=good?NativeSnapshotStatus::close_failed:NativeSnapshotStatus::write_failed;return result;
    }
    good=std::fprintf(index,"{\"format\":\"SM64RAM1\",\"version\":1,\"byte_order\":\"little\","
        "\"descriptor_address_bits\":32,\"tick\":%llu,\"presentation\":%llu,\"phase\":",
        static_cast<unsigned long long>(metadata.tick),static_cast<unsigned long long>(metadata.presentation))>=0;
    good=good && json_text(index,metadata.phase);
    good=good && std::fprintf(index,",\"runtime_section_count\":%u,\"root_header_address\":%u,"
        "\"root_available\":%s,\"linear_available\":%s,\"stack_probe_address\":%u,"
        "\"stack_available\":%s,\"stack_probe_kind\":\"local variable in snapshot call; not original fault PC\","
        "\"thread_tls_address\":%u,\"thread_tls_available\":%s,"
        "\"thread_tls_kind\":\"calling thread TPIDRURW; 512 CPU-visible bytes including libctru and IPC state\","
        "\"stack_range_kind\":\"OS readable mapping containing probe; not exclusive stack bounds\",\"binary_bytes\":%u,"
        "\"consistency\":\"Caller quiesces game and synchronizes GPU; sequential CPU-visible bytes. "
        "Service threads and snapshot I/O bookkeeping may change during streaming. "
        "Linear GPU-written cache lines are not invalidated or claimed coherent.\","
        "\"alias_information\":\"Original section views give module and DS source address. "
        "Video views index physical native storage; VRAM bank aliases are derived from dumped registers.\",\"spans\":[",
        unsigned(metadata.runtime_sections),unsigned(metadata.root_header),metadata.root_available?"true":"false",
        metadata.linear_available?"true":"false",unsigned(metadata.stack_probe_address),
        metadata.stack_available?"true":"false",unsigned(metadata.thread_tls_address),
        metadata.thread_tls_available?"true":"false",unsigned(result.binary_bytes))>=0;
    for(std::size_t i=0;i<span_count && good;++i) {
        good=std::fprintf(index,"%s{\"id\":%u,\"name\":",i?",":"",unsigned(i))>=0 && json_text(index,spans[i].name);
        good=good && std::fprintf(index,",\"address\":%u,\"size\":%u,\"file_offset\":%u}",
            unsigned(spans[i].address),unsigned(spans[i].size),unsigned(offsets[i]))>=0;
    }
    good=good && std::fprintf(index,"],\"views\":[")>=0;
    for(std::size_t i=0;i<view_count && good;++i) {
        const auto& view=views[i];const auto parent=owner(spans,span_count,view.address,view.size);
        good=std::fprintf(index,"%s{\"id\":%u,\"name\":",i?",":"",unsigned(i))>=0 && json_text(index,view.name);
        good=good && std::fprintf(index,",\"address\":%u,\"size\":%u,\"file_offset\":%u,\"span\":%u,"
            "\"module\":%u,\"original_address\":%u,\"flags\":%u,\"zero_fill_section\":%s}",
            unsigned(view.address),unsigned(view.size),unsigned(offsets[parent]+view.address-spans[parent].address),
            unsigned(parent),unsigned(view.module),unsigned(view.original_address),unsigned(view.flags),
            (view.flags&1)?"true":"false")>=0;
    }
    good=good && std::fprintf(index,"],\"io_status\":\"complete\"}\n")>=0 && !std::ferror(index);
    const bool index_closed=std::fclose(index)==0;
    result.status=good && index_closed?NativeSnapshotStatus::complete:
        good?NativeSnapshotStatus::close_failed:NativeSnapshotStatus::write_failed;
    // Failed buffered index writes/close must not publish a complete marker.
    // This path names the index file we just created, never a game resource.
    if(!result.okay())std::remove(index_path);
    return result;
}

bool native_snapshot_write(const char* prefix,std::uint64_t tick,
    std::uint64_t presentation,const char* phase,std::string& error) {
#ifdef __3DS__
    if(!bounded_text(prefix,80) || !*prefix || !bounded_text(phase,160)) {
        error="snapshot: invalid_argument";return false;
    }
    for(const char* p=prefix;*p;++p) {
        if(!((*p>='a' && *p<='z') || (*p>='A' && *p<='Z') || (*p>='0' && *p<='9') || *p=='-' || *p=='_')) {
            error="snapshot: invalid prefix";return false;
        }
    }
    const auto readable=[](std::uint32_t address,std::uint32_t bytes) {
        if(!valid_range(address,bytes))return false;
        while(bytes) {
            MemInfo info{};PageInfo page{};
            if(R_FAILED(svcQueryMemory(&info,&page,address)) || !(info.perm&MEMPERM_READ) ||
               !info.size || address<info.base_addr || address-info.base_addr>=info.size)return false;
            const auto available=info.size-(address-info.base_addr);
            const auto advance=std::min(bytes,available);
            address+=advance;bytes-=advance;
        }
        return true;
    };
    std::array<NativeSnapshotSpan,max_spans> spans{};
    const auto rw_start=reinterpret_cast<std::uintptr_t>(sm64ds_diagnostic_rw_start);
    const auto rw_end=reinterpret_cast<std::uintptr_t>(sm64ds_diagnostic_rw_end);
    if(rw_end<=rw_start) {error="snapshot: invalid ELF RW bounds";return false;}
    spans[0]={"elf-rw",sm64ds_diagnostic_rw_start,rw_start,rw_end-rw_start};
    spans[1]={"app-heap",__ctru_heap,reinterpret_cast<std::uintptr_t>(__ctru_heap),envGetHeapSize()};
    std::size_t span_count=2;
    if(__ctru_linear_heap && envGetLinearHeapSize()) {
        spans[2]={"linear-heap",__ctru_linear_heap,reinterpret_cast<std::uintptr_t>(__ctru_linear_heap),envGetLinearHeapSize()};
        span_count=3;
    }
    for(std::size_t i=0;i<span_count;++i)if(!readable(spans[i].address,spans[i].size)) {
        error="snapshot: unreadable trusted span";return false;
    }
    NativeSnapshotMetadata metadata{tick,presentation,phase,0,0,false,span_count==3};
    std::array<NativeSnapshotView,max_views> views{};std::size_t count=0;
    // Query only this thread's actual local-variable address. This is a stack
    // residency probe, not a recovered fault PC or a walk through other stacks.
    unsigned char stack_probe=0;
    metadata.stack_probe_address=reinterpret_cast<std::uintptr_t>(&stack_probe);
    MemInfo stack_info{};PageInfo stack_page{};
    if(R_SUCCEEDED(svcQueryMemory(&stack_info,&stack_page,metadata.stack_probe_address)) &&
       (stack_info.perm&MEMPERM_READ)) {
        const auto coverage=stack_coverage(spans.data(),span_count,stack_info.base_addr,
            stack_info.size,metadata.stack_probe_address);
        bool covered=coverage==StackCoverage::existing;
        if(coverage==StackCoverage::additional && readable(stack_info.base_addr,stack_info.size)) {
            spans[span_count++]={"mapped-stack-residency-region",reinterpret_cast<const void*>(stack_info.base_addr),
                stack_info.base_addr,stack_info.size};covered=true;
        }
        if(covered) {
            views[count++]={"mapped-stack-residency-region",stack_info.base_addr,stack_info.size,no_module,0,0};
            metadata.stack_available=true;
        }
    }
    // The address comes from libctru's real TPIDRURW accessor. Capture only
    // this thread's TLS block, not neighboring threads in the same OS page.
    // The exact bounded span uses the same overlap/coverage policy as stack
    // residency and must pass OS read checks before any bytes are accessed.
    constexpr std::uint32_t tls_bytes=0x200;
    metadata.thread_tls_address=reinterpret_cast<std::uintptr_t>(getThreadLocalStorage());
    const auto tls_coverage=stack_coverage(spans.data(),span_count,
        metadata.thread_tls_address,tls_bytes,metadata.thread_tls_address);
    if(tls_coverage!=StackCoverage::unavailable && readable(metadata.thread_tls_address,tls_bytes)) {
        if(tls_coverage==StackCoverage::additional) {
            spans[span_count++]={"calling-thread-tls",reinterpret_cast<const void*>(metadata.thread_tls_address),
                metadata.thread_tls_address,tls_bytes};
        }
        views[count++]={"calling-thread-tls",metadata.thread_tls_address,tls_bytes,no_module,0,0};
        metadata.thread_tls_available=true;
    }
    // No arbitrary pointer chain: one app-owned root object, validated before
    // reading its real ARM32 constructor fields at +4/+8.
    const auto root=reinterpret_cast<std::uintptr_t>(native_game_root_heap());
    metadata.root_header=root;
    if(root && owner(spans.data(),span_count,root,0x18)<span_count) {
        std::uint32_t start=0,bytes=0;
        std::memcpy(&start,reinterpret_cast<const void*>(root+4),4);
        std::memcpy(&bytes,reinterpret_cast<const void*>(root+8),4);
        if(valid_range(start,bytes) && owner(spans.data(),span_count,start,bytes)<span_count) {
            views[count++]={"original-root-header",root,0x18,no_module,0,0};
            views[count++]={"original-root-heap",start,bytes,no_module,0,0};metadata.root_available=true;
        }
    }
    const auto table=reinterpret_cast<std::uintptr_t>(sm64ds_runtime_sections);
    const auto table_end=reinterpret_cast<std::uintptr_t>(sm64ds_runtime_sections_end);
    if(table_end<=table || (table_end-table)%20 || (table_end-table)/20>256 ||
       !readable(table,table_end-table)) {error="snapshot: invalid runtime section table";return false;}
    metadata.runtime_sections=(table_end-table)/20;
    for(std::uint32_t i=0;i<metadata.runtime_sections;++i) {
        SnapshotRuntimeSection section{};
        std::memcpy(&section,reinterpret_cast<const void*>(table+i*20),20);
        views[count++]={"original-section",reinterpret_cast<std::uintptr_t>(section.destination),
            section.size,section.module,section.address,section.zero?1u:0u};
    }
    // These four canonical ranges always resolve directly to native arrays;
    // no bank alias probing or derived GX status read is involved.
    for(const auto entry:{std::array<std::uint32_t,2>{0x04000000,0x2000},
        {0x05000000,0x800},{0x06800000,0xa4000},{0x07000000,0x800}}) {
        const auto* data=native_video_pointer(entry[0],entry[1]);
        views[count++]={"native-video",reinterpret_cast<std::uintptr_t>(data),entry[1],no_module,entry[0],0};
    }
    char binary[160],index[160];
    std::snprintf(binary,sizeof binary,"sdmc:/3ds/sm64ds/native/%s.ram.bin",prefix);
    std::snprintf(index,sizeof index,"sdmc:/3ds/sm64ds/native/%s.ram.json",prefix);
    const auto result=native_snapshot_write_ranges(binary,index,spans.data(),span_count,views.data(),count,metadata);
    if(!result.okay()) {error="snapshot: ";error+=native_snapshot_status_name(result.status);return false;}
    error.clear();return true;
#else
    (void)prefix;(void)tick;(void)presentation;(void)phase;
    error="snapshot: unavailable outside 3DS";return false;
#endif
}
}
