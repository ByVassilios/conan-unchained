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

// One install function per fix.
void install_checksum_cache(void);
void skip_survey(void);
void skip_pak_sleeps(void);
void disable_window_ghosting(void);
void fix_occlusion_hash(void);
void install_pak_skip(void);
void install_asset_compare(void);
void install_join_fix(void);
void install_building_items(void);
void install_folder_lists(void);
void install_registry_cache(void);
void install_load_log(void);
