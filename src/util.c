// Small helpers shared by the fixes: finding game code, hooking it, patching bytes, hashing.

#include "unchained.h"

/* Compares memory with a byte signature; 0x100 in the signature matches any byte. */
int signature_matches(const unsigned char *p, const short *sig, size_t n) {
    for (size_t i = 0; i < n; i++) if (sig[i] != 0x100 && p[i] != sig[i]) return 0;
    return 1;
}

/* Finds a signature: at its known RVA first, otherwise by scanning the executable sections (for when
   the game was updated). Returns NULL if it isn't there. */
unsigned char *find_signature(const short *sig, size_t n, size_t known_rva) {
    unsigned char *base = (unsigned char *)GetModuleHandleW(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    if (nt->OptionalHeader.SizeOfImage > known_rva + n && signature_matches(base + known_rva, sig, n)) return base + known_rva;
    IMAGE_SECTION_HEADER *s = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, s++) {
        if (!(s->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        unsigned char *p = base + s->VirtualAddress, *end = p + s->Misc.VirtualSize - n;
        for (; p < end; p++) if (signature_matches(p, sig, n)) return p;
    }
    return NULL;
}

// Redirect `target` to `dst` with a 14-byte absolute jmp. The first `prologue` bytes (>= 14, whole
// position-independent instructions) move to a trampoline that jumps back; returns the trampoline
// (call it to run the original function) or NULL on failure.
void *hook_function(unsigned char *target, int prologue, void *dst) {
    unsigned char *tramp = VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp) return NULL;
    unsigned char jmp[14] = {0xFF, 0x25, 0, 0, 0, 0};
    memcpy(tramp, target, prologue);
    void *back = target + prologue; memcpy(jmp + 6, &back, 8);
    memcpy(tramp + prologue, jmp, 14);
    DWORD old;
    if (!VirtualProtect(target, prologue, PAGE_EXECUTE_READWRITE, &old)) return NULL;
    memcpy(jmp + 6, &dst, 8);
    memcpy(target, jmp, 14);
    for (int i = 14; i < prologue; i++) target[i] = 0x90; // never executed
    VirtualProtect(target, prologue, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, prologue);
    return tramp;
}

/* Overwrites game code in place. */
void patch_bytes(unsigned char *p, const void *bytes, int n) {
    DWORD old;
    if (!VirtualProtect(p, n, PAGE_EXECUTE_READWRITE, &old)) return;
    memcpy(p, bytes, n);
    VirtualProtect(p, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, n);
}

/* Copies s to out[n...], cut off to fit cap (terminator included); returns the new length. */
int wcs_append(wchar_t *out, int cap, int n, const wchar_t *s) {
    while (*s && n < cap - 1) out[n++] = *s++;
    if (n < cap) out[n] = 0;
    return n;
}

/* FNV-1a, continued from h. */
unsigned long long fnv1a(const void *p, size_t n, unsigned long long h) {
    for (size_t i = 0; i < n; i++) h = (h ^ ((const unsigned char *)p)[i]) * 0x100000001b3ull;
    return h;
}

/* Fingerprint of one file: lowercased path, size and modified time. Never 0. */
unsigned long long file_fingerprint(const wchar_t *path, unsigned long long size, unsigned long long mtime) {
    unsigned long long h = 0xcbf29ce484222325ull;
    for (const wchar_t *c = path; *c; c++) { wchar_t l = towlower(*c); h = fnv1a(&l, 2, h); }
    h = fnv1a(&size, 8, h); h = fnv1a(&mtime, 8, h);
    return h | 1;
}

/* Keeps the `keep` most recently modified entries of dir that match pattern (files or flat folders)
   and deletes the rest. Used to cap the per-mod-list caches. */
void keep_newest(const wchar_t *dir, const wchar_t *pattern, int keep) {
    static struct { wchar_t name[MAX_PATH]; unsigned long long t; int folder; } e[65];   // ponytail: only the first 64 entries are considered; fine for a cap of 5
    wchar_t p[MAX_PATH * 2], q[MAX_PATH * 3]; WIN32_FIND_DATAW fd; int n = 0;
    swprintf(p, MAX_PATH * 2, L"%ls\\%ls", dir, pattern);
    HANDLE h = FindFirstFileW(p, &fd); if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == L'.' || n == 64) continue;
        wcscpy(e[n].name, fd.cFileName); e[n].folder = !!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
        e[n++].t = ((unsigned long long)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    for (int i = 1; i < n; i++) for (int j = i; j > 0 && e[j].t > e[j - 1].t; j--) { e[64] = e[j]; e[j] = e[j - 1]; e[j - 1] = e[64]; }   // newest first; e[64] = swap slot
    for (int i = keep; i < n; i++) {
        swprintf(p, MAX_PATH * 2, L"%ls\\%ls", dir, e[i].name);
        if (!e[i].folder) { DeleteFileW(p); continue; }
        swprintf(q, MAX_PATH * 3, L"%ls\\*", p);
        if ((h = FindFirstFileW(q, &fd)) != INVALID_HANDLE_VALUE) {
            do { if (fd.cFileName[0] != L'.') { swprintf(q, MAX_PATH * 3, L"%ls\\%ls", p, fd.cFileName); DeleteFileW(q); } } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        RemoveDirectoryW(p);
    }
}

/* Names the saved-data slot for this launch after the server the game is about to join: Game.ini's
   LastConnected (Conay and the game's own server browser set it before launching or restarting with
   that server's mods), "singleplayer" for a local session, "unknown" if neither is set. Only letters,
   digits, '.' and '-' are kept; anything else (the ':' before the port) becomes '_'. */
void read_server_slot(const wchar_t *ini, wchar_t *out, int n) {
    char line[512], ip[256] = ""; int listen = 0;
    FILE *f = _wfopen(ini, L"rb");
    while (f && fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!strncmp(line, "LastConnected=", 14)) snprintf(ip, sizeof ip, "%.255s", line + 14);
        if (!_stricmp(line, "StartedListenServerSession=True")) listen = 1;
    }
    if (f) fclose(f);
    const char *s = listen ? "singleplayer" : *ip ? ip : "unknown"; int i = 0;
    for (; s[i] && i < n - 1; i++) out[i] = isalnum((unsigned char)s[i]) || s[i] == '.' || s[i] == '-' ? s[i] : L'_';
    out[i] = 0;
}
