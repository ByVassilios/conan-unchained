// Self-test. Builds every source file into one program so the tests can reach each fix's internals,
// and swaps the game's functions for fakes (fake_*), so each fix runs against known inputs.
// Build and run with test.bat.
#include "../src/main.c"
#include "../src/util.c"
#include "../src/checksums.c"
#include "../src/small_patches.c"
#include "../src/pak_skip.c"
#include "../src/asset_compare.c"
#include "../src/join_fix.c"
#include "../src/building_items.c"
#include "../src/folder_lists.c"
#include "../src/registry_cache.c"
#include "../src/load_log.c"
#include "../src/menu_status.c"
#include <assert.h>
static int fake_hash_calls;
static FMD5Hash *fake_hash_file(FMD5Hash *out, const wchar_t *name, void *b) { (void)name; (void)b; fake_hash_calls++; out->valid = 1; memset(out->bytes, fake_hash_calls, 16); return out; }
static int real_md5_calls;   // a "game" hash that computes the real MD5
static FMD5Hash *real_md5_fake(FMD5Hash *out, const wchar_t *name, void *b) { (void)b; real_md5_calls++; md5_file(name, out); return out; }
// Fake FindFilesAtPath: counts files/immediate subfolders under path (case-insensitive) into set Num.
// Worst case for the "folder above the mount point" branch: it returns nothing at all.
static const wchar_t **fakeFiles; static int fakeN;
static void fake_find(void *pak, void *outSet, const wchar_t *path, char f, char d, char r) {
    (void)f; (void)d; (void)r;
    wchar_t dir[512]; wcscpy(dir, path); size_t n = wcslen(dir);
    if (n && dir[n - 1] != L'/') { dir[n++] = L'/'; dir[n] = 0; }
    const wchar_t *m = *(wchar_t **)((char *)pak + PAK_MOUNT);
    if (_wcsnicmp(dir, m, wcslen(m))) return;   // folder not at/under the mount point
    for (int i = 0; i < fakeN; i++) if (!_wcsnicmp(fakeFiles[i], dir, n)) (*(int *)((char *)outSet + SET_NUM))++;
}
// Fakes for the asset compare: the "original" normalizes like SetPath would for these test inputs
// (short names never match here) and can be told to disagree.
static int origCalls, fakeLie;
static char fake_streq(const FString *a, const FString *b) { return !_wcsicmp(a->num > 1 ? a->data : L"", b->num > 1 ? b->data : L""); }
static char fake_ident(void *p, const void *a, const void *b, unsigned f) {
    (void)p; (void)f; origCalls++;
    const FString *x = asset_path(a), *y = asset_path(b);
    if (!path_stable(x) || !path_stable(y)) return 0;
    return fakeLie ? !fake_streq(x, y) : fake_streq(x, y);
}
// Fake item table for the building item shortcut: row = {weak ptr 8, tag 4, pad 4, FStr path 16}.
typedef struct { void *weak; int tag, pad; FString path; } FakeRow;
static FakeRow fakeRows[4]; static int fakeTag = 7, resolves; static FString fakeClsPath; static char loadedObj;
static void *fake_resolve(const FString *p) { resolves++; return p->data && !_wcsicmp(p->data, L"/Game/B/Wall.Wall_C") ? &loadedObj : NULL; }
static char fake_has_item(void *self, void *cls) {   // the game's loop, calling resolve through the call site
    (void)self; (void)cls;
    for (int i = 0; i < 4; i++) {
        FakeRow *r = &fakeRows[i];
        if (!r->weak && r->tag != fakeTag) { r->weak = resolve_filter(&r->path); r->tag = fakeTag; }
        if (r->weak && fake_streq(&r->path, &fakeClsPath)) return 1;
    }
    return 0;
}
static void *fake_find_item(void *self, void *cls, int *id) {   // same loop, returns the row and its id
    (void)self; (void)cls; *id = -1;
    for (int i = 0; i < 4; i++) {
        FakeRow *r = &fakeRows[i];
        if (!r->weak && r->tag != fakeTag) { r->weak = resolve_filter(&r->path); r->tag = fakeTag; }
        if (r->weak && fake_streq(&r->path, &fakeClsPath)) { *id = 100 + i; return r; }
    }
    return NULL;
}
static FString *fake_ref(FString *out, void *obj) { (void)obj; *out = fakeClsPath; out->data = _wcsdup(fakeClsPath.data); return out; }
// Fakes for the mod file list cache: the game's listing and its array helpers.
static int fakeListCalls; static const wchar_t *fakeListing[3] = {L"../../../ConanSandbox/Content/Mods/X/A.uasset", L"../../../ConanSandbox/Content/Mods/X/B/MC.uasset", NULL};
static void fake_arr_resize(FStringArray *a, int n) { a->data = realloc(a->data, n * sizeof *a->data); a->max = n; }
static void fake_arr_grow(FStringArray *a, int oldNum) { (void)oldNum; a->max = a->num * 2 + 4; a->data = realloc(a->data, a->max * sizeof *a->data); }
static void fake_str_resize(FString *s, int n, int old) { (void)old; s->data = realloc(s->data, n * 2); s->max = n; }
static void fake_list(void *self, const FStringArray *paths, char force, FStringArray *out) {
    (void)self; (void)paths; (void)force; fakeListCalls++;
    for (int i = 0; fakeListing[i]; i++) {
        int idx = out->num++; if (out->num > out->max) fake_arr_grow(out, idx);
        FString *s = &out->data[idx]; int len = wcslen(fakeListing[i]) + 1; s->data = NULL; s->num = len; fake_str_resize(s, len, 0);
        memcpy(s->data, fakeListing[i], len * 2);
    }
}
static void free_arr(FStringArray *a) { for (int i = 0; i < a->num; i++) free(a->data[i].data); free(a->data); *a = (FStringArray){0}; }
static void reload_folder_lists(void) { clear_folder_lists(); folder_lists_loaded = 0; folder_list_hits = 0; }
static void fake_viewpoint(void *pc, float *loc, float *rot) { (void)pc; (void)rot; loc[0] = 241838; loc[1] = -347374; loc[2] = -8876; }
static char fake_wait(void *pc, const wchar_t *label) {   // the game's step machine: 0 -> 1 -> 2 -> Done (reset)
    (void)label; int *s = (int *)((char *)pc + PC_WAITSTEP);
    if (*s == 2) { *s = 0; return 1; }
    ++*s; return 0;
}
static void check_same(unsigned char *pak, const wchar_t *const *q, size_t nq) {
    for (int round = 0; round < 2; round++)
        for (size_t i = 0; i < nq; i++) {
            unsigned char set1[0x50] = {0}, set2[0x50] = {0};
            fake_find(pak, set1, q[i], 1, 1, 0);
            find_files_hook(pak, set2, q[i], 1, 1, 0);
            assert(*(int *)(set1 + SET_NUM) == *(int *)(set2 + SET_NUM));
        }
}
int main(void) {
    wcscpy(checksum_path, L"test_cache.txt"); wcscpy(log_path, L"test.log"); _wremove(checksum_path);
    FILE *f = fopen("test_file.pak", "wb"); fputs("abc", f); fclose(f);
    original_hash_file = fake_hash_file; FMD5Hash a, b;
    hash_file_hook(&a, L"test_file.pak", NULL); hash_file_hook(&b, L"test_file.pak", NULL);
    assert(fake_hash_calls == 1 && !memcmp(&a, &b, sizeof a));            // second call served from cache
    checksum_count = 0; checksums_loaded = 0; hash_file_hook(&b, L"test_file.pak", NULL);       // reload from disk
    assert(fake_hash_calls == 1 && !memcmp(&a, &b, sizeof a));
    Sleep(20); f = fopen("test_file.pak", "wb"); fputs("abcd", f); fclose(f);
    hash_file_hook(&b, L"test_file.pak", NULL);
    assert(fake_hash_calls == 2 && b.bytes[0] == 2);                          // changed file re-hashed
    remove("test_file.pak"); _wremove(checksum_path); _wremove(log_path);

    // Parallel pre-hashing: Windows MD5 gives the standard value; a prehashed file is used without the
    // game hashing it, after one small file was checked against the game; a mismatch turns it off.
    { static const unsigned char abc[16] = {0x90,0x01,0x50,0x98,0x3c,0xd2,0x4f,0xb0,0xd6,0x96,0x3f,0x7d,0x28,0xe1,0x7f,0x72};
      const wchar_t *names[3] = {L"test_pre1.pak", L"test_pre2.pak", L"test_pre3.pak"};
      for (int i = 0; i < 3; i++) { FILE *pf = _wfopen(names[i], L"wb"); fputs("abc", pf); fclose(pf); }
      FMD5Hash m; assert(md5_file(names[0], &m) && m.valid == 1 && !memcmp(m.bytes, abc, 16));
      checksum_count = 0; checksums_loaded = 1;
      prehash_jobs = calloc(3, sizeof *prehash_jobs); prehash_count = 3; prehash_next = 0;
      for (int i = 0; i < 3; i++) {
          WIN32_FILE_ATTRIBUTE_DATA fa; GetFileAttributesExW(names[i], GetFileExInfoStandard, &fa);
          normalize_path(names[i], prehash_jobs[i].path); prehash_jobs[i].size = fa.nFileSizeLow;
          prehash_jobs[i].mtime = ((unsigned long long)fa.ftLastWriteTime.dwHighDateTime << 32) | fa.ftLastWriteTime.dwLowDateTime;
      }
      prehash_worker(NULL); assert(prehash_jobs[2].state == 2);
      original_hash_file = real_md5_fake; real_md5_calls = 0;
      hash_file_hook(&m, names[0], NULL);          // first small one: checked against the "game"
      assert(real_md5_calls == 1 && prehash_verified && !prehash_disabled && !memcmp(m.bytes, abc, 16));
      hash_file_hook(&m, names[1], NULL);          // then used as is
      assert(real_md5_calls == 1 && !memcmp(m.bytes, abc, 16) && find_checksum(names[1]));
      prehash_verified = 0; original_hash_file = fake_hash_file;   // a "game" that disagrees
      hash_file_hook(&m, names[2], NULL);
      assert(prehash_disabled && m.bytes[0] != 0x90);   // the game's own value wins
      for (int i = 0; i < 3; i++) _wremove(names[i]);
      _wremove(checksum_path); _wremove(log_path); prehash_disabled = 0; prehash_count = 0; }

    // Pak search skip: hooked results must equal the original for every query.
    static const wchar_t *files[] = {L"../../../conansandbox/content/a/b/x.uasset", L"../../../conansandbox/content/a/y.uasset",
                                     L"../../../conansandbox/content/c/d/e/z.uasset"};
    unsigned char pak[0x200] = {0}; const wchar_t *mount = L"../../../";
    memcpy(pak + PAK_MOUNT, &mount, sizeof mount); *(int *)(pak + PAK_INDEXNUM) = 3;
    original_find_files = fake_find; assert(scope_enter() && scope_enter() && in_scope()); scope_exit(); assert(in_scope());   // nested scan scopes
    static const wchar_t *q[] = {L"../../../ConanSandbox/Content/", L"../../../ConanSandbox/Content/Mods/", L"../../../ConanSandbox/Content/Mods/X/",
        L"../../../ConanSandbox/Content/Mods/X/Deep/", L"../../../ConanSandbox/Content/mods/x/deep/er", L"../../../ConanSandbox/Content/A/",
        L"../../../ConanSandbox/Content/A/B", L"../../../ConanSandbox/Content/A/Q/", L"../../../ConanSandbox/Content/A/Q/R/",
        L"../../../ConanSandbox/Content/C/D/E/", L"../../../ConanSandbox/Content/C/D/E/F/", L"../../../ConanSandbox/Content/Mods/X/Deep/"};
    fakeFiles = files; fakeN = 3;
    check_same(pak, q, sizeof q / sizeof q[0]);
    assert(pak_search_skipped >= 6);   // Mods/X/Deep/..., A/Q/R/, C/D/E/F/ second time round etc.

    // Mod pak mounted deep: searching a parent of its mount point returns nothing, yet folders below
    // the mount point have files. (This broke joining: mod content was skipped.)
    static const wchar_t *modFiles[] = {L"../../../conansandbox/content/mods/lush/maps/a.umap"};
    unsigned char modPak[0x200] = {0}; const wchar_t *modMount = L"../../../ConanSandbox/Content/Mods/Lush/";
    memcpy(modPak + PAK_MOUNT, &modMount, sizeof modMount); *(int *)(modPak + PAK_INDEXNUM) = 1;
    static const wchar_t *mq[] = {L"../../../ConanSandbox/Content/", L"../../../ConanSandbox/Content/Mods/",
        L"../../../ConanSandbox/Content/Mods/Lush/", L"../../../ConanSandbox/Content/Mods/Lush/Maps/",
        L"../../../ConanSandbox/Content/Mods/Other/", L"../../../ConanSandbox/Content/Mods/Other/Deep/"};
    fakeFiles = modFiles; fakeN = 1;
    check_same(modPak, mq, sizeof mq / sizeof mq[0]);
    fakeFiles = files; fakeN = 3;
    *(int *)(pak + PAK_INDEXNUM) = 4;   // "different pak" at the same address: nothing may be skipped
    LONG64 s0 = pak_search_skipped; unsigned char set3[0x50] = {0};
    find_files_hook(pak, set3, L"../../../ConanSandbox/Content/Mods/X/Deep/", 1, 1, 0);
    assert(pak_search_skipped == s0);
    // Outside the mod controller search (e.g. a rejoin) nothing is skipped, and the cache starts empty.
    *(int *)(pak + PAK_INDEXNUM) = 3; check_same(pak, q, sizeof q / sizeof q[0]);
    scope_exit(); assert(!in_scope() && empty_set_count == 0); s0 = pak_search_skipped;   // last scope ends: cache wiped
    check_same(pak, q, sizeof q / sizeof q[0]);
    assert(pak_search_skipped == s0 && empty_set_count == 0);

    // Asset compare shortcut: '/'-paths and empty compare case-insensitively; anything else goes to the
    // game's original; a disagreement with the original switches the shortcut off.
    game_path_equals = fake_streq; original_asset_identical = fake_ident;
    unsigned char va[0x20] = {0}, vb[0x20] = {0};
    FString *sa = (FString *)(va + ASSETPTR_PATH), *sb = (FString *)(vb + ASSETPTR_PATH);
    wchar_t p1[] = L"/Game/Mods/X/Item.Item", p2[] = L"/game/mods/x/item.item", p3[] = L"Item";
    *sa = (FString){p1, 23, 23}; *sb = (FString){p2, 23, 23};
    assert(asset_identical_hook(NULL, va, vb, 0) == 1 && origCalls == 1);   // first 2000 shortcuts are verified
    *sb = (FString){p3, 5, 5};
    assert(asset_identical_hook(NULL, va, vb, 0) == 0 && origCalls == 2);   // short name: game's original
    assert(asset_identical_hook(NULL, NULL, NULL, 0) == 1);                  // both empty
    *sb = (FString){NULL, 0, 0};
    assert(asset_identical_hook(NULL, va, NULL, 0) == 0 && !asset_shortcut_disabled);
    fakeLie = 1;                                                         // original disagrees -> off
    *sb = (FString){p2, 23, 23};
    assert(asset_identical_hook(NULL, va, vb, 0) == 0 && asset_shortcut_disabled);

    // Stuck join fix: the teleport wait finishing must not reset the game wait's step.
    original_wait = fake_wait;
    static unsigned char pc[0x1600]; int *st = (int *)(pc + PC_WAITSTEP);
    wait_hook(pc, L"game"); wait_hook(pc, L"game");                 // game wait at step 2
    assert(*st == 2);
    for (int i = 0; i < 3; i++) wait_hook(pc, L"teleport");        // teleport runs 0,1,2 -> Done
    assert(*st == 2 && teleport_step == 0);
    assert(wait_hook(pc, L"game") == 1 && *st == 0);               // game wait still completes
    // Teleport dropped the player 3473 units from the join target (stuck2): target follows the player.
    static void *vt[VT_VIEWPOINT / 8 + 1]; vt[VT_VIEWPOINT / 8] = (void *)fake_viewpoint; *(void ***)pc = vt;
    float *tg = (float *)(pc + PC_JOINTARGET); tg[0] = 240866; tg[1] = -344040;
    pc[PC_TRANSITION] = 2; teleport_controller = NULL;
    for (int i = 0; i < 3; i++) wait_hook(pc, L"teleport");
    assert(tg[0] == 241838 && tg[1] == -347374);
    tg[0] = 241000; tg[1] = -347000; pc[PC_TRANSITION] = 2;        // already close: left alone
    for (int i = 0; i < 3; i++) wait_hook(pc, L"teleport");
    assert(tg[0] == 241000 && tg[1] == -347000);

    // Building item shortcut: same answer as the game, only the matching row is looked up, and the
    // skipped rows' cache bytes are untouched afterwards.
    asset_shortcut_disabled = 0; game_path_equals = fake_streq; original_resolve = fake_resolve; original_has_item = fake_has_item;
    game_path_of_object = fake_ref; game_free = free; item_main_thread = GetCurrentThreadId();
    wchar_t r0[] = L"/Game/A/Sword.Sword_C", r1[] = L"/Game/B/Floor.Floor_C", r2[] = L"/game/b/wall.wall_c", r3[] = L"/Game/B/Wall.Wall_C";
    fakeRows[0].path = (FString){r0, 22, 22}; fakeRows[1].path = (FString){r1, 22, 22};
    fakeRows[2].path = (FString){r2, 20, 20}; fakeRows[3].path = (FString){r3, 20, 20};
    fakeClsPath = (FString){r3, 20, 20};
    FakeRow before[4]; memcpy(before, fakeRows, sizeof before);
    item_shortcut_disabled = 0; item_lookups = 100;   // past the verified calls
    assert(has_item_hook(NULL, &loadedObj) == 1);
    assert(resolves == 1);                                                   // only row 2 (same path, other case)
    assert(!memcmp(&fakeRows[0], &before[0], sizeof(FakeRow) * 2));          // rows 0-1 untouched
    memcpy(fakeRows, before, sizeof before); item_shortcut_disabled = 0;
    char game = fake_has_item(NULL, NULL);                                   // the unmodified answer
    memcpy(fakeRows, before, sizeof before); item_lookups = 0; resolves = 0;
    assert(has_item_hook(NULL, &loadedObj) == game && !item_shortcut_disabled);          // verified call agrees
    fakeClsPath = (FString){r0, 22, 22}; memcpy(fakeRows, before, sizeof before);
    assert(has_item_hook(NULL, &loadedObj) == 0 && !item_shortcut_disabled);             // not loaded -> no match
    original_find_item = fake_find_item; fakeClsPath = (FString){r3, 20, 20};
    memcpy(fakeRows, before, sizeof before); resolves = 0; item_lookups = 100; int id = 0;
    assert(find_item_hook(NULL, &loadedObj, &id) == &fakeRows[2] && id == 102 && resolves == 1);
    assert(!memcmp(&fakeRows[0], &before[0], sizeof(FakeRow) * 2));
    memcpy(fakeRows, before, sizeof before); item_lookups = 0; id = 0;
    assert(find_item_hook(NULL, &loadedObj, &id) == &fakeRows[2] && id == 102 && !item_shortcut_disabled);   // verified call agrees

    // Mod file list cache: miss saves, a reload uses the file, hit #1 is verified, later hits come from
    // the cache, a changed mod pak or a different listing turns it off.
    original_list_files = fake_list; game_array_reserve = fake_arr_resize; game_array_grow = fake_arr_grow; game_string_resize = fake_str_resize;
    allowed_caller_count = 0; folder_lists_thread = GetCurrentThreadId();
    GetTempPathW(MAX_PATH, folder_lists_dir); wcscat(folder_lists_dir, L"unchained-mfl-test"); CreateDirectoryW(folder_lists_dir, NULL);
    keep_newest(folder_lists_dir, L"*", 0);   // empty it
    checksum_count = 0; FMD5Hash hh = {0}; store_checksum(L"C:/mods/X.pak", 10, 20, &hh); find_checksum(L"C:/mods/X.pak")->seen = 1;
    wchar_t pth[] = L"/Game/Mods/X/"; FString pstr = {pth, 14, 14}; FStringArray paths = {&pstr, 1, 1}, out = {0};
    list_files_hook(NULL, &paths, 1, &out);                       // miss: game lists, saved
    assert(fakeListCalls == 1 && out.num == 2 && folder_list_count == 1); free_arr(&out);
    reload_folder_lists(); list_files_hook(NULL, &paths, 1, &out);             // hit #1 from file: verified against the game
    assert(fakeListCalls == 2 && out.num == 2 && !folder_lists_disabled && folder_list_count == 1); free_arr(&out);
    list_files_hook(NULL, &paths, 1, &out);                       // hit #2: from the cache, same list
    assert(fakeListCalls == 2 && out.num == 2 && !wcscmp(out.data[1].data, fakeListing[1]) && out.data[1].num == (int)wcslen(fakeListing[1]) + 1);
    free_arr(&out);
    find_checksum(L"C:/mods/X.pak")->mtime = 21; reload_folder_lists();                // mod updated: saved lists don't apply
    list_files_hook(NULL, &paths, 1, &out);
    assert(fakeListCalls == 3 && folder_list_count == 1); free_arr(&out);   // listed again and re-saved
    reload_folder_lists(); fakeListing[1] = L"../../../ConanSandbox/Content/Mods/X/C.uasset";   // listing differs from saved
    list_files_hook(NULL, &paths, 1, &out);
    assert(folder_lists_disabled && folder_list_count == 0 && GetFileAttributesW(folder_lists_path) == INVALID_FILE_ATTRIBUTES && out.num == 2);
    free_arr(&out); folder_lists_disabled = 0;

    // Each server gets its own folder list file; switching back finds the old one, and a server whose
    // mods changed overwrites its own file.
    { wcscpy(server_slot, L"A"); reload_folder_lists();
      list_files_hook(NULL, &paths, 1, &out); free_arr(&out);                 // server A saved
      wchar_t pa[MAX_PATH]; wcscpy(pa, folder_lists_path); assert(wcsstr(pa, L"modfiles-A.bin"));
      wcscpy(server_slot, L"B"); find_checksum(L"C:/mods/X.pak")->mtime = 22; reload_folder_lists();   // server B, other mods
      list_files_hook(NULL, &paths, 1, &out); free_arr(&out);
      assert(wcscmp(pa, folder_lists_path) && GetFileAttributesW(pa) != INVALID_FILE_ATTRIBUTES);
      wcscpy(server_slot, L"A"); find_checksum(L"C:/mods/X.pak")->mtime = 21; reload_folder_lists(); int calls = fakeListCalls;   // back to A
      list_files_hook(NULL, &paths, 1, &out); free_arr(&out);
      assert(folder_list_count == 1 && fakeListCalls == calls + 1 && !folder_lists_disabled);   // hit #1 (verified, matched)
      find_checksum(L"C:/mods/X.pak")->mtime = 23; reload_folder_lists(); calls = fakeListCalls;   // A's mods updated
      list_files_hook(NULL, &paths, 1, &out); free_arr(&out);
      reload_folder_lists(); list_files_hook(NULL, &paths, 1, &out); free_arr(&out);   // A's file now holds the new set
      assert(!wcscmp(pa, folder_lists_path) && folder_list_count == 1 && fakeListCalls == calls + 2); }

    // keep_newest: keeps the N most recently modified, deletes the rest (files and folders).
    { wchar_t d[MAX_PATH], b[MAX_PATH]; GetTempPathW(MAX_PATH, d); wcscat(d, L"unchained-keep-test"); CreateDirectoryW(d, NULL); keep_newest(d, L"*", 0);
      for (int i = 0; i < 3; i++) {
          swprintf(b, MAX_PATH, L"%ls\\%d", d, i); CreateDirectoryW(b, NULL);
          HANDLE h = CreateFileW(b, FILE_WRITE_ATTRIBUTES, 0, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
          FILETIME t = {i * 1000, 30000000}; SetFileTime(h, NULL, NULL, &t); CloseHandle(h);
          swprintf(b, MAX_PATH, L"%ls\\%d\\x.bin", d, i); FILE *f = _wfopen(b, L"wb"); fputs("x", f); fclose(f);
          swprintf(b, MAX_PATH, L"%ls\\%d", d, i); h = CreateFileW(b, FILE_WRITE_ATTRIBUTES, 0, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
          SetFileTime(h, NULL, NULL, &t); CloseHandle(h);   // the file write bumped it
      }
      keep_newest(d, L"*", 2);
      swprintf(b, MAX_PATH, L"%ls\\0", d); assert(GetFileAttributesW(b) == INVALID_FILE_ATTRIBUTES);
      swprintf(b, MAX_PATH, L"%ls\\2\\x.bin", d); assert(GetFileAttributesW(b) != INVALID_FILE_ATTRIBUTES); }

    // Asset registry cache: unlabeled files are cleared, same mods keep, another server parks the old
    // set and switching back restores it, a server whose mods changed starts fresh.
    { wchar_t d[MAX_PATH], b[MAX_PATH], s[MAX_PATH]; char c[4] = {0}; GetTempPathW(MAX_PATH, d); wcscat(d, L"unchained-arc-test"); CreateDirectoryW(d, NULL);
      swprintf(s, MAX_PATH, L"%ls\\store", d); keep_newest(s, L"*", 0);
      swprintf(b, MAX_PATH, L"%ls.txt", s); FILE *f = _wfopen(b, L"wb"); fputs("99", f); fclose(f);   // label without a server
      assert(swap_registry_cache(d, s, 0x99, L"A") == 0);                       // same fingerprint: kept, now labeled A
      swprintf(b, MAX_PATH, L"%ls\\x.bin", d); f = _wfopen(b, L"wb"); fputs("?", f); fclose(f);
      assert(swap_registry_cache(d, s, 0x123, L"A") == 2 && GetFileAttributesW(b) == INVALID_FILE_ATTRIBUTES);   // A's mods changed
      f = _wfopen(b, L"wb"); fputs("A", f); fclose(f);
      assert(swap_registry_cache(d, s, 0x123, L"A") == 0 && GetFileAttributesW(b) != INVALID_FILE_ATTRIBUTES);
      assert(swap_registry_cache(d, s, 0x124, L"B") == 2 && GetFileAttributesW(b) == INVALID_FILE_ATTRIBUTES);   // A parked
      f = _wfopen(b, L"wb"); fputs("B", f); fclose(f);
      assert(swap_registry_cache(d, s, 0x123, L"A") == 1);                      // A restored, B parked
      f = _wfopen(b, L"rb"); fread(c, 1, 1, f); fclose(f); assert(c[0] == 'A');
      swprintf(b, MAX_PATH, L"%ls\\B\\x.bin", s); assert(GetFileAttributesW(b) != INVALID_FILE_ATTRIBUTES);
      assert(swap_registry_cache(d, s, 0x125, L"B") == 2);                      // B's mods changed while away: parked set dropped
      assert(GetFileAttributesW(b) == INVALID_FILE_ATTRIBUTES);
      swprintf(b, MAX_PATH, L"%ls\\A\\x.bin", s); assert(GetFileAttributesW(b) != INVALID_FILE_ATTRIBUTES); }

    // Server slot name from Game.ini.
    { wchar_t ini[MAX_PATH], slot[64]; GetTempPathW(MAX_PATH, ini); wcscat(ini, L"unchained-game-test.ini");
      FILE *f = _wfopen(ini, L"wb"); fputs("[x]\r\nStartedListenServerSession=False\r\nLastConnected=217.182.174.204:7777\r\n", f); fclose(f);
      read_server_slot(ini, slot, 64); assert(!wcscmp(slot, L"217.182.174.204_7777"));
      f = _wfopen(ini, L"wb"); fputs("StartedListenServerSession=True\r\nLastConnected=1.2.3.4:7777\r\n", f); fclose(f);
      read_server_slot(ini, slot, 64); assert(!wcscmp(slot, L"singleplayer"));
      DeleteFileW(ini); read_server_slot(ini, slot, 64); assert(!wcscmp(slot, L"unknown")); }

    // Menu status: only the revision string is rewritten; states and the buffer limit hold.
    { static void *seen; static wchar_t got[4096];
      void *fake_s2t(void *o, const FString *in, void *x, void *y) { (void)o; (void)x; (void)y; seen = (void *)in; wcscpy(got, in->data); return o; }
      original_str_to_text = fake_s2t;
      // Fake name table: index 7 = W_IngameMenu_C, index 9 = W_MainMenu_MainMenu_C (same bucket, chained).
      static void *heads[65536]; static char e1[64], e2[64]; name_heads = heads; heads[123] = e1;
      *(int *)e1 = 7 << 1; strcpy(e1 + 0x10, "W_IngameMenu_C"); *(char **)(e1 + 8) = e2;
      *(int *)e2 = 9 << 1; strcpy(e2 + 0x10, "W_MainMenu_MainMenu_C");
      static char mainCls[0x20], gameCls[0x20], mainObj[0x20], gameObj[0x20], mainFrame[0x20], gameFrame[0x20];
      *(int *)(mainCls + 0x18) = 9; *(int *)(gameCls + 0x18) = 7;
      *(char **)(mainObj + 0x10) = mainCls; *(char **)(gameObj + 0x10) = gameCls;
      *(char **)(mainFrame + 0x18) = mainObj; *(char **)(gameFrame + 0x18) = gameObj;
      wchar_t other[] = L"Revision"; FString o = {other, 9, 9}; str_to_text_hook(NULL, &o, NULL, mainFrame);
      assert(seen == &o);                                                        // other strings pass straight through
      for (int i = 0; i < FIX_COUNT; i++) fixes[i].state = 1;
      fixes[0].state = 0; fixes[1].state = 2; pak_skip_disabled = 1;
      wchar_t rev[] = L"Revision (#1/2) | Backend live (3)"; FString r = {rev, (int)wcslen(rev) + 1, 0};
      str_to_text_hook(NULL, &r, NULL, gameFrame); assert(seen == &r);          // in-game menu: unchanged
      str_to_text_hook(NULL, &r, NULL, NULL); assert(seen == &r);               // no frame: unchanged
      str_to_text_hook(NULL, &r, NULL, mainFrame); assert(main_menu_name == 9);
      assert(seen != &r && !wcsncmp(got, L"[CONAN UNCHAINED]\n", 18));
      assert(wcsstr(got, L"Pak mount sleeps: Disabled\n") && wcsstr(got, L"Not Responding fix: Failed to load\n"));
      assert(wcsstr(got, L"Pak search skip: Turned off by safety check\n") && wcsstr(got, L"Occlusion hash: Loaded\n"));
      size_t g = wcslen(got), rl = wcslen(rev); assert(g > rl && !wcscmp(got + g - rl, rev) && !wcsncmp(got + g - rl - 2, L"\n\n", 2));   // game text at the bottom, after a blank line
      wchar_t small[40]; assert(menu_text(small, 40, rev) == 39 && wcslen(small) == 39);   // truncates, never overflows
      pak_skip_disabled = 0; }
    puts("ok"); return 0;
}
