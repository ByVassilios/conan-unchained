// Pak directory search skip
// FPakFile::FindFilesAtPath(OutFiles TSet<FString>, Path, bFiles, bDirs, bRecursive) loops over the
// pak's ENTIRE directory index (building a string per directory) whenever Path and the mount point
// overlap. ~45 base paks are mounted at "../../../", so every directory listing during the mod
// controller asset scan walks all of them: 1-2 minutes of CPU on joins with many mods.
// Exact shortcut: if a pak returns nothing for folder A/ (files + folders), it holds nothing anywhere
// under A/, so later searches of A/... in that pak return nothing too. We remember those empty
// (pak, folder) pairs and skip the search. Nothing else changes.

#include "unchained.h"

typedef void (*FindFiles_t)(void *pak, void *outSet, const wchar_t *path, char files, char dirs, char recursive);
static FindFiles_t original_find_files;
static const short FIND_SIG[] = {
    0x44,0x88,0x4c,0x24,0x20, 0x55, 0x56, 0x57, 0x41,0x54, 0x41,0x55, 0x41,0x56,   // prologue (14 bytes)
    0x41,0x57, 0x48,0x8d,0xac,0x24,0x70,0xff,0xff,0xff, 0x48,0x81,0xec,0x90,0x01,0x00,0x00,
    0x48,0xc7,0x45,0x50,0xfe,0xff,0xff,0xff, 0x48,0x89,0x9c,0x24,0xd8,0x01,0x00,0x00,
    0x49,0x8b,0xf8, 0x4c,0x8b,0xfa, 0x48,0x8b,0xf1};
#define FIND_RVA 0x3236260
#define PAK_MOUNT 0xC8     // FString MountPoint inside FPakFile
#define PAK_INDEXNUM 0xF0  // Index TMap element count (part of the cache key: detects a different pak at a reused address)
#define SET_NUM 8          // TSet<FString> element count

// We keep 64-bit hashes of (pak, folder) rather than the strings. A wrong "empty" would need a
// 64-bit collision, about 1 in 10^10 at 100k entries.
static unsigned long long *empty_set; static size_t empty_set_capacity, empty_set_count;
static SRWLOCK empty_set_lock = SRWLOCK_INIT;
static volatile LONG64 pak_search_calls, pak_search_skipped, empty_folders_known;
volatile LONG pak_skip_disabled; 
int verify_every_skip;   // unchained\fullcheck: re-check every skip (slow, for testing)

/* Hash that identifies one mounted pak: its address, index size and mount point. */
static unsigned long long pak_key(const unsigned char *pak) {
    unsigned long long h = 1469598103934665603ULL ^ (unsigned long long)(ULONG_PTR)pak;
    h = (h ^ *(const int *)(pak + PAK_INDEXNUM)) * 1099511628211ULL;
    const wchar_t *m = *(wchar_t *const *)(pak + PAK_MOUNT);
    if (m) for (; *m; m++) h = (h ^ (unsigned)towlower(*m)) * 1099511628211ULL;
    return h;
}

// Hash of (pak, first n chars of path [+ '/']); lowercased, '\\' treated as '/'.
static unsigned long long folder_key(unsigned long long pk, const wchar_t *p, size_t n, int addSlash) {
    unsigned long long h = pk;
    for (size_t i = 0; i < n; i++) { wchar_t c = p[i] == L'\\' ? L'/' : (wchar_t)towlower(p[i]); h = (h ^ c) * 1099511628211ULL; }
    if (addSlash) h = (h ^ L'/') * 1099511628211ULL;
    return h | 1; // 0 marks an empty slot
}

/* Is this (pak, folder) hash in the empty-folder set? Caller holds negLock. */
static int empty_set_has(unsigned long long k) {
    if (!empty_set_capacity) return 0;
    for (size_t i = k & (empty_set_capacity - 1);; i = (i + 1) & (empty_set_capacity - 1)) {
        if (empty_set[i] == k) return 1;
        if (!empty_set[i]) return 0;
    }
}

/* Adds a hash to the empty-folder set (open addressing, grows at half full). Caller holds negLock
   exclusively. */
static void empty_set_add(unsigned long long k) {
    if ((empty_set_count + 1) * 2 > empty_set_capacity) {
        size_t nc = empty_set_capacity ? empty_set_capacity * 2 : 1 << 16;
        unsigned long long *ns = calloc(nc, sizeof *ns), *os = empty_set;
        if (!ns) return;
        size_t oc = empty_set_capacity; empty_set = ns; empty_set_capacity = nc; empty_set_count = 0;
        for (size_t i = 0; i < oc; i++) if (os[i]) empty_set_add(os[i]);
        free(os);
    }
    size_t i = k & (empty_set_capacity - 1);
    while (empty_set[i] && empty_set[i] != k) i = (i + 1) & (empty_set_capacity - 1);
    if (!empty_set[i]) { empty_set[i] = k; empty_set_count++; }
}

// Skip only on threads inside a scan scope: the mod controller search, or an asset folder scan
// (0x2139AF0: the background asset scan started at launch, and the folder listings). The cache is
// wiped when the first scope starts and when the last one ends, so nothing learned outlives the paks
// mounted at that moment (leaving a server and rejoining got stuck loading when the cache lived for
// the whole session).
typedef struct { DWORD tid; int depth; } Scope;
static Scope scan_scopes[8]; static int active_scopes; static SRWLOCK scope_lock = SRWLOCK_INIT;

/* Forgets every empty folder. */
static void empty_set_clear(void) {
    AcquireSRWLockExclusive(&empty_set_lock);
    if (empty_set) memset(empty_set, 0, empty_set_capacity * sizeof *empty_set);
    empty_set_count = 0; empty_folders_known = 0;
    ReleaseSRWLockExclusive(&empty_set_lock);
}
/* True if the calling thread is inside a mod search or an asset folder scan. */
static int in_scope(void) {
    DWORD t = GetCurrentThreadId(); int r = 0;
    AcquireSRWLockShared(&scope_lock);
    for (int i = 0; i < 8 && !r; i++) r = scan_scopes[i].depth && scan_scopes[i].tid == t;
    ReleaseSRWLockShared(&scope_lock);
    return r;
}
// Returns 0 if no slot was free (that call just runs unpatched).
static int scope_enter(void) {
    DWORD t = GetCurrentThreadId(); int ok = 0, first = 0;
    AcquireSRWLockExclusive(&scope_lock);
    for (int i = 0; i < 8 && !ok; i++) if (scan_scopes[i].depth && scan_scopes[i].tid == t) { scan_scopes[i].depth++; ok = 1; }
    for (int i = 0; i < 8 && !ok; i++) if (!scan_scopes[i].depth) { scan_scopes[i].tid = t; scan_scopes[i].depth = 1; ok = 1; first = !active_scopes++; }
    if (first) empty_set_clear();
    ReleaseSRWLockExclusive(&scope_lock);
    return ok;
}
/* Leaves the calling thread's scope; the last scope to end wipes the empty-folder set. */
static void scope_exit(void) {
    DWORD t = GetCurrentThreadId(); int last = 0;
    AcquireSRWLockExclusive(&scope_lock);
    for (int i = 0; i < 8; i++) if (scan_scopes[i].depth && scan_scopes[i].tid == t) { if (!--scan_scopes[i].depth) last = !--active_scopes; break; }
    if (last) { log_line("scans done: %lld pak searches, %lld skipped\n", pak_search_calls, pak_search_skipped); empty_set_clear(); }
    ReleaseSRWLockExclusive(&scope_lock);
}

/* Replacement for FPakFile::FindFilesAtPath: skips the search when the folder (or a parent) is
   known to be empty in this pak, and records folders that come back empty. */
static void find_files_hook(void *pak, void *outSet, const wchar_t *path, char files, char dirs, char recursive) {
    if (!path || !pak || !in_scope()) { original_find_files(pak, outSet, path, files, dirs, recursive); return; }
    LONG64 n = InterlockedIncrement64(&pak_search_calls);
    if (n % 50000 == 0) log_line("pak search: %lld calls, %lld skipped, %lld empty folders known\n", n, pak_search_skipped, empty_folders_known);

    unsigned long long pk = pak_key(pak);
    size_t len = wcslen(path);
    // Is this folder, or any parent folder, already known to be empty in this pak?
    AcquireSRWLockShared(&empty_set_lock);
    int skip = 0;
    for (size_t i = 0; i < len && !skip; i++)
        if (path[i] == L'/' || path[i] == L'\\') skip = empty_set_has(folder_key(pk, path, i + 1, 0));
    int noSlash = len && path[len - 1] != L'/' && path[len - 1] != L'\\';
    if (!skip && noSlash) skip = empty_set_has(folder_key(pk, path, len, 1));   // folder given without trailing slash
    ReleaseSRWLockShared(&empty_set_lock);
    if (skip && !pak_skip_disabled) {
        LONG64 k = InterlockedIncrement64(&pak_search_skipped);
        if (!verify_every_skip && k > 2000 && k % 256) return;
        // Safety net: double-check the first 2000 skips and 1 in 256 after that against the real
        // search. Any disagreement switches the skip off for the rest of the session.
        unsigned char scratch[0x50] = {0};
        *(int *)(scratch + 0x30) = -1;   // empty TSet, as the game builds it (FirstFreeIndex = -1)
        original_find_files(pak, scratch, path, files, dirs, recursive);
        if (*(int *)(scratch + SET_NUM) == 0) return;
        pak_skip_disabled = 1;   // the scratch set's strings leak here, but only once
        log_line("pak search skip DISABLED: '%ls' not empty in pak mounted at '%ls'\n", path, *(wchar_t *const *)((char *)pak + PAK_MOUNT));
    }

    int before = *(int *)((char *)outSet + SET_NUM);
    original_find_files(pak, outSet, path, files, dirs, recursive);
    // Only folders at/below the mount point (or unrelated to it) prove "nothing below here". A folder
    // ABOVE a deep mount point (e.g. Content/Mods/ for a pak mounted at Content/Mods/X/) may come back
    // empty while the pak still has files further down, so never remember those.
    const wchar_t *m = *(wchar_t *const *)((char *)pak + PAK_MOUNT);
    size_t mlen = m ? wcslen(m) : 0, plen = len + noSlash;
    int aboveMount = 0;
    if (m && plen < mlen) {
        aboveMount = 1;   // path (+ '/') is a strict prefix of the mount point?
        for (size_t i = 0; i < plen && aboveMount; i++) {
            wchar_t a = i < len ? path[i] : L'/', b = m[i];
            if (a == L'\\') a = L'/';
            if (b == L'\\') b = L'/';
            aboveMount = towlower(a) == towlower(b);
        }
    }
    if (files && dirs && !aboveMount && before == 0 && *(int *)((char *)outSet + SET_NUM) == 0) {
        unsigned long long h = folder_key(pk, path, len, noSlash);
        AcquireSRWLockExclusive(&empty_set_lock);
        empty_set_add(h);
        ReleaseSRWLockExclusive(&empty_set_lock);
        InterlockedIncrement64(&empty_folders_known);
    }
}

// DreamworldMods "Searching for mod/dlc controllers" (rcx = this, dl = bool). Prologue: mov rax,rsp;
// 5 pushes; lea rbp,[rax-0x758] = 19 position-independent bytes. 0x100 = wildcard (cookie rip offset).
static const short MODSEARCH_SIG[] = {
    0x48,0x8b,0xc4,0x55,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0xa8,0xa8,0xf8,0xff,0xff,
    0x48,0x81,0xec,0x30,0x08,0x00,0x00,0x48,0xc7,0x85,0x68,0x01,0x00,0x00,0xfe,0xff,0xff,0xff,
    0x48,0x89,0x58,0x10,0x48,0x89,0x70,0x18,0x48,0x89,0x78,0x20,0x0f,0x29,0x70,0xc8,0x0f,0x29,0x78,0xb8,
    0x48,0x8b,0x05,0x100,0x100,0x100,0x100,0x48,0x33,0xc4};
#define MODSEARCH_RVA 0x37D1F50
typedef unsigned long long (*ModSearch_t)(void *, unsigned long long, unsigned long long, unsigned long long);
static ModSearch_t original_mod_search;

// The background scan thread never leaves its loop (it idles waiting for more paths), so its scope never
// ends. The mod search therefore always wipes the list at its start and end itself, as before: nothing
// learned before a (re)join is used in it, and nothing learned in it outlives it.
static unsigned long long mod_search_hook(void *a, unsigned long long b, unsigned long long c, unsigned long long d) {
    if (!scope_enter()) return original_mod_search(a, b, c, d);
    empty_set_clear();
    unsigned long long r = original_mod_search(a, b, c, d);
    empty_set_clear();
    log_line("mod search done: %lld pak searches, %lld skipped\n", pak_search_calls, pak_search_skipped);
    scope_exit();
    return r;
}
// Asset folder scan (FAssetDataDiscovery work loop, 0x2139AF0): same 19-byte prologue shape.
#define SCAN_RVA 0x2139AF0
static const unsigned char SCAN_SIG[] = {0x48,0x8b,0xc4,0x55,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0xa8,0x88,0xfe,0xff,0xff,
                                         0x48,0x81,0xec,0x50,0x02,0x00,0x00};
typedef void *(*Scan_t)(void *, void *, void *, void *);
static Scan_t original_folder_scan;
/* Wraps the asset folder scan so the pak search skip is active while it runs. */
static void *folder_scan_hook(void *a, void *b, void *c, void *d) {
    if (!scope_enter()) return original_folder_scan(a, b, c, d);
    void *r = original_folder_scan(a, b, c, d);
    scope_exit();
    return r;
}

/* Hooks FindFilesAtPath, the mod controller search and the asset folder scan. */
int install_pak_skip(void) {
    unsigned char *p = find_signature(FIND_SIG, sizeof FIND_SIG / sizeof FIND_SIG[0], FIND_RVA);
    if (!p) { log_line("pak search signature not found - left unpatched.\n"); return 0; }
    original_find_files = (FindFiles_t)hook_function(p, 14, (void *)find_files_hook);
    if (!original_find_files) return 0;
    unsigned char *s = find_signature(MODSEARCH_SIG, sizeof MODSEARCH_SIG / sizeof MODSEARCH_SIG[0], MODSEARCH_RVA);
    if (!s || !(original_mod_search = (ModSearch_t)hook_function(s, 19, (void *)mod_search_hook))) {
        log_line("mod controller search signature not found - pak search skip stays off.\n"); return 0;
    }
    log_line("pak search skip installed at rva 0x%llX (mod search 0x%llX)\n", (unsigned long long)(p - (unsigned char *)GetModuleHandleW(NULL)),
          (unsigned long long)(s - (unsigned char *)GetModuleHandleW(NULL)));
    unsigned char *sc = (unsigned char *)GetModuleHandleW(NULL) + SCAN_RVA;
    if (memcmp(sc, SCAN_SIG, sizeof SCAN_SIG)) { log_line("asset scan: game build differs - pak search skip stays limited to the mod search.\n"); return 1; }
    if ((original_folder_scan = (Scan_t)hook_function(sc, 19, (void *)folder_scan_hook))) log_line("pak search skip also covers asset folder scans\n");
    return 1;
}
