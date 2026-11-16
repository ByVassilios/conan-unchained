// Asset registry cache for cooked packages
// At launch the asset registry scans every package in every pak on two background threads (open,
// inflate and parse each header). The engine has a disk cache for this
// (Intermediate/AssetRegistryCache/<hash>.bin) and the game turns it on, but the gatherer (0x213A210)
// refuses to store cooked packages (PackageFlags & PKG_FilterEditorOnly, the jl at 0x213AD77). In a
// shipped game every package is cooked, so the cache stays empty and the scan repeats every launch.
// We NOP that jump. Packages without asset data still aren't stored (that's a separate check). We
// don't trust the cache's per-package timestamps, so each server gets its own cache, labeled with a
// fingerprint of every pak; if any pak changes, that server's cache starts fresh.

#include "unchained.h"

static unsigned long long pak_fingerprint(const wchar_t *path) {
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &a)) return file_fingerprint(path, 0, 0);
    return file_fingerprint(path, ((unsigned long long)a.nFileSizeHigh << 32) | a.nFileSizeLow,
                   ((unsigned long long)a.ftLastWriteTime.dwHighDateTime << 32) | a.ftLastWriteTime.dwLowDateTime);
}
/* Sum of the fingerprints of every *.pak in a folder. */
static unsigned long long folder_paks_fingerprint(const wchar_t *dir) {
    wchar_t pat[MAX_PATH * 2], full[MAX_PATH * 2]; WIN32_FIND_DATAW fd; unsigned long long s = 0;
    swprintf(pat, MAX_PATH * 2, L"%ls\\*.pak", dir);
    HANDLE h = FindFirstFileW(pat, &fd); if (h == INVALID_HANDLE_VALUE) return 0;
    do { swprintf(full, MAX_PATH * 2, L"%ls\\%ls", dir, fd.cFileName); s += pak_fingerprint(full); } while (FindNextFileW(h, &fd));
    FindClose(h); return s;
}
// game = ...\ConanSandbox. Base paks + local mod paks + every pak modlist.txt lists + the list's order.
// Not modlist.txt's modified time: launchers rewrite it (same content) before every launch.
static unsigned long long all_paks_fingerprint(const wchar_t *game) {
    wchar_t p[MAX_PATH * 2]; unsigned long long s = 0, order = 0xcbf29ce484222325ull;
    swprintf(p, MAX_PATH * 2, L"%ls\\Content\\Paks", game); s += folder_paks_fingerprint(p);
    swprintf(p, MAX_PATH * 2, L"%ls\\Mods", game); s += folder_paks_fingerprint(p);
    swprintf(p, MAX_PATH * 2, L"%ls\\Mods\\modlist.txt", game);
    FILE *f = _wfopen(p, L"rb"); char line[1024]; wchar_t w[MAX_PATH];
    while (f && fgets(line, sizeof line, f)) {
        char *l = line; if (!memcmp(l, "\xEF\xBB\xBF", 3)) l += 3;
        l[strcspn(l, "\r\n")] = 0; if (!*l) continue;
        order = fnv1a(l, strlen(l) + 1, order);   // mount order matters
        if (!MultiByteToWideChar(CP_UTF8, 0, l, -1, w, MAX_PATH)) continue;
        if (wcschr(w, L':')) s += pak_fingerprint(w);
        else { swprintf(p, MAX_PATH * 2, L"%ls\\Mods\\%ls", game, w); s += pak_fingerprint(p); }
    }
    if (f) fclose(f);
    return s + (order | 1);
}
/* Moves every from\*.bin into to (or deletes them if to is NULL). Returns how many were moved. */
static int move_registry_files(const wchar_t *from, const wchar_t *to) {
    wchar_t pat[MAX_PATH * 2], src[MAX_PATH * 2], dst[MAX_PATH * 2]; WIN32_FIND_DATAW fd; int n = 0;
    swprintf(pat, MAX_PATH * 2, L"%ls\\*.bin", from);
    HANDLE h = FindFirstFileW(pat, &fd); if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        swprintf(src, MAX_PATH * 2, L"%ls\\%ls", from, fd.cFileName);
        if (!to) { DeleteFileW(src); continue; }
        swprintf(dst, MAX_PATH * 2, L"%ls\\%ls", to, fd.cFileName);
        n += !!MoveFileExW(src, dst, MOVEFILE_REPLACE_EXISTING);
    } while (FindNextFileW(h, &fd));
    FindClose(h); return n;
}
/* Reads "<fingerprint hex> <server>" (server optional, older versions wrote only the fingerprint). */
static unsigned long long read_set_label(const wchar_t *path, wchar_t *server, int n) {
    unsigned long long fp = 0; char s[64] = ""; *server = 0;
    FILE *f = _wfopen(path, L"rb"); if (!f) return 0;
    if (fscanf(f, "%llx %63s", &fp, s) < 1) fp = 0;
    fclose(f);
    MultiByteToWideChar(CP_UTF8, 0, s, -1, server, n);
    return fp;
}
static void write_set_label(const wchar_t *path, unsigned long long fp, const wchar_t *server) {
    FILE *f = _wfopen(path, L"wb"); if (f) { fprintf(f, "%llx %ls", fp, server); fclose(f); }
}
// The game reads dir\*.bin. <store>.txt labels them with their fingerprint and server. When another
// server is launched, they're parked in <store>\<server>\ (with their label in label.txt), and that
// server's parked set comes back if its fingerprint still matches. The same
// server with changed mods starts fresh, so its old set is overwritten. MOD_LISTS_KEPT - 1 parked
// sets are kept. Returns 0 = kept as is, 1 = parked set restored, 2 = fresh cache.
static int swap_registry_cache(const wchar_t *dir, const wchar_t *store, unsigned long long fp, const wchar_t *server) {
    wchar_t label[MAX_PATH * 2], sets[MAX_PATH * 2], slot[MAX_PATH * 2], slotLabel[MAX_PATH * 2], oldServer[64], parkedServer[64];
    CreateDirectoryW(dir, NULL);
    swprintf(label, MAX_PATH * 2, L"%ls.txt", store);
    unsigned long long old = read_set_label(label, oldServer, 64);
    if (old == fp) { write_set_label(label, fp, server); return 0; }   // same mods (maybe another server with the same list): still valid
    wcscpy(sets, store); CreateDirectoryW(sets, NULL);
    if (old && *oldServer && wcscmp(oldServer, server)) {   // park the previous server's set
        swprintf(slot, MAX_PATH * 2, L"%ls\\%ls", sets, oldServer); CreateDirectoryW(slot, NULL);
        move_registry_files(slot, NULL); move_registry_files(dir, slot);
        swprintf(slotLabel, MAX_PATH * 2, L"%ls\\label.txt", slot); write_set_label(slotLabel, old, oldServer);
    } else move_registry_files(dir, NULL);   // same server with changed mods, or unlabeled: start fresh
    swprintf(slot, MAX_PATH * 2, L"%ls\\%ls", sets, server);
    swprintf(slotLabel, MAX_PATH * 2, L"%ls\\label.txt", slot);
    int restored = read_set_label(slotLabel, parkedServer, 64) == fp && move_registry_files(slot, dir) > 0;
    move_registry_files(slot, NULL); DeleteFileW(slotLabel); RemoveDirectoryW(slot);   // stale or restored: the slot is emptied
    keep_newest(sets, L"*", MOD_LISTS_KEPT - 1);
    write_set_label(label, fp, server);
    return restored ? 1 : 2;
}
#define REGCACHE_RVA 0x213AD73
static const unsigned char REGCACHE_SIG[] = {0x83,0x78,0x50,0x00, 0x0f,0x8c,0xe1,0x03,0x00,0x00};   // cmp [rax+50h],0; jl skip
/* Swaps in this mod list's asset scan cache, then lets the gatherer store cooked packages. */
int install_registry_cache(void) {
    unsigned char *p = (unsigned char *)GetModuleHandleW(NULL) + REGCACHE_RVA;
    if (memcmp(p, REGCACHE_SIG, sizeof REGCACHE_SIG)) { log_line("asset registry cache: game build differs - left off.\n"); return 0; }
    wchar_t game[MAX_PATH], dir[MAX_PATH * 2];
    if (!GetModuleFileNameW(NULL, game, MAX_PATH)) return 0;
    for (int up = 0; up < 3; up++) { wchar_t *sl = wcsrchr(game, L'\\'); if (sl) *sl = 0; }   // ...\ConanSandbox
    unsigned long long fp = all_paks_fingerprint(game);
    if (!fp) { log_line("asset registry cache: no paks found - left off.\n"); return 0; }
    swprintf(dir, MAX_PATH * 2, L"%ls\\Intermediate\\AssetRegistryCache", game);
    wchar_t store[MAX_PATH * 2]; swprintf(store, MAX_PATH * 2, L"%ls\\asset-cache", data_dir);   // parked sets; label in asset-cache.txt
    int swapped = swap_registry_cache(dir, store, fp, server_slot);
    if (swapped == 1) log_line("asset registry cache: saved cache for %ls restored\n", server_slot);
    if (swapped == 2) log_line("asset registry cache: new server or its mods changed - starting a fresh cache for %ls\n", server_slot);
    static const unsigned char nop6[] = {0x66,0x0f,0x1f,0x44,0x00,0x00};
    DWORD old;
    if (!VirtualProtect(p + 4, 6, PAGE_EXECUTE_READWRITE, &old)) return 0;
    memcpy(p + 4, nop6, 6);
    VirtualProtect(p + 4, 6, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 10);
    log_line("asset registry cache: cooked packages are cached\n");
    return 1;
}
