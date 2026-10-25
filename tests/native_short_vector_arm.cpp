// ABI fixture: call the real short-vector scalar leaf, then overwrite a source
// component only after it has returned. This also checks result forwarding.
#include <cstdint>

extern "C" int func_02053644(const std::int16_t* vector);
extern "C" int native_test_short_length_store_first(std::int16_t* vector) {
    const int result=func_02053644(vector);
    const std::uint16_t bits=static_cast<std::uint16_t>(result);
    __builtin_memcpy(vector,&bits,sizeof(bits));
    return result;
}
