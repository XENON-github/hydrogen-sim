#ifndef PLATFORM_H
#define PLATFORM_H

/* Tiny portability layer so the GTK-free modules build on Linux, macOS and Windows (MinGW/MSYS2). */

#include <stdio.h>

#ifdef _WIN32
#  define PLAT_POPEN(cmd, mode) _popen((cmd), (mode))
#  define PLAT_PCLOSE(f) _pclose(f)
#  define PLAT_NULL_DEVICE "NUL"
#  define PLAT_POPEN_WRITE "wb"      /* Windows pipes are text-mode unless told otherwise */
#else
#  define PLAT_POPEN(cmd, mode) popen((cmd), (mode))
#  define PLAT_PCLOSE(f) pclose(f)
#  define PLAT_NULL_DEVICE "/dev/null"
#  define PLAT_POPEN_WRITE "w"       /* POSIX popen() rejects a "b" flag */
#endif

/* Number of online CPU cores (>= 1). */
int plat_cpu_count(void);

/* 1 if an `ffmpeg` executable can be started from PATH. Result is cached. */
int plat_have_ffmpeg(void);

#endif /* PLATFORM_H */
