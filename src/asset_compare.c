// Soft asset reference compare
// UAssetObjectProperty::Identical (TAssetPtr / soft references) copies both values into temporary
// asset pointers, runs each path through FStringAssetReference::SetPath twice, compares, then frees
// it all: ~4 allocations per compare. Blueprint "Array Contains" on structs that hold arrays of soft
// references does this millions of times while the world loads in (3 s of main-thread time).
// SetPath (0x167D9D0) leaves a path that is empty or starts with '/' unchanged, so for those the
// result is exactly the game's own case-insensitive compare (0x2804A30) of the stored strings.

#include "unchained.h"

typedef char (*AssetIdent_t)(void *prop, const void *a, const void *b, unsigned flags);
static AssetIdent_t original_asset_identical;
PathEquals_t game_path_equals;
static volatile LONG64 asset_compares, asset_shortcuts; volatile LONG asset_shortcut_disabled;
#define ASSET_RVA 0x16E1B50     // UAssetObjectProperty::Identical(this, A, B, PortFlags)
#define ASSETPTR_PATH 0x10      // FStringAssetReference (one FString) inside FAssetPtr
static const short ASSET_SIG[] = {
    0x48,0x8b,0xc4,0x55,0x57,0x41,0x56,0x48,0x8d,0x68,0xa1,0x48,0x81,0xec,0xe0,0x00,0x00,0x00,
    0x48,0xc7,0x45,0x3f,0xfe,0xff,0xff,0xff,0x48,0x89,0x58,0x08,0x48,0x89,0x70,0x20,
    0x49,0x8b,0xf8,0x48,0x8b,0xda,0x45,0x33,0xf6,0x44,0x89,0x75,0x6f};

/* The path string stored inside an FAssetPtr (an empty string for NULL). */
static const FString *asset_path(const void *v) { static const FString empty; return v ? (const FString *)((const char *)v + ASSETPTR_PATH) : &empty; }
/* True if SetPath would leave this path unchanged: empty, or starting with '/'. */
static int path_stable(const FString *s) { return s->num <= 1 || (s->data && s->data[0] == L'/'); }

/* Replacement for UAssetObjectProperty::Identical: compares the stored paths directly when that
   gives the same answer, and spot-checks the result against the game. */
static char asset_identical_hook(void *prop, const void *a, const void *b, unsigned flags) {
    const FString *pa = asset_path(a), *pb = asset_path(b);
    LONG64 n = InterlockedIncrement64(&asset_compares);
    if (n % 1000000 == 0) log_line("asset compare: %lld calls, %lld shortcut\n", n, asset_shortcuts);
    if (asset_shortcut_disabled || !path_stable(pa) || !path_stable(pb)) return original_asset_identical(prop, a, b, flags);
    char r = game_path_equals(pa, pb) != 0;
    LONG64 k = InterlockedIncrement64(&asset_shortcuts);
    if (k <= 2000 || k % 1024 == 0) {   // safety net: compare with the game's own answer
        char o = original_asset_identical(prop, a, b, flags) != 0;
        if (o != r) {
            asset_shortcut_disabled = 1;
            log_line("asset compare shortcut DISABLED: '%ls' vs '%ls': shortcut %d, game %d\n",
                  pa->num > 1 ? pa->data : L"", pb->num > 1 ? pb->data : L"", r, o);
            return o;
        }
    }
    return r;
}

/* Hooks UAssetObjectProperty::Identical (exact game build only). */
int install_asset_compare(void) {
    unsigned char *base = (unsigned char *)GetModuleHandleW(NULL);
    unsigned char *p = find_signature(ASSET_SIG, sizeof ASSET_SIG / sizeof ASSET_SIG[0], ASSET_RVA);
    static const unsigned char eq[] = {0x48,0x83,0xec,0x28, 0x83,0x7a,0x08,0x00, 0x48,0x8b,0xc1};
    // Calls a game function at a fixed address, so only on the exact build it was written for.
    if (p != base + ASSET_RVA || memcmp(base + STREQ_RVA, eq, sizeof eq)) { log_line("asset compare: game build differs - left off.\n"); return 0; }
    game_path_equals = (PathEquals_t)(base + STREQ_RVA);
    if (!(original_asset_identical = (AssetIdent_t)hook_function(p, 18, (void *)asset_identical_hook))) return 0;
    log_line("asset compare shortcut installed\n");
    return 1;
}
