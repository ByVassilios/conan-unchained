// Blocking load logger (diagnostic, off unless unchained\loadlog exists)
// Logs every LoadPackage call (= a blocking load) that takes >= 5 ms: wall time, ms, thread, package
// and the calling code as RVAs, to find which loads stall the game thread and whether they repeat.

#include "unchained.h"

typedef void *(*LoadPkg_t)(void *outer, const wchar_t *name, unsigned flags, void *ar);
static LoadPkg_t original_load_package; static DWORD load_log_thread; static double qpc_ticks_to_ms;
#define LOADPKG_RVA 0x1700510
static const short LOADPKG_SIG[] = {
    0x48,0x8b,0xc4,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0xa8,0xd8,0xfc,0xff,0xff,
    0x48,0x81,0xec,0xf0,0x03,0x00,0x00,0x48,0xc7,0x45,0x58,0xfe,0xff,0xff,0xff,0x48,0x89,0x58,0x20,0x0f,0x29,0x78,0xb8};

/* Times LoadPackage and logs any load of 5 ms or more with its caller. */
static void *load_package_hook(void *outer, const wchar_t *name, unsigned flags, void *ar) {
    void *frames[12]; USHORT nf = RtlCaptureStackBackTrace(0, 12, frames, NULL);
    LARGE_INTEGER t0, t1; QueryPerformanceCounter(&t0);
    void *r = original_load_package(outer, name, flags, ar);
    QueryPerformanceCounter(&t1);
    double ms = (t1.QuadPart - t0.QuadPart) * qpc_ticks_to_ms;
    if (ms >= 5) {
        SYSTEMTIME st; GetLocalTime(&st);
        char rvas[12 * 12] = "", *p = rvas;
        ULONG_PTR base = (ULONG_PTR)GetModuleHandleW(NULL);
        for (int i = 1; i < nf; i++)   // frame 0 is this hook
            p += sprintf(p, " %llX", (unsigned long long)((ULONG_PTR)frames[i] - base));
        log_line("load\t%02d:%02d:%02d.%03d\t%.1f\t%s\t%ls\t%s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, ms,
              GetCurrentThreadId() == load_log_thread ? "main" : "other", name ? name : L"(null)", rvas);
    }
    return r;
}

/* Hooks LoadPackage for the diagnostic load log. */
void install_load_log(void) {
    unsigned char *p = find_signature(LOADPKG_SIG, sizeof LOADPKG_SIG / sizeof LOADPKG_SIG[0], LOADPKG_RVA);
    if (!p) { log_line("load log: signature not found\n"); return; }
    LARGE_INTEGER f; QueryPerformanceFrequency(&f); qpc_ticks_to_ms = 1000.0 / f.QuadPart;
    load_log_thread = GetCurrentThreadId();   // DllMain runs on the game's main thread
    if ((original_load_package = (LoadPkg_t)hook_function(p, 14, (void *)load_package_hook))) log_line("load log installed (loads >= 5 ms)\n");
}
