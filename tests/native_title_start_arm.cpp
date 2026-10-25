#include <cstddef>
#include <cstdint>
#include <limits>

// Test-only observer. These declarations are C names of the existing BSS
// aliases, not replacement definitions or independent menu storage.
extern "C" {
extern std::uint8_t* data_ov007_0210342c;
extern std::int32_t data_ov007_02104bec[];
}

struct StateRecord {
    std::int16_t current, pending;
    std::int32_t flags;
    std::int16_t previous, last;
    std::uint32_t ticks;
};
struct TouchRecord {
    std::uint16_t samples[4][4];
    std::int32_t held_ticks, released_ticks, counter, flags;
    std::uint16_t initial_x, initial_y, reserved34, reserved36;
    std::int16_t delta_x, delta_y, frame_delta_x, frame_delta_y;
    std::int32_t last_index;
};

extern "C" const std::uint32_t title_observer_contract[] = {
    sizeof(void*), sizeof(std::int32_t), sizeof(StateRecord),
    offsetof(StateRecord, pending), offsetof(StateRecord, ticks),
    sizeof(TouchRecord), offsetof(TouchRecord, released_ticks),
    0x188, 0x130, 0x10
};

extern "C" int title_span_contains(std::uintptr_t begin, std::uint32_t bytes,
                                    std::uintptr_t address, std::uint32_t length) {
    if (!begin || bytes > std::numeric_limits<std::uintptr_t>::max()-begin ||
        address < begin || length > bytes) return 0;
    return address-begin <= bytes-length;
}

template<class T>
static bool read_span(std::uintptr_t begin, std::uint32_t bytes,
                      std::uintptr_t address, T& result) {
    if (!title_span_contains(begin, bytes, address, sizeof(T))) return false;
    auto* destination = reinterpret_cast<std::uint8_t*>(&result);
    auto* source = reinterpret_cast<const std::uint8_t*>(address);
    for (std::size_t i=0; i<sizeof(T); ++i) destination[i]=source[i];
    return true;
}

// out is trusted, test-owned storage. Every game pointer is validated before
// dereference. Bounds prove readable residency, not that an allocation is live.
// No allocator/vtable calls, allocation, publication or game writes occur.
extern "C" int title_read_gate(std::uintptr_t begin, std::uint32_t bytes,
                                std::uintptr_t context, std::uint32_t* out) {
    if (!out) return 0;
    for (unsigned i=0; i<16; ++i) out[i]=0;
    out[0]=1;
    if (!title_span_contains(begin, bytes, context, 0x188)) return 0;
    std::uintptr_t main=0, sub=0, touch=0, keys=0, element=0, object=0;
    read_span(begin, bytes, context+4, main);
    read_span(begin, bytes, context+8, sub);
    read_span(begin, bytes, context+0x50, touch);
    read_span(begin, bytes, context+0x54, keys);
    read_span(begin, bytes, context+0x130, element);
    StateRecord first{}, second{};
    out[0]=2;
    if (!read_span(begin, bytes, main, first)) return 0;
    out[0]=3;
    if (!read_span(begin, bytes, sub, second)) return 0;
    out[0]=4;
    if (!title_span_contains(begin, bytes, element, 8) ||
        !read_span(begin, bytes, element, object)) return 0;
    out[0]=5;
    if (!title_span_contains(begin, bytes, object, 0x14)) return 0;
    out[0]=6;
    if (!title_span_contains(begin, bytes, touch, 0x28)) return 0;
    out[0]=7;
    if (!title_span_contains(begin, bytes, keys, 4)) return 0;
    std::int32_t opacity=0, released=0;
    std::uint16_t x=0, y=0, down=0, prior=0, held=0, previous=0;
    read_span(begin, bytes, object+0x10, opacity);
    read_span(begin, bytes, touch+8, x);
    read_span(begin, bytes, touch+0xa, y);
    read_span(begin, bytes, touch+0xc, down);
    read_span(begin, bytes, touch+0x14, prior);
    read_span(begin, bytes, touch+0x24, released);
    read_span(begin, bytes, keys, held);
    read_span(begin, bytes, keys+2, previous);
    out[0]=0;
    out[1]=static_cast<std::uint32_t>(first.current);
    out[2]=static_cast<std::uint32_t>(first.pending);
    out[3]=first.ticks;
    out[4]=static_cast<std::uint32_t>(second.current);
    out[5]=static_cast<std::uint32_t>(second.pending);
    out[6]=second.ticks;
    out[7]=static_cast<std::uint32_t>(opacity);
    out[8]=x; out[9]=y; out[10]=down; out[11]=prior;
    out[12]=static_cast<std::uint32_t>(released);
    out[13]=held; out[14]=previous;
    return 1;
}
extern "C" void title_observer_entry() {}
