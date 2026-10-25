// Host memory/contract check, using the real native_video register store and
// physical VRAM banks. Synthetic source patterns are test fixtures only;
// no rendered game image or GPU result is claimed by this executable.
#include "sm64ds/native_capture.hpp"
#include "sm64ds/native_video.hpp"
#include "sm64ds/native_platform.hpp"
#include "sm64ds/game_draw.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>

namespace {
unsigned platform_failures;
const char* last_failure;
constexpr unsigned bank_bytes = 0x20000, vram_bytes = 0xa4000;
constexpr std::uint32_t capture_register = 0x04000064, bank_register = 0x04000240;
unsigned capture_cases, bank_rejections, explicit_failures;
}
namespace sm64ds {
void native_platform_fail(const char* message) { ++platform_failures; last_failure=message; }
void native_platform_irq_set_master(bool) { std::abort(); }
bool native_video_gx_status(std::uint32_t&) { std::abort(); }
}
extern "C" {
void sm64ds_draw_fifo(std::uint32_t) { std::abort(); }
void sm64ds_draw_port(std::uint32_t,std::uint32_t) { std::abort(); }
bool sm64ds_draw_ok() { std::abort(); }
}
namespace {
using namespace sm64ds;
std::uint8_t* physical() {
    return static_cast<std::uint8_t*>(native_video_pointer(0x06800000,vram_bytes));
}
void write(std::uint32_t address,std::uint32_t value,unsigned bytes=4) {
    assert(native_video_write(address,value,bytes));
}
std::uint32_t read(std::uint32_t address,unsigned bytes=4) {
    return std::uint32_t(native_video_read(address,bytes));
}
void reset() {
    native_video_reset();
    platform_failures=0;last_failure=nullptr;
    std::memset(physical(),0xa5,vram_bytes);
}
std::vector<std::uint8_t> image(unsigned stride,bool alternate) {
    std::vector<std::uint8_t> result(stride*192,0xbb);
    const std::uint8_t alpha[]={0,1,127,128,254,255};
    for(unsigned y=0;y<192;++y)for(unsigned x=0;x<256;++x) {
        auto* pixel=result.data()+y*stride+x*4;
        pixel[0]=std::uint8_t(alternate?255-x:x);
        pixel[1]=std::uint8_t(alternate?255-y:y);
        pixel[2]=std::uint8_t(alternate?x+y:x^y);
        pixel[3]=alpha[(x+y)%6];
    }
    // Fixed colors distinguish channel packing and nonzero alpha from an
    // 8-bit alpha>=128 threshold, even before the full bitmap reference.
    const std::uint8_t known[]={255,0,0,255, 0,255,0,1, 0,0,255,127,
                               255,255,255,0, 248,8,16,128};
    if(!alternate)std::memcpy(result.data(),known,sizeof(known));
    return result;
}
NativeCaptureImage available(const std::vector<std::uint8_t>& pixels,
                             unsigned stride,std::uint64_t frame=42) {
    return {pixels.data(),pixels.size(),stride,frame};
}
std::vector<std::uint8_t> reference(const std::vector<std::uint8_t>& rgba,
                                   unsigned stride,unsigned width,unsigned height) {
    std::vector<std::uint8_t> result(width*height*2);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
        const auto* pixel=rgba.data()+y*stride+x*4;
        const unsigned packed=(pixel[0]/8)+(pixel[1]/8)*32+(pixel[2]/8)*1024+
                              (pixel[3]==0?0:32768);
        const unsigned position=(y*width+x)*2;
        result[position]=std::uint8_t(packed%256);
        result[position+1]=std::uint8_t(packed/256);
    }
    return result;
}
void check_failure(NativeCaptureRequest request,const NativeCaptureSources& sources,
                   NativeCaptureError expected) {
    const std::vector<std::uint8_t> before(physical(),physical()+vram_bytes);
    const auto failures=platform_failures;
    const auto result=native_capture_complete_frame(request,sources);
    assert(!result.okay() && result.status==NativeCaptureStatus::failed && result.error==expected);
    assert(result.bytes_written==0 && result.rendered_frame==0);
    assert(platform_failures==failures+1 && last_failure && *last_failure);
    assert(!(read(capture_register)&0x80000000u));
    assert(std::memcmp(physical(),before.data(),vram_bytes)==0 && native_video_ok());
    ++explicit_failures;
}
void arm(std::uint32_t control,std::uint8_t bank_control=0x80) {
    write(bank_register+((control>>16)&3),bank_control,1);
    write(capture_register,control);
}
}  // namespace

int main() {
    using namespace sm64ds;
    const auto composite=image(1024,false),three_d=image(1088,true);
    const NativeCaptureSources sources={available(composite,1024),available(three_d,1088)};
    const unsigned widths[]={128,256,256,256},heights[]={128,64,128,192};
    for(unsigned bank=0;bank<4;++bank)for(unsigned size=0;size<4;++size)
        for(unsigned offset=0;offset<4;++offset)for(unsigned source=0;source<2;++source) {
            reset();
            const std::uint32_t control=0x80000000u|(bank<<16)|(offset<<18)|(size<<20)|
                                        (source<<24)|0x1010u;
            arm(control);
            const auto request=native_capture_begin_frame(42);
            assert(request.enabled() && request.destination_lcdc);
            assert(request.bank()==bank && request.width()==widths[size] && request.height()==heights[size]);
            assert(request.needs_composited()==(source==0) && request.needs_3d_only()==(source==1));
            assert(read(capture_register)==control); // start does not complete early
            const auto result=native_capture_complete_frame(request,sources);
            assert(result.okay() && result.status==NativeCaptureStatus::captured);
            assert(result.bytes_written==widths[size]*heights[size]*2 && result.rendered_frame==42);
            assert(result.source==request.source() && result.bank==bank && result.write_offset==offset*0x8000);
            assert(result.width==widths[size] && result.height==heights[size]);
            assert(read(capture_register)==(control&0x7fffffffu) && !platform_failures && native_video_ok());
            const auto converted=reference(source?three_d:composite,source?1088:1024,widths[size],heights[size]);
            std::vector<std::uint8_t> expected(vram_bytes,0xa5);
            // Independent two-span wrap model rather than per-pixel masking.
            const unsigned start=offset*0x8000;
            const auto first=std::min<std::size_t>(converted.size(),bank_bytes-start);
            std::memcpy(expected.data()+bank*bank_bytes+start,converted.data(),first);
            if(converted.size()>first)
                std::memcpy(expected.data()+bank*bank_bytes,converted.data()+first,converted.size()-first);
            assert(std::memcmp(physical(),expected.data(),vram_bytes)==0);
            if(source==0) {
                const auto* packed=physical()+bank*bank_bytes+start;
                assert(packed[0]==0x1f && packed[1]==0x80); // opaque red
                assert(packed[2]==0xe0 && packed[3]==0x83); // green, alpha=1
                assert(packed[4]==0x00 && packed[5]==0xfc); // blue, alpha=127
                assert(packed[6]==0xff && packed[7]==0x7f); // transparent white
                assert(packed[8]==0x3f && packed[9]==0x88); // independent mixed-color sample
            }
            // Reallocation exposes exactly the captured physical bytes.
            const auto* own=physical()+bank*bank_bytes;
            if(bank==2) {
                write(bank_register+bank,0x84,1);
                assert(native_video_pointer(0x06200000,bank_bytes)==own);
            } else if(bank==3) {
                write(bank_register+bank,0x84,1);
                assert(native_video_pointer(0x06600000,bank_bytes)==own);
            } else {
                write(bank_register+bank,0x81|(bank<<3),1);
                assert(native_video_pointer(0x06000000+bank*bank_bytes,bank_bytes)==own);
            }
            assert(std::memcmp(physical(),expected.data(),vram_bytes)==0);
            ++capture_cases;
        }

    for(unsigned bank=0;bank<4;++bank)for(std::uint8_t control:{0x00,0x03,0x81,0x82,0x83}) {
        reset();arm(0x80300000u|(bank<<16),control);
        const std::vector<std::uint8_t> before(physical(),physical()+vram_bytes);
        const auto request=native_capture_begin_frame(0);
        assert(!request.destination_lcdc && !request.needs_composited() && !request.needs_3d_only());
        const auto result=native_capture_complete_frame(request,{});
        assert(result.okay() && result.status==NativeCaptureStatus::no_lcdc_destination);
        assert(result.bytes_written==0 && result.rendered_frame==0 && !platform_failures);
        assert(read(capture_register)==(request.control&0x7fffffffu));
        assert(std::memcmp(physical(),before.data(),vram_bytes)==0);
        ++bank_rejections;
    }
    // Original Scene reset: disabled A + enable-only does not require a
    // fabricated black GPU frame and cannot corrupt owned texture memory.
    reset();write(capture_register,0x80000000u);
    auto request=native_capture_begin_frame(0);
    auto result=native_capture_complete_frame(request,{});
    assert(result.status==NativeCaptureStatus::no_lcdc_destination && read(capture_register)==0);
    assert(!platform_failures && result.bytes_written==0);
    for(unsigned i=0;i<vram_bytes;++i)assert(physical()[i]==0xa5);
    ++bank_rejections;
    for(unsigned bank=0;bank<4;++bank) {
        reset();arm(0x80000000u|(bank<<16),0x84);
        request=native_capture_begin_frame(42);
        result=native_capture_complete_frame(request,sources);
        if(bank<2)assert(result.status==NativeCaptureStatus::captured);
        else assert(result.status==NativeCaptureStatus::no_lcdc_destination);
        assert(!platform_failures);
    }
    reset();arm(0x80000000u,0xf8); // ignored OFS/unused bits in LCDC mode
    request=native_capture_begin_frame(42);
    result=native_capture_complete_frame(request,sources);
    assert(result.status==NativeCaptureStatus::captured && !platform_failures);

    // Supported Source A must have its OWN actual frame; another image is
    // never substituted just because a pointer or zeroed texture exists.
    for(unsigned source=0;source<2;++source) {
        reset();arm(0x80000000u|(source<<24));
        request=native_capture_begin_frame(42);
        NativeCaptureSources absent=sources;
        (source?absent.three_d_only:absent.composited)={};
        check_failure(request,absent,NativeCaptureError::unavailable_image);
        reset();arm(0x80000000u|(source<<24));request=native_capture_begin_frame(42);
        NativeCaptureSources allocated=sources;
        (source?allocated.three_d_only:allocated.composited).rendered_frame=0;
        check_failure(request,allocated,NativeCaptureError::unavailable_image);
    }
    reset();arm(0x80000000u);request=native_capture_begin_frame(41);
    check_failure(request,sources,NativeCaptureError::frame_mismatch);
    reset();arm(0x80000000u);request=native_capture_begin_frame(0);
    check_failure(request,sources,NativeCaptureError::unavailable_image);
    for(unsigned mode=1;mode<4;++mode)for(unsigned b=0;b<2;++b) {
        reset();arm(0x80000000u|(mode<<29)|(b<<25));request=native_capture_begin_frame(42);
        assert(!request.needs_composited() && !request.needs_3d_only());
        check_failure(request,sources,NativeCaptureError::unsupported_source);
    }
    for(unsigned variant=0;variant<5;++variant) {
        reset();arm(0x80000000u);request=native_capture_begin_frame(42);
        auto invalid=sources;
        switch(variant) {
        case 0:invalid.composited.row_stride_bytes=1023;break;
        case 1:invalid.composited.row_stride_bytes=std::numeric_limits<std::size_t>::max();break;
        case 2:invalid.composited.bytes=1024*192-1;break;
        case 3:invalid.composited.rgba8=nullptr;break;
        case 4:invalid.composited.rgba8=physical();break;
        }
        check_failure(request,invalid,variant==3?NativeCaptureError::unavailable_image:NativeCaptureError::invalid_image);
    }
    reset();arm(0x80000000u);request=native_capture_begin_frame(42);
    write(bank_register,0x83,1);
    check_failure(request,sources,NativeCaptureError::configuration_changed);
    reset();arm(0x80000000u);request=native_capture_begin_frame(42);
    write(capture_register,0x80100000u);
    check_failure(request,sources,NativeCaptureError::configuration_changed);
    reset();arm(0x80000000u);request=native_capture_begin_frame(42);
    assert(native_capture_complete_frame(request,sources).status==NativeCaptureStatus::captured);
    check_failure(request,sources,NativeCaptureError::configuration_changed);

    // A disabled capture at line0 stays idle even if a new request is armed
    // later: the new request belongs to the NEXT begin-frame boundary.
    reset();write(capture_register,0x00360010u);
    request=native_capture_begin_frame(42);
    assert(!request.enabled());
    arm(0x80360010u);
    result=native_capture_complete_frame(request,{});
    assert(result.status==NativeCaptureStatus::idle && read(capture_register)==0x80360010u);
    request=native_capture_begin_frame(42);
    assert(native_capture_complete_frame(request,sources).status==NativeCaptureStatus::captured);
    assert(!platform_failures && read(capture_register)==0x00360010u);

    std::printf("{\"status\":\"passed\",\"scope\":\"Host capture contract with real owned VRAM; synthetic unit-test pixels only; no GPU, game or console proof\",\"all_size_bank_offset_source_combinations\":%u,\"no_lcdc_destination_cases\":%u,\"explicit_failure_cases\":%u,\"wrap_crop_alpha_and_aliases\":\"passed\",\"enable_timing\":\"passed\"}\n",
                capture_cases,bank_rejections,explicit_failures);
}
