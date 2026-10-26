#include "platform.h"
#include <stdlib.h>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <unistd.h>
#endif

int plat_cpu_count(void) {
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwNumberOfProcessors < 1 ? 1 : (int)si.dwNumberOfProcessors;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n < 1 ? 1 : (int)n;
#endif
}

int plat_have_ffmpeg(void) {
    static int cached = -1;
    if (cached >= 0) return cached;
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "ffmpeg -hide_banner -version > %s 2>&1", PLAT_NULL_DEVICE);
    cached = (system(cmd) == 0);
    return cached;
}
