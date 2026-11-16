// Shared declarations for Conan Unchained. Each fix lives in its own .c file; main.c installs them.
#pragma once
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// UE4 containers as laid out in memory by this game build.
typedef struct { wchar_t *data; int num, max; } FString;
typedef struct { FString *data; int num, max; } FStringArray;   // TArray<FString>

// main.c
extern wchar_t data_dir[MAX_PATH], checksum_path[MAX_PATH], log_path[MAX_PATH];
extern int verify_every_skip;
void log_line(const char *fmt, ...);
int switch_on(const wchar_t *name);

// util.c
int signature_matches(const unsigned char *p, const short *sig, size_t n);
unsigned char *find_signature(const short *sig, size_t n, size_t known_rva);
void *hook_function(unsigned char *target, int prologue, void *dst);
void patch_bytes(unsigned char *p, const void *bytes, int n);
unsigned long long fnv1a(const void *p, size_t n, unsigned long long h);
unsigned long long file_fingerprint(const wchar_t *path, unsigned long long size, unsigned long long mtime);
int wcs_append(wchar_t *out, int cap, int n, const wchar_t *s);
void keep_newest(const wchar_t *dir, const wchar_t *pattern, int keep);
#define MOD_LISTS_KEPT 5   // per-server caches kept on disk (folder lists, asset registry)
void read_server_slot(const wchar_t *ini, wchar_t *out, int n);
extern wchar_t server_slot[64];   // this launch's server, e.g. "217.182.174.204_7777"

// asset_compare.c: the game's own case-insensitive path compare, also used by building_items.c
#define STREQ_RVA 0x2804A30     // FStringAssetReference operator== (Stricmp of the two paths)
typedef char (*PathEquals_t)(const FString *a, const FString *b);
extern PathEquals_t game_path_equals;

// checksums.c
unsigned long long checksums_fingerprint(void);

// One install function per fix; each returns 1 if it is in place, 0 if it couldn't be applied.
int install_checksum_cache(void);
int install_prehash(void);
int skip_survey(void);
int skip_pak_sleeps(void);
int disable_window_ghosting(void);
int fix_occlusion_hash(void);
int install_pak_skip(void);
int install_asset_compare(void);
int install_join_fix(void);
int install_building_items(void);
int install_folder_lists(void);
int install_registry_cache(void);
int install_menu_status(void);
void install_load_log(void);

// Set by a fix's safety check when it switches itself off for the session (shown in the menu).
extern volatile LONG pak_skip_disabled, asset_shortcut_disabled, item_shortcut_disabled, folder_lists_disabled, prehash_disabled;

// main.c: one "name: state" line per fix; menu_status.c: those lines plus the game's text.
int status_lines(wchar_t *out, int cap, int n);
int menu_text(wchar_t *out, int cap, const wchar_t *game);
