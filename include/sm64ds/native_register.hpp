#pragma once
#include "sm64ds/native_video.hpp"
#include <type_traits>

namespace sm64ds {
// A native owned-register reference for mechanically adapted original code.
// Each conversion is one read; assignment is one write; a compound assignment
// performs one read followed by one write at the original access width.
template<class T> class NativeRegister {
    static_assert(std::is_integral<T>::value &&
                  (sizeof(T)==1 || sizeof(T)==2 || sizeof(T)==4));
public:
    explicit constexpr NativeRegister(std::uint32_t address):address_(address) {}
    operator T() const {return T(native_video_read(address_,sizeof(T)));}
    NativeRegister& operator=(T value) {
        native_video_write(address_,value,sizeof(T));return *this;
    }
    NativeRegister& operator=(const NativeRegister& other) {return *this=T(other);}
    NativeRegister& operator&=(T value) {return *this=T(T(*this)&value);}
    NativeRegister& operator|=(T value) {return *this=T(T(*this)|value);}
    NativeRegister& operator^=(T value) {return *this=T(T(*this)^value);}
    NativeRegister& operator+=(T value) {return *this=T(T(*this)+value);}
    NativeRegister& operator-=(T value) {return *this=T(T(*this)-value);}
private:
    std::uint32_t address_;
};
template<class T> NativeRegister<T> native_register(std::uint32_t address) {
    return NativeRegister<T>(address);
}
// Local register-pointer variables in the recovered menu also need ordered
// reads/writes (especially geometry ports). They must not expose backing RAM.
template<class T> class NativeRegisterAddress {
public:
    explicit constexpr NativeRegisterAddress(std::uint32_t address):address_(address) {}
    NativeRegister<T> operator*() const {return native_register<T>(address_);}
private:
    std::uint32_t address_;
};
template<class T> NativeRegisterAddress<T> native_register_address(std::uint32_t address) {
    return NativeRegisterAddress<T>(address);
}
// SDK routines taking a register-block pointer receive owned backing memory,
// never a pointer into the DS address space. The complete span is validated.
template<class T> T* native_register_pointer(std::uint32_t address,std::size_t bytes) {
    return static_cast<T*>(native_video_pointer(address,bytes));
}
}
