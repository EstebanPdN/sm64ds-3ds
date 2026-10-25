// ABI fixture only: exercise the caller storing LenVec3's result into a word
// of the same vector after all original input components have been consumed.
#include <cstdint>

extern "C" int LenVec3(const std::int32_t* vector);
extern "C" int native_test_lenvec3_store_first(std::int32_t* vector) {
    const int result=LenVec3(vector);
    vector[0]=result;
    return result;
}
