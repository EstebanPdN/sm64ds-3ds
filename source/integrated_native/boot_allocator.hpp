#pragma once
// The original game's global operator new routes into its DS heap, which does
// not exist during first-run setup. Boot containers must explicitly use libc
// on both host and ARM, including allocations retained until audio shutdown.
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <new>
#include <string>
#include <type_traits>
#include <vector>

namespace boot_memory {
// Setup and shutdown run on the main thread, before/after the audio worker.
// Track only these explicitly owned libc containers, including vector capacity.
struct Counts { std::size_t live_bytes=0,live_blocks=0,peak_bytes=0; };
inline Counts counts;
inline Counts statistics() noexcept{return counts;}
template<class T> struct Allocator {
    using value_type=T;
    using is_always_equal=std::true_type;
    Allocator() noexcept=default;
    template<class U> Allocator(const Allocator<U>&) noexcept {}
    template<class U> struct rebind { using other=Allocator<U>; };
    T* allocate(std::size_t count) {
        if(count>std::numeric_limits<std::size_t>::max()/sizeof(T))throw std::bad_alloc();
        const std::size_t bytes=count?count*sizeof(T):1;
        void* block=std::malloc(bytes);
        if(!block)throw std::bad_alloc();
        counts.live_bytes+=bytes;++counts.live_blocks;
        if(counts.live_bytes>counts.peak_bytes)counts.peak_bytes=counts.live_bytes;
        return static_cast<T*>(block);
    }
    void deallocate(T* block,std::size_t count) noexcept {
        if(block){counts.live_bytes-=count?count*sizeof(T):1;--counts.live_blocks;}
        std::free(block);
    }
};
template<class T,class U> bool operator==(const Allocator<T>&,const Allocator<U>&) noexcept{return true;}
template<class T,class U> bool operator!=(const Allocator<T>&,const Allocator<U>&) noexcept{return false;}
using String=std::basic_string<char,std::char_traits<char>,Allocator<char>>;
template<class T> using Vector=std::vector<T,Allocator<T>>;
}
