#include "Log.hpp"
#include <3ds.h>
#include <CTRPluginFramework.hpp>
#include <cstdarg>
#include <cstdio>

using namespace CTRPluginFramework;

namespace Log {

static Mutex s_mutex;
static File  s_file;
static u32   s_written = 0;
static bool  s_ok = false;
static const u32 kMaxBytes = 64 * 1024;

void Init() {
    Lock l(s_mutex);
    if (File::Open(s_file, "mmo_log.txt", File::RWC | File::TRUNCATE) == 0) s_ok = true;
    s_written = 0;
}

void Write(const char *fmt, ...) {
    if (!s_ok || s_written > kMaxBytes) return;
    char line[256];
    u64 t = osGetTime();
    int n = snprintf(line, sizeof(line), "[%6lu.%03lu] ", (unsigned long)((t / 1000) % 1000000), (unsigned long)(t % 1000));
    va_list ap;
    va_start(ap, fmt);
    n += vsnprintf(line + n, sizeof(line) - n - 2, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof(line) - 2) n = sizeof(line) - 2;
    line[n++] = '\n';
    Lock l(s_mutex);
    s_file.Write(line, n);
    s_file.Flush();
    s_written += n;
}

} // namespace Log
