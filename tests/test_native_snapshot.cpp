// Same translation unit exposes the private pure range policy to fixtures
// without adding a production API. The tool compiles production ARM separately.
#include "../source/game_port/native_snapshot.cpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#if defined(__unix__) || defined(__APPLE__)
#include <csignal>
#include <sys/resource.h>
#endif
using namespace sm64ds;
namespace {
std::vector<unsigned char> read(const std::string& path) {
    std::ifstream file(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
std::uint32_t le32(const std::vector<unsigned char>& data,std::size_t offset) {
    assert(offset+4<=data.size());
    return data[offset]|std::uint32_t(data[offset+1])<<8|
        std::uint32_t(data[offset+2])<<16|std::uint32_t(data[offset+3])<<24;
}
}
int main(int argc,char** argv) {
    assert(argc==2);const std::string dir=argv[1];
    const NativeSnapshotSpan large_mapping{"app-heap",reinterpret_cast<const void*>(1),0x08000000,60465152};
    assert(stack_coverage(&large_mapping,1,0x08000000,60465152,0x0807e003)==StackCoverage::existing);
    assert(stack_coverage(&large_mapping,1,0x08001000,3*1024*1024,0x0807e003)==StackCoverage::existing);
    assert(stack_coverage(&large_mapping,1,0x10000000,2*1024*1024,0x10000100)==StackCoverage::additional);
    assert(stack_coverage(&large_mapping,1,0x10000000,2*1024*1024+1,0x10000100)==StackCoverage::unavailable);
    assert(stack_coverage(&large_mapping,1,0x07fff000,8192,0x08000000)==StackCoverage::unavailable);
    assert(stack_coverage(&large_mapping,1,0x10000000,4096,0x10001000)==StackCoverage::unavailable);
    assert(stack_coverage(&large_mapping,1,UINT32_MAX-1,3,UINT32_MAX-1)==StackCoverage::unavailable);
    assert(stack_coverage(&large_mapping,1,0,4096,1)==StackCoverage::unavailable);
    const std::array<std::uint32_t,5> sizes={4096,131123,4096,4096,512};
    const std::array<std::uint32_t,5> addresses={0x01000000,0x08000000,0x14000000,0x0ff00000,0x1ff82000};
    std::array<std::vector<unsigned char>,5> storage,before;
    std::array<NativeSnapshotSpan,5> spans{};
    for(std::size_t i=0;i<spans.size();++i) {
        storage[i].resize(sizes[i]+2,0xa5);
        for(std::uint32_t j=0;j<sizes[i];++j)storage[i][j+1]=static_cast<unsigned char>((j*37+i*17)&255);
        spans[i]={"fixture",storage[i].data()+1,addresses[i],sizes[i]};
    }
    before=storage;
    assert(stack_coverage(spans.data(),4,addresses[4],sizes[4],addresses[4])==StackCoverage::additional);
    assert(stack_coverage(spans.data(),5,addresses[4],sizes[4],addresses[4])==StackCoverage::existing);
    assert(stack_coverage(spans.data(),5,0x1ff82400,512,0x1ff82400)==StackCoverage::unavailable);
    std::vector<NativeSnapshotView> views;
    for(unsigned i=0;i<226;++i)views.push_back({"original-section",addresses[0]+i*16,16,
        i?i%104:UINT32_MAX,0x02000000+i*16,i%2});
    views.push_back({"original-root-header",addresses[1],24,UINT32_MAX,0,0});
    views.push_back({"original-root-heap",addresses[1]+24,65537,UINT32_MAX,0,0});
    views.push_back({"mapped-stack-residency-region",addresses[3],sizes[3],UINT32_MAX,0,0});
    views.push_back({"calling-thread-tls",addresses[4],sizes[4],UINT32_MAX,0,0});
    NativeSnapshotMetadata metadata;
    metadata.tick=180;metadata.presentation=360;metadata.phase="title \"state\"\n11";
    metadata.runtime_sections=226;metadata.root_header=addresses[1];metadata.root_available=true;
    metadata.linear_available=true;metadata.stack_available=true;metadata.stack_probe_address=addresses[3]+96;
    metadata.thread_tls_address=addresses[4];metadata.thread_tls_available=true;
    const auto binary=dir+"/fixture.ram.bin",index=dir+"/fixture.ram.json";
    auto result=native_snapshot_write_ranges(binary.c_str(),index.c_str(),spans.data(),spans.size(),views.data(),views.size(),metadata);
    assert(result.okay());assert(storage==before);
    const auto data=read(binary);assert(data.size()==result.binary_bytes);
    assert(data.size()>=32 && !std::memcmp(data.data(),"SM64RAM1",8));
    assert(le32(data,8)==1 && le32(data,16)==spans.size() && le32(data,20)==views.size());
    assert(le32(data,24)==data.size() && le32(data,28)==0);
    const auto payload=32+spans.size()*16+views.size()*28;assert(le32(data,12)==payload);
    std::size_t cursor=payload;
    for(std::size_t i=0;i<spans.size();++i) {
        const auto row=32+i*16;
        assert(le32(data,row)==spans[i].address && le32(data,row+4)==spans[i].size);
        assert(le32(data,row+8)==cursor && le32(data,row+12)==i);
        assert(!std::memcmp(data.data()+cursor,storage[i].data()+1,sizes[i]));cursor+=sizes[i];
    }
    for(std::size_t i=0;i<views.size();++i) {
        const auto row=32+spans.size()*16+i*28;
        const auto& view=views[i];const auto parent=le32(data,row+12);
        assert(parent<spans.size() && le32(data,row)==view.address && le32(data,row+4)==view.size);
        assert(le32(data,row+8)==le32(data,32+parent*16+8)+view.address-spans[parent].address);
        assert(le32(data,row+16)==view.module && le32(data,row+20)==view.original_address && le32(data,row+24)==view.flags);
    }
    assert(cursor==data.size());
    const auto rejected=dir+"/rejected.bin",rejected_json=dir+"/rejected.json";
    auto call=[&](const NativeSnapshotSpan* s,std::size_t n,const NativeSnapshotView* v,std::size_t m) {
        return native_snapshot_write_ranges(rejected.c_str(),rejected_json.c_str(),s,n,v,m,metadata);
    };
    assert(call(nullptr,0,nullptr,0).status==NativeSnapshotStatus::invalid_argument);
    assert(call(spans.data(),6,nullptr,0).status==NativeSnapshotStatus::invalid_argument);
    assert(call(spans.data(),4,nullptr,1).status==NativeSnapshotStatus::invalid_argument);
    assert(call(spans.data(),4,views.data(),273).status==NativeSnapshotStatus::invalid_argument);
    auto altered=spans;
    altered[1].address=UINT32_MAX-1;altered[1].size=3;
    assert(call(altered.data(),4,nullptr,0).status==NativeSnapshotStatus::invalid_range);
    altered=spans;altered[1].address=spans[0].address+16;
    assert(call(altered.data(),4,nullptr,0).status==NativeSnapshotStatus::invalid_range);
    altered=spans;altered[1].data=nullptr;
    assert(call(altered.data(),4,nullptr,0).status==NativeSnapshotStatus::invalid_argument);
    NativeSnapshotView outside{"outside",addresses[0]+sizes[0]-1,2,0,0,0};
    assert(call(spans.data(),4,&outside,1).status==NativeSnapshotStatus::uncovered_view);
    outside={"null",0,1,0,0,0};
    assert(call(spans.data(),4,&outside,1).status==NativeSnapshotStatus::invalid_range);
    NativeSnapshotSpan huge{"huge",storage[0].data(),1,UINT32_MAX-1};
    assert(call(&huge,1,nullptr,0).status==NativeSnapshotStatus::invalid_range);
    assert(read(rejected).empty() && read(rejected_json).empty());
    assert(native_snapshot_write_ranges(binary.c_str(),binary.c_str(),spans.data(),4,nullptr,0,metadata).status==NativeSnapshotStatus::invalid_argument);
    const auto missing=dir+"/no-such-directory/file";
    assert(native_snapshot_write_ranges(missing.c_str(),rejected_json.c_str(),spans.data(),4,nullptr,0,metadata).status==NativeSnapshotStatus::open_failed);
    const auto partial=dir+"/binary-without-index.bin";
    result=native_snapshot_write_ranges(partial.c_str(),missing.c_str(),spans.data(),4,nullptr,0,metadata);
    assert(result.status==NativeSnapshotStatus::open_failed && read(partial).size()==result.binary_bytes);
    unsigned io_write_failure=0;
#if defined(__unix__) || defined(__APPLE__)
    const auto short_binary=dir+"/short-write.bin",short_index=dir+"/short-write.json";
    result=native_snapshot_write_ranges(short_binary.c_str(),short_index.c_str(),spans.data(),1,nullptr,0,metadata);
    assert(result.okay());
    const auto prior_index=read(short_index);
    assert(std::string(prior_index.begin(),prior_index.end()).find("\"io_status\":\"complete\"")!=std::string::npos);
    struct rlimit previous{};assert(getrlimit(RLIMIT_FSIZE,&previous)==0);
    auto limit=previous;limit.rlim_cur=128;
    const auto previous_handler=std::signal(SIGXFSZ,SIG_IGN);
    assert(setrlimit(RLIMIT_FSIZE,&limit)==0);
    result=native_snapshot_write_ranges(short_binary.c_str(),short_index.c_str(),spans.data(),1,nullptr,0,metadata);
    assert(result.status==NativeSnapshotStatus::write_failed && result.binary_bytes==128 && read(short_index).empty());
    auto tiny_span=spans[0];tiny_span.size=4;
    const auto tiny_binary=dir+"/index-short-write.bin",tiny_index=dir+"/index-short-write.json";
    result=native_snapshot_write_ranges(tiny_binary.c_str(),tiny_index.c_str(),&tiny_span,1,nullptr,0,metadata);
    assert(!result.okay() && result.binary_bytes==52 && read(tiny_index).empty());
    assert(setrlimit(RLIMIT_FSIZE,&previous)==0);std::signal(SIGXFSZ,previous_handler);
    io_write_failure=2;
#endif
    assert(storage==before);
    std::string error;
    assert(!native_snapshot_write("fixture",0,0,"host",error) && error=="snapshot: unavailable outside 3DS");
    std::printf("{\"status\":\"passed\",\"spans\":5,\"views\":%u,\"original_sections\":226,"
        "\"read_only_buffers_and_canaries\":true,\"negative_range_cases\":10,"
        "\"negative_open_cases\":2,\"stack_residency_policy_cases\":8,\"TLS_range_policy_cases\":3,\"forced_short_write_cases\":%u,\"stale_index_invalidated\":true,\"binary_bytes\":%u}\n",
        unsigned(views.size()),io_write_failure,unsigned(data.size()));
}
