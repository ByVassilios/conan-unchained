// Small one-off patches with no state of their own.

#include "unchained.h"

// UConanGameInstance::Init runs a WMI hardware survey (CPU/GPU/RAM/disks) up to 3 times for Funcom
// telemetry, blocking the main thread ~5 s. It is guarded by "if (setting) { ... }"; we turn the
// guard's conditional jump (je) into an unconditional jmp, i.e. the path taken when the setting is off.
static const short SURVEY_SIG[] = {
    0xe8,0x100,0x100,0x100,0x100, 0x48,0x8b,0xc8, 0x48,0x8d,0x15,0x100,0x100,0x100,0x100,
    0xe8,0x100,0x100,0x100,0x100, 0x80,0x38,0x00,
    0x0f,0x84,0x100,0x100,0x100,0x100,                     // je skip   <- patched (offset 23)
    0x80,0x3d,0x100,0x100,0x100,0x100,0x00, 0x74,0x06, 0xff,0x15,0x100,0x100,0x100,0x100,
    0x48,0x8d,0x95,0x20,0x03,0x00,0x00, 0x48,0x8b,0xce, 0xe8};
#define SURVEY_RVA 0x4A4BF6
#define SURVEY_JE 23

/* Turns the survey's "if (setting)" jump into an unconditional jump past it. */
int skip_survey(void) {
    unsigned char *p = find_signature(SURVEY_SIG, sizeof SURVEY_SIG / sizeof SURVEY_SIG[0], SURVEY_RVA);
    if (!p) { log_line("hardware survey signature not found - left unpatched.\n"); return 0; }
    p += SURVEY_JE;
    int rel; memcpy(&rel, p + 2, 4);
    unsigned char jmp[6] = {0xE9, 0, 0, 0, 0, 0x90};   // jmp rel32 is 1 byte shorter than je rel32
    rel += 1; memcpy(jmp + 1, &rel, 4);
    DWORD old;
    if (!VirtualProtect(p, 6, PAGE_EXECUTE_READWRITE, &old)) return 0;
    memcpy(p, jmp, 6);
    VirtualProtect(p, 6, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 6);
    log_line("hardware survey skipped at rva 0x%llX\n", (unsigned long long)(p - (unsigned char *)GetModuleHandleW(NULL)));
    return 1;
}

// While the paks are mounted the main thread mostly sleeps; the checker threads it waits on barely
// work. Two sleeps, both rounded up to the 15.6 ms timer tick. (Raising the timer resolution doesn't
// help: Windows 11 ignores it for a process that has no visible window yet.)
// - SetThreadName (0x162CFC0) does Sleep(10) on the creating thread for every new thread, only so a
//   debugger can catch the name. The pak signature checker starts one thread per pak.
// - The signed pak reader (0x324B700) polls its checker with FPlatformProcess::Sleep(0.001); the
//   constant is loaded at 0x324B877. Sleep(0) instead yields (SwitchToThread) and polls again.
// Both patches only apply to this exact game build.
int skip_pak_sleeps(void) {
    unsigned char *base = (unsigned char *)GetModuleHandleW(NULL);
    static const unsigned char nameSig[] = {0xb9,0x0a,0x00,0x00,0x00,0xff,0x15};          // mov ecx,10; call [Sleep]
    static const unsigned char pollSig[] = {0xf3,0x0f,0x10,0x35,0x31,0x93,0xe1,0x00};     // movss xmm6,[1 ms]
    static const unsigned char pollNew[] = {0x0f,0x57,0xf6, 0x0f,0x1f,0x44,0x00,0x00};    // xorps xmm6,xmm6; nop5
    if (memcmp(base + 0x162CFCF, nameSig, sizeof nameSig) || memcmp(base + 0x324B877, pollSig, sizeof pollSig)) {
        log_line("pak mount sleeps: game build differs - left alone.\n"); return 0;
    }
    unsigned char zero = 0;
    patch_bytes(base + 0x162CFD0, &zero, 1);
    patch_bytes(base + 0x324B877, pollNew, sizeof pollNew);
    log_line("pak mount sleeps removed\n");
    return 1;
}

// Joining blocks the game thread in a single frame for 10-15 s. After 5 s Windows swaps the window
// for a white "(Not Responding)" ghost; with ghosting off the loading screen just stays up.
// GetProcAddress so we don't add a user32 import (the game has it loaded already).
int disable_window_ghosting(void) {
    HMODULE u = GetModuleHandleW(L"user32.dll");
    void (WINAPI *noGhost)(void) = u ? (void (WINAPI *)(void))GetProcAddress(u, "DisableProcessWindowsGhosting") : NULL;
    if (!noGhost) return 0;
    noGhost(); log_line("window ghosting off\n");
    return 1;
}

typedef struct { unsigned rva; unsigned char len; const char *old, *neu; } HexPatch;

/* Applies every patch in the table, or none if any site's bytes differ from this game build. */
static int apply_hex_patches(const HexPatch *s, int n) {
    unsigned char *base = (unsigned char *)GetModuleHandleW(NULL), old[32], neu[32];
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < s[i].len; j++) {
                sscanf(s[i].old + 2 * j, "%2hhx", &old[j]);
                sscanf(s[i].neu + 2 * j, "%2hhx", &neu[j]);
            }
            if (pass == 0 && memcmp(base + s[i].rva, old, s[i].len)) return 0;
            if (pass == 1) patch_bytes(base + s[i].rva, neu, s[i].len);
        }
    return 1;
}

// Occlusion culling keeps a history per (primitive id, sub-query index) in a hash set. Its hash is
// PrimId ^ (SubIndex >> 20), so every sub-query of one primitive (instanced meshes, foliage) lands in
// the same bucket and each lookup walks the whole chain: O(n^2) per primitive per frame. Traced at
// 6.8 s of 35 s render-thread time near big builds (0x1CF95D0, one 16-byte loop). We change the hash
// to PrimId ^ (SubIndex * 0x9E3779B1) at all 5 places that compute it (insert, 3 lookups, rehash).
// SubIndex 0 hashes as before. All-or-nothing: any byte mismatch leaves every site alone.
int fix_occlusion_hash(void) {
    static const HexPatch s[] = {
        {0x1CF953B, 27, "418b57404c8d4e388b46484533c0ffc8c1ea1441331723d0418bc0",
                        "41695740b179379e4c8d4e388b4648ffc841331723d04533c033c0"},
        {0x1CF98E9, 12, "488b55d048c1ea2048c1ea14", "6955d4b179379e0f1f440000"},
        {0x1CFA65D, 12, "488b55d848c1ea2048c1ea14", "6955dcb179379e0f1f440000"},
        {0x1D29303, 11, "4d8bc849c1e92049c1e914",   "44694c241cb179379e6690"},
        {0x1D34A98,  8, "458b484041c1e914",         "45694840b179379e"},
    };
    if (!apply_hex_patches(s, 5)) { log_line("occlusion hash: game build differs - left alone.\n"); return 0; }
    log_line("occlusion hash fixed (5 sites)\n");
    return 1;
}
