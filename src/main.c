// Conan Unchained - load time and performance fixes for Conan Exiles (legacy build).
//
// Built as xinput1_3.dll and dropped next to ConanSandbox.exe; the game loads it on its own and every
// XInput call is passed on to the real DLL in System32. (A version.dll proxy doesn't work: Windows
// loads its own copy before the game can pick up ours.) All patches happen in memory, and each one
// checks the game's bytes first and stays off if they don't match this build.
//
// Build with build.bat, run the self-test with test.bat. Each fix lives in its own file under src/;
// this file sets up the log and switch files and installs them.

#include "unchained.h"

wchar_t data_dir[MAX_PATH], checksum_path[MAX_PATH], log_path[MAX_PATH];   // data_dir = Win64\unchained, the others inside it
wchar_t server_slot[64] = L"unknown";

/* Appends a printf-style line to unchained\log.txt. Opens and closes the file each time so nothing is lost on a crash. */
void log_line(const char *fmt, ...) {
    FILE *f = _wfopen(log_path, L"ab"); if (!f) return;
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fclose(f);
}

// Switch files in the unchained folder: "no<fix>" turns a fix off, "fullcheck" re-checks every pak
// skip (slow, for testing).
int switch_on(const wchar_t *name) {
    wchar_t p[MAX_PATH];
    swprintf(p, MAX_PATH, L"%ls\\%ls", data_dir, name);
    if (GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES) return 0;
    log_line("switch %ls is on\n", name);
    return 1;
}

// Every fix in install order: off switch, name shown in the menu, install function, and the flag its
// safety check sets if it turns itself off later. state: 0 = switched off, 1 = loaded, 2 = failed.
typedef struct { const wchar_t *off, *name; int (*install)(void); volatile LONG *tripped; int state; } Fix;
static Fix fixes[] = {
    {L"nosleep",     L"Pak mount sleeps",       skip_pak_sleeps},
    {L"noghost",     L"Not Responding fix",     disable_window_ghosting},
    {L"noocclusion", L"Occlusion hash",         fix_occlusion_hash},
    {L"nosurvey",    L"Hardware survey skip",   skip_survey},
    {L"nopak",       L"Pak search skip",        install_pak_skip,        &pak_skip_disabled},
    {L"noasset",     L"Item list checks",       install_asset_compare,   &asset_shortcut_disabled},
    {L"nowaitfix",   L"Stuck join fix",         install_join_fix},
    {L"noitem",      L"Building item lookups",  install_building_items,  &item_shortcut_disabled},
    {L"nomodcache",  L"Mod folder lists",       install_folder_lists,    &folder_lists_disabled},
    {L"noregcache",  L"Asset scan cache",       install_registry_cache},
    {L"nomd5",       L"Mod checksum cache",     install_checksum_cache},
    {L"noprehash",   L"Parallel mod hashing",   install_prehash,         &prehash_disabled},
};
#define FIX_COUNT (int)(sizeof fixes / sizeof fixes[0])

/* Appends one "name: state" line per fix to out (length n so far); returns the new length. */
int status_lines(wchar_t *out, int cap, int n) {
    for (int i = 0; i < FIX_COUNT; i++) {
        const Fix *f = &fixes[i];
        const wchar_t *s = f->state == 0 ? L"Disabled" : f->state == 2 ? L"Failed to load"
                         : f->tripped && *f->tripped ? L"Turned off by safety check" : L"Loaded";
        n = wcs_append(out, cap, n, f->name); n = wcs_append(out, cap, n, L": ");
        n = wcs_append(out, cap, n, s); n = wcs_append(out, cap, n, L"\n");
    }
    return n;
}

/* Applies every fix that isn't switched off. Runs once, from DllMain. */
static void install_all(void) {
    wchar_t ini[MAX_PATH];
    if (GetModuleFileNameW(NULL, ini, MAX_PATH)) {
        for (int up = 0; up < 3; up++) { wchar_t *sl = wcsrchr(ini, L'\\'); if (sl) *sl = 0; }   // ...\ConanSandbox
        wcscat(ini, L"\\Saved\\Config\\WindowsNoEditor\\Game.ini");
        read_server_slot(ini, server_slot, 64);
    }
    log_line("server for saved data: %ls\n", server_slot);
    verify_every_skip = switch_on(L"fullcheck");
    if (switch_on(L"loadlog")) install_load_log();
    for (int i = 0; i < FIX_COUNT; i++)
        if (!switch_on(fixes[i].off)) { int r = fixes[i].install(); fixes[i].state = r < 0 ? 0 : r ? 1 : 2; }
    if (!switch_on(L"nomenu")) install_menu_status();
}

/* Sets up the file paths and installs the fixes, but only inside ConanSandbox.exe. */
BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID r) {
    (void)r;
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(inst);
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (_wcsicmp(wcsrchr(exe, L'\\') + 1, L"ConanSandbox.exe")) return TRUE; // only touch the game itself
    GetModuleFileNameW(inst, data_dir, MAX_PATH);
    wcscpy(wcsrchr(data_dir, L'\\') + 1, L"unchained");   // everything the DLL writes goes in Win64\unchained
    CreateDirectoryW(data_dir, NULL);
    swprintf(checksum_path, MAX_PATH, L"%ls\\checksums.txt", data_dir);
    swprintf(log_path, MAX_PATH, L"%ls\\log.txt", data_dir);
    _wremove(log_path);
    install_all();
    return TRUE;
}
