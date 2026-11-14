// Building piece item lookup shortcut
// When a building piece class loads, ABuildableBase::LoadCDOItemData asks HasBuildingItem (0x625470)
// whether an item row points at that class. It walks EVERY item table row; for each row whose asset
// isn't loaded it looks the object up by path string (0x167C220, called at 0x625626: FName creation,
// path parsing, StaticFindObject), and only then compares the row's stored path (row+0xB8) with the
// class path (Stricmp 0x2804A30). A row whose stored path differs can never match, so its lookup is
// wasted, and that's most of the FPS dip when a big modded base streams in. Shortcut: skip the lookup for
// rows whose path differs, and afterwards put back the row's cached pointer + tag bytes exactly.

#include "unchained.h"

typedef char (*HasItem_t)(void *self, void *cls);
typedef void *(*FindItem_t)(void *self, void *cls, int *id);
typedef void *(*Resolve_t)(const FString *path);
typedef FString *(*RefFromObj_t)(FString *out, void *obj);
typedef void (*Free_t)(void *p);
static HasItem_t original_has_item; static FindItem_t original_find_item; static Resolve_t original_resolve; static RefFromObj_t game_path_of_object; static Free_t game_free;
static DWORD item_thread, item_main_thread; static FString item_class_path; static int item_shortcut_active;
static struct { unsigned char *row; unsigned char saved[16]; } *skipped_rows; static int skipped_count, skipped_capacity;
static volatile LONG64 item_lookups, item_rows_skipped; static volatile LONG item_shortcut_disabled;
#define HASITEM_RVA 0x625470
#define HASITEM_CALL 0x625626   // call 0x167C220 inside HasBuildingItem
#define FINDITEM_RVA 0x61A2C0
#define FINDITEM_CALL 0x61A4C7  // call 0x167C220 inside FindBuildingItem
#define RESOLVE_RVA 0x167C220
#define REFFROMOBJ_RVA 0x166B8D0
#define FREE_RVA 0x155A960
#define ROW_PTR_OFS 0x10        // resolve() gets &row.Path; the row's FAssetPtr (weak ptr + tag) is 0x10 before
static const short HASITEM_SIG[] = {
    0x48,0x8b,0xc4,0x55,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x8d,0x68,0xa1,0x48,0x81,0xec,0xd0,0x00,0x00,0x00};

/* Sits in front of the by-path lookup inside the item loops. For rows whose path can't match the
   building class, it answers "not loaded" and remembers the row so its bytes can be put back. */
static void *resolve_filter(const FString *path) {
    if (item_shortcut_active && GetCurrentThreadId() == item_thread && !game_path_equals(path, &item_class_path)) {
        if (skipped_count == skipped_capacity) {
            void *n = realloc(skipped_rows, (skipped_capacity ? skipped_capacity * 2 : 1024) * sizeof *skipped_rows);
            if (!n) return original_resolve(path);
            skipped_rows = n; skipped_capacity = skipped_capacity ? skipped_capacity * 2 : 1024;
        }
        skipped_rows[skipped_count].row = (unsigned char *)path - ROW_PTR_OFS;
        memcpy(skipped_rows[skipped_count].saved, skipped_rows[skipped_count].row, 16); skipped_count++;
        return NULL;   // "not loaded": the row is skipped, exactly as its path compare would skip it
    }
    return original_resolve(path);
}

// Shortcut on (begin) for one call of either lookup; 0 = run the game's code unchanged.
static int item_shortcut_begin(void *cls) {
    if (item_shortcut_disabled || item_shortcut_active || !cls || GetCurrentThreadId() != item_main_thread) return 0;   // game thread only
    if (!game_path_of_object(&item_class_path, cls)->data || item_class_path.data[0] != L'/') {   // unusual path: leave it to the game
        if (item_class_path.data) game_free(item_class_path.data);
        item_class_path = (FString){0}; return 0;
    }
    item_thread = GetCurrentThreadId(); skipped_count = 0; item_shortcut_active = 1;
    return 1;
}
// Puts the skipped rows' cache bytes back; returns 1 if this call should be checked against the game.
static int item_shortcut_end(void) {
    item_shortcut_active = 0;
    for (int i = skipped_count - 1; i >= 0; i--) memcpy(skipped_rows[i].row, skipped_rows[i].saved, 16);
    LONG64 n = InterlockedIncrement64(&item_lookups); InterlockedAdd64(&item_rows_skipped, skipped_count);
    if (n % 500 == 0) log_line("building item lookups: %lld, row lookups skipped %lld\n", n, item_rows_skipped);
    return n <= 20 || n % 64 == 0;
}
/* Turns the building item shortcut off after a mismatch and logs it. */
static void item_shortcut_fail(const char *what, long long a, long long b) {
    item_shortcut_disabled = 1; log_line("building item shortcut DISABLED (%s): '%ls' shortcut %lld, game %lld\n", what, item_class_path.data, a, b);
}
/* Frees the class path fetched by hi_begin. */
static void item_shortcut_done(void) { game_free(item_class_path.data); item_class_path = (FString){0}; }

// HasBuildingItem(self, cls) -> bool
static char has_item_hook(void *self, void *cls) {
    if (!item_shortcut_begin(cls)) return original_has_item(self, cls);
    char r = original_has_item(self, cls) != 0;
    if (item_shortcut_end()) { char o = original_has_item(self, cls) != 0; if (o != r) { item_shortcut_fail("has", r, o); r = o; } }
    item_shortcut_done();
    return r;
}
// FindBuildingItem(self, cls, &itemId) -> matching row or NULL (0x61A2C0, the same row loop)
static void *find_item_hook(void *self, void *cls, int *id) {
    if (!item_shortcut_begin(cls)) return original_find_item(self, cls, id);
    void *r = original_find_item(self, cls, id);
    if (item_shortcut_end()) {
        int oid; void *o = original_find_item(self, cls, &oid);
        if (o != r || oid != *id) { item_shortcut_fail("find", (long long)(ULONG_PTR)r ^ *id, (long long)(ULONG_PTR)o ^ oid); r = o; *id = oid; }
    }
    item_shortcut_done();
    return r;
}

static const unsigned char FINDITEM_SIG[] = {
    0x40,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0x6c,0x24,0xd9,0x48,0x81,0xec,0xe0,0x00,0x00,0x00};

/* True if the instruction at `call` is a direct call to the by-path lookup. */
static int calls_resolve(unsigned char *base, unsigned char *call) {
    return call[0] == 0xE8 && call + 5 + *(int *)(call + 1) == base + RESOLVE_RVA;
}
/* Retargets a rel32 call instruction to `cave`. */
static void point_call(unsigned char *call, unsigned char *cave) {
    DWORD old; int rel = (int)(cave - (call + 5));
    VirtualProtect(call + 1, 4, PAGE_EXECUTE_READWRITE, &old); memcpy(call + 1, &rel, 4); VirtualProtect(call + 1, 4, old, &old);
}

/* Hooks HasBuildingItem and FindBuildingItem and routes their lookup calls through resolve_filter. */
void install_building_items(void) {
    unsigned char *base = (unsigned char *)GetModuleHandleW(NULL);
    unsigned char *p = find_signature(HASITEM_SIG, sizeof HASITEM_SIG / sizeof HASITEM_SIG[0], HASITEM_RVA);
    static const unsigned char eq[] = {0x48,0x83,0xec,0x28, 0x83,0x7a,0x08,0x00, 0x48,0x8b,0xc1};
    static const unsigned char res[] = {0x48,0x83,0xec,0x28,0x8b,0x51,0x08,0x85,0xd2};
    static const unsigned char ref[] = {0x48,0x89,0x4c,0x24,0x08,0x53,0x48,0x83,0xec,0x40};
    // Calls game functions at fixed addresses, so only on the exact build it was written for.
    if (p != base + HASITEM_RVA || memcmp(base + STREQ_RVA, eq, sizeof eq) || memcmp(base + RESOLVE_RVA, res, sizeof res) ||
        memcmp(base + REFFROMOBJ_RVA, ref, sizeof ref) || memcmp(base + FINDITEM_RVA, FINDITEM_SIG, sizeof FINDITEM_SIG) ||
        !calls_resolve(base, base + HASITEM_CALL) || !calls_resolve(base, base + FINDITEM_CALL)) {
        log_line("building item shortcut: game build differs - left off.\n"); return;
    }
    // The calls are rel32, so they need a jump stub within 2 GB: 16 bytes of int3 padding inside .text.
    unsigned char *cave = NULL;
    for (unsigned char *q = base + 0x1000; q < base + 0x3000000 && !cave; q++)
        if (*(unsigned long long *)q == 0xCCCCCCCCCCCCCCCCull && *(unsigned long long *)(q + 8) == 0xCCCCCCCCCCCCCCCCull && q[-1] == 0xCC) cave = q;
    if (!cave) { log_line("building item shortcut: no stub space - left off.\n"); return; }
    game_path_equals = (PathEquals_t)(base + STREQ_RVA); original_resolve = (Resolve_t)(base + RESOLVE_RVA);
    game_path_of_object = (RefFromObj_t)(base + REFFROMOBJ_RVA); game_free = (Free_t)(base + FREE_RVA);
    item_main_thread = GetCurrentThreadId();   // DllMain runs on the game's main thread
    if (!(original_has_item = (HasItem_t)hook_function(p, 15, (void *)has_item_hook))) return;
    if (!(original_find_item = (FindItem_t)hook_function(base + FINDITEM_RVA, 17, (void *)find_item_hook))) return;
    DWORD old; unsigned char jmp[14] = {0xFF, 0x25, 0, 0, 0, 0}; void *dst = (void *)resolve_filter; memcpy(jmp + 6, &dst, 8);
    VirtualProtect(cave, 14, PAGE_EXECUTE_READWRITE, &old); memcpy(cave, jmp, 14); VirtualProtect(cave, 14, old, &old);
    point_call(base + HASITEM_CALL, cave); point_call(base + FINDITEM_CALL, cave);
    FlushInstructionCache(GetCurrentProcess(), base, 0x3000000);
    log_line("building item shortcut installed (has + find)\n");
}
