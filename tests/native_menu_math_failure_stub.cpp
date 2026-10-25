// CPU-test boundary only. The test hook stops at this symbol and records the
// supplied diagnostic. Production supplies its existing fatal adapter.
extern "C" [[noreturn]] void sm64ds_native_missing(const char*) {
    __builtin_trap();
}
