// Menu status
// The main menu (W_MainMenu_MainMenu) shows the build at the bottom right. Its Blueprint builds
// "Revision (#R/S) | Backend <env> (<rev>)" with string concats and turns it into text with
// Conv_StringToText (impl 0x26562A0, only called from its Blueprint thunk 0x2B20230). We hook that
// impl and, for that one string, put "[CONAN UNCHAINED]" and one line per fix above it.
// FText::FromString (0x1594E50) copies the string, so a stack buffer is fine.
// The in-game menu (W_IngameMenu) builds the very same string, so we look at which widget is asking:
// the thunk keeps the Blueprint call frame (FFrame) in rbx, a small stub hands it to the hook as a
// 4th argument (the impl ignores r9), and the frame's Object (+0x18) must be of class
// W_MainMenu_MainMenu_C. Class names come from the engine's name table (65536 hash buckets of
// entries: int index<<1 | wide flag at +0, next at +8, ANSI text at +0x10; objects hold the index
// at +0x18, their class at +0x10).

#include "unchained.h"

typedef void *(*StrToText_t)(void *out, const FString *in, void *a, void *frame);
static StrToText_t original_str_to_text;
#define STRTOTEXT_RVA 0x26562A0
#define NAME_HEADS_RVA 0x5C25A10
static const unsigned char STRTOTEXT_SIG[] = {0x40,0x53, 0x48,0x83,0xec,0x30, 0x48,0x8b,0xd9, 0xc7,0x44,0x24,0x20,0x00,0x00,0x00,0x00, 0xe8};
static const unsigned char THUNK_SIG[] = {0x48,0x8b,0xda};   // mov rbx, rdx (rbx = FFrame) at 0x2B20247
static const wchar_t MENU_TAG[] = L"Revision (#";
static void **name_heads;
static int main_menu_name = -1;   // FName index of "W_MainMenu_MainMenu_C", found on first use

/* Status lines followed by the game's text; returns the length written. */
int menu_text(wchar_t *out, int cap, const wchar_t *game) {
    int n = wcs_append(out, cap, 0, L"[CONAN UNCHAINED]\n");
    n = status_lines(out, cap, n);
    n = wcs_append(out, cap, n, L"\n");   // blank line before the game's revision text
    return wcs_append(out, cap, n, game);
}

/* FName index of an ANSI name, or -1. Walks the whole table: only done until it is found. */
static int find_name(const char *s) {
    for (int k = 0; k < 65536; k++)
        for (char *e = name_heads[k]; e; e = *(char **)(e + 8))
            if (!(*(int *)e & 1) && !strcmp(e + 0x10, s)) return *(int *)e >> 1;
    return -1;
}

/* True if the Blueprint frame belongs to the main menu widget. */
static int from_main_menu(const char *frame) {
    const char *obj = frame ? *(char **)(frame + 0x18) : NULL, *cls = obj ? *(char **)(obj + 0x10) : NULL;
    if (!cls) return 0;
    if (main_menu_name < 0) main_menu_name = find_name("W_MainMenu_MainMenu_C");
    return main_menu_name >= 0 && *(int *)(cls + 0x18) == main_menu_name;
}

static void *str_to_text_hook(void *out, const FString *in, void *a, void *frame) {
    if (!in || in->num <= (int)wcslen(MENU_TAG) || !in->data || wcsncmp(in->data, MENU_TAG, wcslen(MENU_TAG)) || !from_main_menu(frame))
        return original_str_to_text(out, in, a, frame);
    wchar_t buf[4096];
    int n = menu_text(buf, 4096, in->data);
    FString s = {buf, n + 1, n + 1};
    return original_str_to_text(out, &s, a, frame);
}

/* Hooks Conv_StringToText through a stub that passes rbx (the Blueprint frame) in r9 (exact game build only). */
int install_menu_status(void) {
    unsigned char *base = (unsigned char *)GetModuleHandleW(NULL), *p = base + STRTOTEXT_RVA;
    if (memcmp(p, STRTOTEXT_SIG, sizeof STRTOTEXT_SIG) || memcmp(base + 0x2B20247, THUNK_SIG, sizeof THUNK_SIG)) {
        log_line("menu status: game build differs - left off.\n"); return 0;
    }
    unsigned char *stub = VirtualAlloc(NULL, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!stub) return 0;
    void *dst = (void *)str_to_text_hook;
    memcpy(stub, "\x49\x89\xd9\x48\xb8", 5); memcpy(stub + 5, &dst, 8); memcpy(stub + 13, "\xff\xe0", 2);   // mov r9,rbx; mov rax,hook; jmp rax
    name_heads = (void **)(base + NAME_HEADS_RVA);
    if (!(original_str_to_text = (StrToText_t)hook_function(p, 17, stub))) return 0;
    log_line("menu status installed\n");
    return 1;
}
