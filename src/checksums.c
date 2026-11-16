// Mod checksum cache. UModManager::LoadModInfo MD5s every mod .pak on every launch
// (FMD5Hash::HashFile). We remember each hash by path + size + modified time, and let the game's own
// code hash anything new or changed, so servers always get the same checksum.

#include "unchained.h"
#include <bcrypt.h>

typedef struct { unsigned char valid; unsigned char bytes[16]; } FMD5Hash; // UE4 layout, 17 bytes
typedef FMD5Hash *(*HashFile_t)(FMD5Hash *out, const wchar_t *name, void *buffer);

// Start of FMD5Hash::HashFile. 0x100 = wildcard (call rel32).
static const short HASHFILE_SIG[] = {
    0x48,0x89,0x5c,0x24,0x08, 0x48,0x89,0x74,0x24,0x10, 0x57, 0x48,0x83,0xec,0x20, // prologue (15 bytes, relocated to trampoline)
    0x49,0x8b,0xf8, 0x48,0x8b,0xda, 0x48,0x8b,0xf1, 0xe8,0x100,0x100,0x100,0x100,
    0x45,0x33,0xc0, 0x48,0x8b,0xd3, 0x48,0x8b,0xc8, 0x4c,0x8b,0x08, 0x41,0xff,0x51,0x18,
    0x4c,0x8b,0xc7, 0x48,0x8b,0xce, 0x48,0x8b,0xd0, 0x48,0x8b,0xd8, 0xe8};
#define HASHFILE_SIG_LEN (sizeof HASHFILE_SIG / sizeof HASHFILE_SIG[0])
#define HASHFILE_PROLOGUE 15
#define HASHFILE_RVA 0x15EFA20

typedef struct { unsigned long long size, mtime; FMD5Hash h; wchar_t *path; int seen; } ChecksumEntry;   // seen: hashed this launch
static ChecksumEntry *checksum_entries; static int checksum_count, checksum_capacity; // linear search is fine for a few hundred mods
static HashFile_t original_hash_file;
static SRWLOCK checksum_lock = SRWLOCK_INIT;
static int checksums_loaded;


/* Looks up a pak in the checksum cache by path (case-insensitive). Returns NULL if it isn't there. */
static ChecksumEntry *find_checksum(const wchar_t *path) {
    for (int i = checksum_count - 1; i >= 0; i--) if (!_wcsicmp(checksum_entries[i].path, path)) return &checksum_entries[i];
    return NULL;
}

/* Adds or updates the cached checksum for a pak. */
static void store_checksum(const wchar_t *path, unsigned long long size, unsigned long long mtime, const FMD5Hash *h) {
    ChecksumEntry *e = find_checksum(path);
    if (!e) {
        if (checksum_count == checksum_capacity) { checksum_capacity = checksum_capacity ? checksum_capacity * 2 : 128; checksum_entries = realloc(checksum_entries, checksum_capacity * sizeof *checksum_entries); }
        e = &checksum_entries[checksum_count++]; e->path = _wcsdup(path); e->seen = 0;
    }
    e->size = size; e->mtime = mtime; e->h = *h;
}

// Cache file: one line per file: "<size hex> <mtime hex> <34 hex chars> <utf8 path>". Later lines win.
static void load_checksums(void) {
    FILE *f = _wfopen(checksum_path, L"rb"); if (!f) return;
    char line[2048];
    while (fgets(line, sizeof line, f)) {
        unsigned long long size, mtime; char hex[35]; int off = 0;
        if (sscanf(line, "%llx %llx %34s %n", &size, &mtime, hex, &off) != 3 || !off || strlen(hex) != 34) continue;
        line[strcspn(line, "\r\n")] = 0;
        FMD5Hash h; unsigned char *b = (unsigned char *)&h;
        for (int i = 0; i < 17; i++) { unsigned v; sscanf(hex + 2 * i, "%2x", &v); b[i] = (unsigned char)v; }
        wchar_t wpath[MAX_PATH * 2];
        if (!MultiByteToWideChar(CP_UTF8, 0, line + off, -1, wpath, MAX_PATH * 2)) continue;
        store_checksum(wpath, size, mtime, &h);
    }
    fclose(f);
}

/* Writes one checksum line to the end of checksums.txt. */
static void append_checksum(const wchar_t *path, unsigned long long size, unsigned long long mtime, const FMD5Hash *h) {
    char u8[MAX_PATH * 4];
    if (!WideCharToMultiByte(CP_UTF8, 0, path, -1, u8, sizeof u8, NULL, NULL)) return;
    FILE *f = _wfopen(checksum_path, L"ab"); if (!f) return;
    fprintf(f, "%llx %llx ", size, mtime);
    for (int i = 0; i < 17; i++) fprintf(f, "%02x", ((const unsigned char *)h)[i]);
    fprintf(f, " %s\n", u8);
    fclose(f);
}

// Parallel pre-hashing. The game hashes the mods one at a time on one core (~100 s for 39 GB). At
// launch we read modlist.txt, and every mod with no valid saved checksum is hashed on worker threads
// with Windows' MD5 (plain MD5 of the whole file, the same value the game produces). When the game
// asks for that file, the hook waits for the worker's result instead of hashing it again.
typedef struct { wchar_t path[MAX_PATH]; unsigned long long size, mtime; FMD5Hash h; volatile LONG state; } PrehashJob;   // state: 0 queued, 1 running, 2 done, 3 failed
static PrehashJob *prehash_jobs; static int prehash_count; static volatile LONG prehash_next;
static int prehash_verified; volatile LONG prehash_disabled;
#define PREHASH_THREADS 8
#define PREHASH_VERIFY_MAX (64ull << 20)   // the first prehashed file under 64 MB is re-hashed by the game to compare

/* Plain MD5 of a whole file with Windows' BCrypt. Returns 1 on success. */
static int md5_file(const wchar_t *path, FMD5Hash *out) {
    BCRYPT_ALG_HANDLE alg; BCRYPT_HASH_HANDLE hh; int ok = 0;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_MD5_ALGORITHM, NULL, 0)) return 0;
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    unsigned char *buf = malloc(4 << 20);
    if (f != INVALID_HANDLE_VALUE && buf && !BCryptCreateHash(alg, &hh, NULL, 0, NULL, 0, 0)) {
        DWORD got; ok = 1;
        while (ok && (ok = ReadFile(f, buf, 4 << 20, &got, NULL)) && got) ok = !BCryptHashData(hh, buf, got, 0);
        ok = ok && !BCryptFinishHash(hh, out->bytes, 16, 0);
        BCryptDestroyHash(hh);
    }
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    free(buf); BCryptCloseAlgorithmProvider(alg, 0);
    out->valid = (unsigned char)ok;
    return ok;
}

/* Worker: takes jobs in modlist order until none are left. */
static DWORD WINAPI prehash_worker(void *arg) {
    (void)arg;
    for (LONG i; (i = InterlockedIncrement(&prehash_next) - 1) < prehash_count;) {
        PrehashJob *j = &prehash_jobs[i];
        InterlockedExchange(&j->state, 1);
        InterlockedExchange(&j->state, md5_file(j->path, &j->h) ? 2 : 3);
    }
    return 0;
}

/* Full path with backslashes, for comparing the game's names with modlist.txt's. */
static void normalize_path(const wchar_t *in, wchar_t *out) {
    if (!GetFullPathNameW(in, MAX_PATH, out, NULL)) wcscpy(out, in);
}

/* Queues every mod in modlist.txt that has no valid saved checksum, then starts the workers.
   Does nothing (no threads) when every mod is already saved. */
static void start_prehash(void) {
    wchar_t game[MAX_PATH], list[MAX_PATH], rel[MAX_PATH], full[MAX_PATH * 2]; char line[1024];
    if (!GetModuleFileNameW(NULL, game, MAX_PATH)) return;
    for (int up = 3; up; up--) { wchar_t *sl = wcsrchr(game, L'\\'); if (sl) *sl = 0; }   // ...\ConanSandbox
    swprintf(list, MAX_PATH, L"%ls\\Mods\\modlist.txt", game);
    FILE *f = _wfopen(list, L"rb"); if (!f) return;
    AcquireSRWLockExclusive(&checksum_lock);
    if (!checksums_loaded) { load_checksums(); checksums_loaded = 1; }
    while (fgets(line, sizeof line, f)) {
        char *l = line; if (!memcmp(l, "\xEF\xBB\xBF", 3)) l += 3;
        l[strcspn(l, "\r\n")] = 0;
        if (!*l || !MultiByteToWideChar(CP_UTF8, 0, l, -1, rel, MAX_PATH)) continue;
        if (wcschr(rel, L':')) wcscpy(full, rel); else swprintf(full, MAX_PATH * 2, L"%ls\\Mods\\%ls", game, rel);
        PrehashJob j = {0}; WIN32_FILE_ATTRIBUTE_DATA fa;
        normalize_path(full, j.path);
        if (!GetFileAttributesExW(j.path, GetFileExInfoStandard, &fa)) continue;
        j.size = ((unsigned long long)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
        j.mtime = ((unsigned long long)fa.ftLastWriteTime.dwHighDateTime << 32) | fa.ftLastWriteTime.dwLowDateTime;
        int saved = 0;   // saved under the game's spelling of the path (forward slashes)
        for (int i = 0; i < checksum_count && !saved; i++) {
            wchar_t n[MAX_PATH]; normalize_path(checksum_entries[i].path, n);
            saved = !_wcsicmp(n, j.path) && checksum_entries[i].size == j.size && checksum_entries[i].mtime == j.mtime;
        }
        if (saved) continue;
        PrehashJob *grown = realloc(prehash_jobs, (prehash_count + 1) * sizeof *prehash_jobs);
        if (!grown) break;
        prehash_jobs = grown; prehash_jobs[prehash_count++] = j;
    }
    ReleaseSRWLockExclusive(&checksum_lock);
    fclose(f);
    if (!prehash_count) return;
    log_line("prehash: %d mods without a saved checksum, hashing on %d threads\n", prehash_count, PREHASH_THREADS);
    for (int i = 0; i < PREHASH_THREADS && i < prehash_count; i++) {
        HANDLE t = CreateThread(NULL, 0, prehash_worker, NULL, 0, NULL);
        if (t) { SetThreadPriority(t, THREAD_PRIORITY_BELOW_NORMAL); CloseHandle(t); }
    }
}

/* Finds a queued job for a file the game asks about (same file, size and time). */
static PrehashJob *find_prehash(const wchar_t *name, unsigned long long size, unsigned long long mtime) {
    if (prehash_disabled || !prehash_count) return NULL;
    wchar_t n[MAX_PATH]; normalize_path(name, n);
    for (int i = 0; i < prehash_count; i++)
        if (!_wcsicmp(prehash_jobs[i].path, n) && prehash_jobs[i].size == size && prehash_jobs[i].mtime == mtime) return &prehash_jobs[i];
    return NULL;
}

/* Replacement for FMD5Hash::HashFile: returns the saved hash when the file's size and modified
   time are unchanged, otherwise lets the game hash it and saves the result. */
static FMD5Hash *hash_file_hook(FMD5Hash *out, const wchar_t *name, void *buffer) {
    WIN32_FILE_ATTRIBUTE_DATA fa;
    // Only cache plain hashes; if the caller wants the file contents (buffer) let the game do it.
    if (buffer || !name || !GetFileAttributesExW(name, GetFileExInfoStandard, &fa)) return original_hash_file(out, name, buffer);
    unsigned long long size = ((unsigned long long)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
    unsigned long long mtime = ((unsigned long long)fa.ftLastWriteTime.dwHighDateTime << 32) | fa.ftLastWriteTime.dwLowDateTime;

    AcquireSRWLockExclusive(&checksum_lock);
    if (!checksums_loaded) { load_checksums(); checksums_loaded = 1; }
    ChecksumEntry *e = find_checksum(name);
    if (e && e->size == size && e->mtime == mtime) {
        *out = e->h; e->seen = 1;
        ReleaseSRWLockExclusive(&checksum_lock);
        log_line("hit  %ls\n", name);
        return out;
    }
    ReleaseSRWLockExclusive(&checksum_lock);

    DWORD t = GetTickCount();
    PrehashJob *j = find_prehash(name, size, mtime);
    while (j && j->state < 2) Sleep(1);   // a worker is on it (or will be soon)
    int prehashed = j && j->state == 2 && !prehash_disabled;
    if (prehashed && !prehash_verified && size <= PREHASH_VERIFY_MAX) {
        // Safety check: the game hashes the first small prehashed file itself and must get the same result.
        original_hash_file(out, name, buffer); prehash_verified = 1;
        if (!out->valid || memcmp(out, &j->h, sizeof *out)) { prehash_disabled = 1; log_line("prehash DISABLED: %ls differs from the game's hash\n", name); }
        else log_line("prehash checked against the game: %ls matches\n", name);
    } else if (prehashed) {
        *out = j->h; log_line("miss %ls (prehashed, waited %lu ms)\n", name, GetTickCount() - t);
    } else {
        original_hash_file(out, name, buffer); log_line("miss %ls (%lu ms)\n", name, GetTickCount() - t);
    }
    if (out->valid) {
        AcquireSRWLockExclusive(&checksum_lock);
        store_checksum(name, size, mtime, out); find_checksum(name)->seen = 1;
        append_checksum(name, size, mtime, out);
        ReleaseSRWLockExclusive(&checksum_lock);
    }
    return out;
}


/* Order-independent fingerprint of every mod pak checksummed this launch (0 if none). Used to tell
   whether the saved folder lists still apply. */
unsigned long long checksums_fingerprint(void) {
    unsigned long long s = 0;
    AcquireSRWLockExclusive(&checksum_lock);
    for (int i = 0; i < checksum_count; i++)
        if (checksum_entries[i].seen) s += file_fingerprint(checksum_entries[i].path, checksum_entries[i].size, checksum_entries[i].mtime);
    ReleaseSRWLockExclusive(&checksum_lock);
    return s;
}

/* Hooks FMD5Hash::HashFile (found by signature if the game moved it). */
int install_checksum_cache(void) {
    unsigned char *target = find_signature(HASHFILE_SIG, HASHFILE_SIG_LEN, HASHFILE_RVA);
    if (!target) { log_line("HashFile signature not found - game updated? Hook NOT installed, game runs unmodified.\n"); return 0; }

    original_hash_file = (HashFile_t)hook_function(target, HASHFILE_PROLOGUE, (void *)hash_file_hook);
    if (!original_hash_file) return 0;
    log_line("hook installed at rva 0x%llX\n", (unsigned long long)(target - (unsigned char *)GetModuleHandleW(NULL)));
    return 1;
}

/* Starts hashing new mods in the background; needs the checksum cache hook (-1: that one is switched off). */
int install_prehash(void) {
    if (!original_hash_file) return switch_on(L"nomd5") ? -1 : 0;
    start_prehash();
    return 1;
}
