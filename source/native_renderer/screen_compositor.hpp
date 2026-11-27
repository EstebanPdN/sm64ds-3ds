#pragma once
#include "citro3d_backend.hpp"
#include "screen_scene.hpp"

namespace sm64ds::render {
// Render-thread native LCD owner. Source adapters supply native layers; this
// object knows no DS registers, tilemaps, VRAM aliases or graphics programs.
// The backend and C3D_Init must outlive it. Shut down outside an SDK frame,
// before backend.shutdown/C3D_Fini. It never starts/ends a normal draw frame.
class ScreenCompositor {
public:
    ScreenCompositor()=default;
    ScreenCompositor(const ScreenCompositor&)=delete;
    ScreenCompositor& operator=(const ScreenCompositor&)=delete;
    Error init(Citro3DBackend&) noexcept;
    Error draw(const ScreenCanvas& top,const ScreenCanvas& bottom) noexcept;
    bool prepare_right_eye() noexcept;
    void right_eye(bool right) noexcept {right_pass_=right;}
    Error shutdown() noexcept;
    // Completed RGB8 target readback, after backend.end(), caller-owned linear
    // storage. Portrait BGR layout:240 rows,400/320 columns,128-byte alignment.
    Error capture(Screen,void*,std::size_t) noexcept;
    struct Metrics {
        std::uint64_t frames=0,refused=0,draws=0,clear_only_screens=0;
        std::size_t output_vram_bytes=0;
    };
    const Metrics& metrics() const noexcept {return metrics_;}
    C3D_RenderTarget* target(Screen screen) const noexcept {return target_[screen==Screen::bottom];}
    // BUILD135: an overlapped second image presents one screen only
    // (Citro3DBackend::submit_overlapped transfers that target alone), so the
    // other screen is neither validated nor drawn: its target keeps what the
    // pair's first image drew and no LCD ever receives this copy. -1 = both.
    void present_only(int screen) noexcept {only_=screen;}
private:
    Citro3DBackend* owner_=nullptr;
    std::array<C3D_RenderTarget*,2> target_{};
    C3D_RenderTarget* right_=nullptr;
    bool right_pass_=false;
    Mesh quad_{};
    bool completed_=false;
    int only_=-1;
    Metrics metrics_{};
};
}
