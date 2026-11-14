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

/* Applies every fix that isn't switched off. Runs once, from DllMain. */
static void install_all(void) {
    wchar_t ini[MAX_PATH];
    if (GetModuleFileNameW(NULL, ini, MAX_PATH)) {
        for (int up = 0; up < 3; up++) { wchar_t *sl = wcsrchr(ini, L'\\'); if (sl) *sl = 0; }   // ...\ConanSandbox
        wcscat(ini, L"\\Saved\\Config\\WindowsNoEditor\\Game.ini");
        read_server_slot(ini, server_slot, 64);
    }
    log_line("server for saved data: %ls\n", server_slot);
    if (!switch_on(L"nosleep")) skip_pak_sleeps();
    if (!switch_on(L"noghost")) disable_window_ghosting();
    if (!switch_on(L"noocclusion")) fix_occlusion_hash();
    if (!switch_on(L"nosurvey")) skip_survey();
    verify_every_skip = switch_on(L"fullcheck");
    if (!switch_on(L"nopak")) install_pak_skip();
    if (!switch_on(L"noasset")) install_asset_compare();
    if (!switch_on(L"nowaitfix")) install_join_fix();
    if (!switch_on(L"noitem")) install_building_items();
    if (!switch_on(L"nomodcache")) install_folder_lists();
    if (!switch_on(L"noregcache")) install_registry_cache();
    if (switch_on(L"loadlog")) install_load_log();
    if (!switch_on(L"nomd5")) install_checksum_cache();
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
