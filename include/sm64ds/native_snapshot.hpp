#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace sm64ds {
// Physical native ranges are written once. Logical views only index bytes in
// those ranges; they never reconstruct, relocate or manufacture game memory.
struct NativeSnapshotSpan {
    const char* name;
    const void* data;
    std::uint32_t address,size;
};
struct NativeSnapshotView {
    const char* name;
    std::uint32_t address,size,module,original_address,flags;
};
struct NativeSnapshotMetadata {
    std::uint64_t tick=0,presentation=0;
    const char* phase="unspecified";
    std::uint32_t runtime_sections=0,root_header=0;
    bool root_available=false,linear_available=false;
    std::uint32_t stack_probe_address=0;
    bool stack_available=false;
    std::uint32_t thread_tls_address=0;
    bool thread_tls_available=false;
};
enum class NativeSnapshotStatus {
    complete,invalid_argument,invalid_range,uncovered_view,
    open_failed,write_failed,close_failed,unavailable
};
struct NativeSnapshotResult {
    NativeSnapshotStatus status=NativeSnapshotStatus::invalid_argument;
    std::uint32_t binary_bytes=0;
    bool okay() const {return status==NativeSnapshotStatus::complete;}
};
const char* native_snapshot_status_name(NativeSnapshotStatus status);
// The caller owns readable data pointers. Addresses are their native ARM32
// identities (host fixtures may supply separate byte arrays). All range and
// index checks finish before either file is opened. I/O is streamed in <=64KiB
// writes without a payload-sized allocation. Partial output is explicitly a
// failed result; only a completed JSON index labels the capture complete.
NativeSnapshotResult native_snapshot_write_ranges(
    const char* binary_path,const char* index_path,
    const NativeSnapshotSpan* spans,std::size_t span_count,
    const NativeSnapshotView* views,std::size_t view_count,
    const NativeSnapshotMetadata& metadata);
// 3DS only: use trusted linker/libctru globals and generated runtime tables,
// verify OS read permissions, then capture ELF RW + app heap + available linear
// heap and the readable OS mapping containing a local stack probe. Existing
// spans may cover that mapping at any size; additional spans are capped at
// 2MiB. The mapping is a residency region, not exclusive stack bounds.
// Also index the calling thread's real 512-byte TLS block obtained through
// libctru, adding a fifth span only when existing ranges do not cover it.
// The TLS address is the real TPIDRURW value, not a recovered fault register.
// Prefix is a filename token below sdmc:/3ds/sm64ds/native/.
// Caller must quiesce game updates and synchronize Citro3D before calling.
// Linear bytes reflect CPU-visible cache contents; no GPU readback or blanket
// cache invalidation is performed. This function never changes game state.
bool native_snapshot_write(const char* prefix,std::uint64_t tick,
    std::uint64_t presentation,const char* phase,std::string& error);
}
