// Mod folder file list cache
// When joining, the mod search finds every mod in the game's own ModControllerCache.json, but for each
// mod it still lists the mod's whole folder (0x212D200, called at 0x37D2E50 with force rescan): a
// temporary asset scanner walks every directory, builds a string per file and is thrown away. The list
// is only used to confirm the cached controller files exist, and it's most of the first-join freeze.
// The listing doesn't touch the asset registry (it uses its own scanner), so we can save the result,
// keyed by the folder paths, for as long as every mod .pak and base pak is unchanged.

#include "unchained.h"

typedef void (*ListFiles_t)(void *self, const FStringArray *paths, char force, FStringArray *out);
typedef void (*ArrResize_t)(FStringArray *arr, int newMax);
typedef void (*ArrGrow_t)(FStringArray *arr, int oldNum);
typedef void (*StrResize_t)(FString *s, int newMax, int oldMax);
static ListFiles_t original_list_files; static ArrResize_t game_array_reserve; static ArrGrow_t game_array_grow; static StrResize_t game_string_resize;
typedef struct { wchar_t *key; int n; wchar_t **files; } FolderList;
static FolderList *folder_lists; static int folder_list_count, folder_lists_loaded, folder_lists_disabled; static unsigned long long folder_lists_fingerprint;
static wchar_t folder_lists_path[MAX_PATH], folder_lists_dir[MAX_PATH]; static DWORD folder_lists_thread; static volatile LONG64 folder_list_hits, folder_list_misses;
static void *allowed_callers[3]; static int allowed_caller_count;   // allowed return addresses (the test leaves none = any)
#define LISTFILES_RVA 0x212D200
#define LISTFILES_RET 0x37D2E57   // return address of the call in the mod search's cache-hit branch
#define LISTMODS_RET 0x37D3654    // ... the sync search listing all of /Game/Mods/
#define LISTDLC_RET 0x37D3719     // ... the sync search listing /Game/DLC/ (base game paks, ~13 s)
#define ARRRESIZE_RVA 0x2345BB0
#define ARRGROW_RVA 0x2D0200
#define STRRESIZE_RVA 0x1DFC200
static const unsigned char LISTFILES_SIG[] = {0x40,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x81,0xec,0x10,0x01,0x00,0x00};

/* FNV-1a, continued from h. */
// Fingerprint for the saved folder lists: the mod paks checksummed this launch plus the base game's
// Content/Paks/*.pak (the /Game/DLC/ listing comes from those). 0 = no mods seen, so the cache stays off.
static unsigned long long folder_lists_paks_fingerprint(void) {
    unsigned long long s = checksums_fingerprint();
    if (!s) return 0;
    wchar_t dir[MAX_PATH]; WIN32_FIND_DATAW fd;
    if (!GetModuleFileNameW(NULL, dir, MAX_PATH)) return s;
    for (int up = 0; up < 3; up++) { wchar_t *sl = wcsrchr(dir, L'\\'); if (sl) *sl = 0; }   // ...\Binaries\Win64\x.exe -> ...\ConanSandbox
    wcscat(dir, L"\\Content\\Paks\\*.pak");
    HANDLE h = FindFirstFileW(dir, &fd);
    if (h == INVALID_HANDLE_VALUE) return s;
    do s += file_fingerprint(fd.cFileName, ((unsigned long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow,
                             ((unsigned long long)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime);
    while (FindNextFileW(h, &fd));
    FindClose(h);
    return s;
}
/* Joins the folder paths into one key, "path|path|...". Caller frees it. */
static wchar_t *paths_key(const FStringArray *a) {
    size_t n = 1;
    for (int i = 0; i < a->num; i++) n += (a->data[i].num > 1 ? wcslen(a->data[i].data) : 0) + 1;
    wchar_t *k = calloc(n, sizeof *k);
    for (int i = 0; k && i < a->num; i++) { if (a->data[i].num > 1) wcscat(k, a->data[i].data); wcscat(k, L"|"); }
    return k;
}
/* Finds a saved folder list by key. */
static FolderList *find_folder_list(const wchar_t *key) {
    for (int i = 0; i < folder_list_count; i++) if (!wcscmp(folder_lists[i].key, key)) return &folder_lists[i];
    return NULL;
}
/* Drops every saved folder list from memory. */
static void clear_folder_lists(void) {
    for (int i = 0; i < folder_list_count; i++) { for (int j = 0; j < folder_lists[i].n; j++) free(folder_lists[i].files[j]); free(folder_lists[i].files); free(folder_lists[i].key); }
    free(folder_lists); folder_lists = NULL; folder_list_count = 0;
}
// File: "MFL1", u64 fingerprint, u32 lists; per list: u32 len + key chars, u32 files; per file: u32 len + chars.
static int read_wide_string(FILE *f, wchar_t **out) {
    unsigned len; *out = NULL;
    if (fread(&len, 4, 1, f) != 1 || len > 32768) return 0;
    *out = calloc(len + 1, sizeof **out);
    return *out && fread(*out, 2, len, f) == len;
}
/* Loads this server's modfiles-<server>.bin if its fingerprint matches this launch. Stops at the first damaged entry. */
static void load_folder_lists(void) {
    FILE *f = _wfopen(folder_lists_path, L"rb"); if (!f) return;
    char magic[4]; unsigned long long sig; unsigned n;
    if (fread(magic, 4, 1, f) == 1 && !memcmp(magic, "MFL1", 4) && fread(&sig, 8, 1, f) == 1 && sig == folder_lists_fingerprint && fread(&n, 4, 1, f) == 1) {
        for (unsigned i = 0; i < n; i++) {
            FolderList l = {0}; unsigned nf = 0; int ok = read_wide_string(f, &l.key) && fread(&nf, 4, 1, f) == 1 && nf <= 1000000;
            if (ok) ok = (l.files = calloc(nf ? nf : 1, sizeof *l.files)) != NULL;
            for (unsigned j = 0; ok && j < nf; j++) { ok = read_wide_string(f, &l.files[j]); l.n = j + 1; }
            FolderList *nl = ok ? realloc(folder_lists, (folder_list_count + 1) * sizeof *folder_lists) : NULL;
            if (!nl) {   // damaged file: keep what loaded so far
                for (int j = 0; j < l.n; j++) free(l.files[j]);
                free(l.files); free(l.key); break;
            }
            l.n = nf; folder_lists = nl; folder_lists[folder_list_count++] = l;
        }
    }
    fclose(f);
}
/* Writes all folder lists to a temp file and swaps it in, so a crash never leaves half a file. */
static void save_folder_lists(void) {
    wchar_t tmp[MAX_PATH]; wcscpy(tmp, folder_lists_path); wcscat(tmp, L".tmp");
    FILE *f = _wfopen(tmp, L"wb"); if (!f) return;
    unsigned n = folder_list_count; fwrite("MFL1", 4, 1, f); fwrite(&folder_lists_fingerprint, 8, 1, f); fwrite(&n, 4, 1, f);
    for (int i = 0; i < folder_list_count; i++) {
        unsigned kl = wcslen(folder_lists[i].key), nf = folder_lists[i].n;
        fwrite(&kl, 4, 1, f); fwrite(folder_lists[i].key, 2, kl, f); fwrite(&nf, 4, 1, f);
        for (int j = 0; j < folder_lists[i].n; j++) { unsigned l = wcslen(folder_lists[i].files[j]); fwrite(&l, 4, 1, f); fwrite(folder_lists[i].files[j], 2, l, f); }
    }
    int ok = !ferror(f); fclose(f);
    if (ok) MoveFileExW(tmp, folder_lists_path, MOVEFILE_REPLACE_EXISTING); else DeleteFileW(tmp);
}
// Append the saved names to the game's array exactly like the original does (same game helpers).
static void fill_from_folder_list(FStringArray *out, const FolderList *l) {
    if (l->n > out->max) game_array_reserve(out, l->n);
    for (int j = 0; j < l->n; j++) {
        int idx = out->num++;
        if (out->num > out->max) game_array_grow(out, idx);
        FString *s = &out->data[idx]; int len = wcslen(l->files[j]) + 1;
        s->data = NULL; s->num = len; s->max = 0;
        game_string_resize(s, len, 0); memcpy(s->data, l->files[j], len * 2);
    }
}
/* True if the game's listing and a saved list are identical, in the same order. */
static int same_as_folder_list(const FStringArray *a, const FolderList *l) {
    if (a->num != l->n) return 0;
    for (int j = 0; j < l->n; j++) if (wcscmp(a->data[j].num > 1 ? a->data[j].data : L"", l->files[j])) return 0;
    return 1;
}

/* Replacement for the folder listing (0x212D200). Only answers the three known callers on the main
   thread; everything else goes straight to the game. */
static void list_files_hook(void *self, const FStringArray *paths, char force, FStringArray *out) {
    void *ret = __builtin_return_address(0); int allowed = !allowed_caller_count;
    for (int i = 0; i < allowed_caller_count; i++) allowed |= ret == allowed_callers[i];
    if (folder_lists_disabled || !allowed || GetCurrentThreadId() != folder_lists_thread || !paths || !out || out->num) {
        original_list_files(self, paths, force, out); return;
    }
    if (!folder_lists_loaded) {
        folder_lists_loaded = 1; folder_lists_fingerprint = folder_lists_paks_fingerprint();
        if (!folder_lists_fingerprint) { folder_lists_disabled = 1; log_line("mod file list cache: no mod checksums seen - left off.\n"); original_list_files(self, paths, force, out); return; }
        // One file per server (modfiles-<server>.bin). The fingerprint inside decides if it's
        // still valid; if that server's mods changed, its file is simply overwritten.
        swprintf(folder_lists_path, MAX_PATH, L"%ls\\modfiles-%ls.bin", folder_lists_dir, server_slot);
        load_folder_lists(); log_line("mod file list cache: %d saved lists loaded\n", folder_list_count);
        HANDLE h = CreateFileW(folder_lists_path, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) { FILETIME now; GetSystemTimeAsFileTime(&now); SetFileTime(h, NULL, NULL, &now); CloseHandle(h); }   // mark as used
        keep_newest(folder_lists_dir, L"modfiles-*.bin", MOD_LISTS_KEPT);
    }
    wchar_t *key = paths_key(paths);
    FolderList *l = key ? find_folder_list(key) : NULL;
    if (l) {
        // Only per-mod folder lists are re-checked: re-listing /Game/DLC/ or /Game/Mods/ costs ~10 s each,
        // and those come from the same paks the fingerprint covers.
        int perMod = !allowed_caller_count || ret == allowed_callers[0];
        LONG64 n = perMod ? InterlockedIncrement64(&folder_list_hits) : 2;
        if (n == 1 || n % 32 == 0) {   // safety net: the game's own listing
            original_list_files(self, paths, force, out);
            if (!same_as_folder_list(out, l)) {
                log_line("mod file list cache DISABLED: '%ls' saved %d files, game %d\n", key, l->n, out->num);
                folder_lists_disabled = 1; clear_folder_lists(); DeleteFileW(folder_lists_path);
            }
        } else fill_from_folder_list(out, l);
        free(key); return;
    }
    InterlockedIncrement64(&folder_list_misses);
    original_list_files(self, paths, force, out);
    if (!key) return;
    FolderList nl = {key, out->num, calloc(out->num ? out->num : 1, sizeof(wchar_t *))};
    FolderList *grown = nl.files ? realloc(folder_lists, (folder_list_count + 1) * sizeof *folder_lists) : NULL;
    if (!grown) { free(nl.files); free(key); return; }
    for (int j = 0; j < out->num; j++) nl.files[j] = _wcsdup(out->data[j].num > 1 ? out->data[j].data : L"");
    folder_lists = grown; folder_lists[folder_list_count++] = nl;
    save_folder_lists();
}

/* Hooks the folder listing after checking the function and all three call sites. */
void install_folder_lists(void) {
    unsigned char *base = (unsigned char *)GetModuleHandleW(NULL);
    static const unsigned char call[] = {0x41,0xff,0x92,0xe0,0x00,0x00,0x00};   // call [r10+0xE0] right before each return address
    if (memcmp(base + LISTFILES_RVA, LISTFILES_SIG, sizeof LISTFILES_SIG) || memcmp(base + LISTFILES_RET - 7, call, sizeof call) ||
        memcmp(base + LISTMODS_RET - 7, call, sizeof call) || memcmp(base + LISTDLC_RET - 7, call, sizeof call)) {
        log_line("mod file list cache: game build differs - left off.\n"); return;
    }
    game_array_reserve = (ArrResize_t)(base + ARRRESIZE_RVA); game_array_grow = (ArrGrow_t)(base + ARRGROW_RVA); game_string_resize = (StrResize_t)(base + STRRESIZE_RVA);
    allowed_callers[0] = base + LISTFILES_RET; allowed_callers[1] = base + LISTMODS_RET; allowed_callers[2] = base + LISTDLC_RET; allowed_caller_count = 3;
    folder_lists_thread = GetCurrentThreadId();   // DllMain runs on the game's main thread
    wcscpy(folder_lists_dir, data_dir);
    if ((original_list_files = (ListFiles_t)hook_function(base + LISTFILES_RVA, 19, (void *)list_files_hook))) log_line("mod file list cache installed\n");
}
