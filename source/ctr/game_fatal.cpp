#include <cstdio>
#include <cstdlib>

// The original fatal screen touches DS display/IRQ registers. A native fatal
// error must terminate instead of returning success to a damaged game state.
extern "C" [[noreturn]] void Crash() {
    const char* message="FATAL: el nucleo original ha detenido la ejecucion.\n";
    std::fputs(message,stderr);
    if(FILE* log=std::fopen("sdmc:/3ds/sm64ds/platform.log","a")) {
        std::fputs(message,log);std::fclose(log);
    }
    std::abort();
}
