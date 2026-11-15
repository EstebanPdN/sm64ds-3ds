#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
namespace sm64ds::render {
// Bounded CPU history, including refused submissions. No file I/O, allocation
// or device queries during recording. Tick frequency belongs in dump metadata.
// The phase columns after `endpoint` are wall scopes measured on the render
// worker (bridge/draw/upload/submit) or the game thread (producer/skin); they
// nest inside update/end-start and must not be summed with them.
struct FrameTiming {
    std::uint64_t frame=0,scene=0,start=0,end=0,update=0,source_2d=0,bank_load=0,gpu_wait=0;
    std::uint32_t previous_gpu_us=0,draws=0,triangles=0,quads=0,loads=0,evictions=0,error=0;
    bool endpoint=false;
    std::uint64_t bridge=0,draw=0,upload=0,submit=0,producer=0,skin=0;
    std::uint32_t matrices=0,light_rows=0,upload_bytes=0,material_binds=0,pipeline_changes=0,shader_binds=0;
    // BUILD121: game-thread time that ended during this job (the worker shares
    // core 0 at a lower priority on Old 3DS: end-start minus this estimates
    // the render's own CPU), SceneRenderer::draw stages and the 2D route
    // (1 native menu 2D, 2 engine A CPU 2D, 4 A prepared, 8 B prepared,
    // 16 GPU map, 32 title sky).
    std::uint32_t game_overlap=0;
    std::array<std::uint32_t,9> stages{};
    std::uint32_t route_2d=0;
};
class FrameTimeline {
public:
    static constexpr unsigned capacity=1024;
    void push(FrameTiming row) noexcept {rows_[written_++%capacity]=row;}
    unsigned size() const noexcept {return unsigned(written_<capacity?written_:capacity);}
    const FrameTiming& at(unsigned i) const noexcept {return rows_[(written_-size()+i)%capacity];}
    static bool write_header(FILE* file) noexcept {
        return file&&std::fputs("frame,scene,start_tick,end_tick,update_ticks,source_2d_ticks,bank_load_ticks,gpu_wait_ticks,previous_gpu_us,draws,triangles,quads,total_bank_loads,total_evictions,error,endpoint,"
            "bridge_ticks,draw_ticks,upload_ticks,submit_ticks,producer_ticks,skin_ticks,matrices,light_rows,upload_bytes,material_binds,pipeline_changes,shader_binds,"
            "game_overlap_ticks,stage_validate,stage_wait,stage_resources,stage_dynamic,stage_preflight,stage_plan,stage_geometry,stage_compose,stage_finish,route_2d\n",file)>=0;
    }
    static bool write_row(FILE* file,const FrameTiming& r) noexcept {
        return std::fprintf(file,"%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%u,%u,%u,%u,%u,%u,%u,%u,%llu,%llu,%llu,%llu,%llu,%llu,%u,%u,%u,%u,%u,%u,"
            "%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu\n",
            (unsigned long long)r.frame,(unsigned long long)r.scene,(unsigned long long)r.start,(unsigned long long)r.end,
            (unsigned long long)r.update,(unsigned long long)r.source_2d,(unsigned long long)r.bank_load,(unsigned long long)r.gpu_wait,
            r.previous_gpu_us,r.draws,r.triangles,r.quads,r.loads,r.evictions,r.error,unsigned(r.endpoint),
            (unsigned long long)r.bridge,(unsigned long long)r.draw,(unsigned long long)r.upload,(unsigned long long)r.submit,
            (unsigned long long)r.producer,(unsigned long long)r.skin,r.matrices,r.light_rows,r.upload_bytes,r.material_binds,
            r.pipeline_changes,r.shader_binds,(unsigned long)r.game_overlap,
            (unsigned long)r.stages[0],(unsigned long)r.stages[1],(unsigned long)r.stages[2],(unsigned long)r.stages[3],(unsigned long)r.stages[4],
            (unsigned long)r.stages[5],(unsigned long)r.stages[6],(unsigned long)r.stages[7],(unsigned long)r.stages[8],(unsigned long)r.route_2d)>=0;
    }
    bool write_csv(FILE* file) const noexcept {
        if(!write_header(file))return false;
        for(unsigned i=0;i<size();++i)if(!write_row(file,at(i)))return false;
        return !std::ferror(file);
    }
private:
    std::array<FrameTiming,capacity> rows_{};
    std::uint64_t written_=0;
};
}
