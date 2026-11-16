<div align="center">

# Conan Unchained

**Faster startup, faster joins and fewer stuck loading screens for heavily modded Conan Exiles.**

[![Game](https://img.shields.io/badge/Conan%20Exiles-legacy%20build-8b5a2b)](#read-this-first)
[![Platform](https://img.shields.io/badge/platform-Windows%20x64-0078d4)](#install)
[![Status](https://img.shields.io/badge/status-experimental-e67e22)](#read-this-first)
[![Language](https://img.shields.io/badge/written%20in-C-555555)](#building-it-yourself)

[Install](#install) · [What it fixes](#what-it-fixes) · [Troubleshooting](#troubleshooting) · [Uninstall](#uninstall) · [How it works](#how-it-works)

</div>

---

## Results

Measured with 65 mods (39 GB) on an NVMe SSD:

| | Before | With Conan Unchained | |
|---|---:|---:|---|
| Engine startup | 142–181 s | **24–31 s** | about 6× faster |
| Frozen screen after pressing Connect | 72 s | **12–15 s** | about 5× faster |
| Launch to in the world (auto-connect) | over 3 min | **about 77 s** | about 23 s of it is waiting for the server |

It's a single file, `xinput1_3.dll`, dropped next to the game's exe. Controllers keep working: every controller call is passed on to Windows' own `xinput1_3.dll` in `C:\Windows\System32`.

## Read this first

> [!WARNING]
> **Not for BattlEye servers.** BattlEye may treat an unknown DLL inside the game as a cheat and **ban your account**. Only use it in single player or on servers with BattlEye disabled. If you also play on BattlEye servers, remove the file before launching with BattlEye.

> [!IMPORTANT]
> **Experimental.** This is a hobby patch made by reverse-engineering the game. It was tested by one person, with 65 mods, on one server. It can have bugs that crash the game or stop a join from finishing. If something goes wrong, [uninstall it](#uninstall) and the game is exactly as before.

> [!NOTE]
> **Legacy build only.** It patches exact spots in the code of one specific build of Conan Exiles. On any other version, every fix checks the game's code first and switches itself off if it doesn't match: you get no speedup, but nothing breaks. It never changes game files on disk; every fix only happens in memory while the game runs.

## What it fixes

| # | Fix | What you notice | Off switch |
|:-:|---|---|---|
| 1 | Mod checksums are saved, and new ones are calculated on 8 cores at once | Startup about 100 s shorter | `nomd5`, `noprehash` |
| 2 | A telemetry hardware survey that blocks startup is skipped | 3–5 s off startup | `nosurvey` |
| 3 | Needless sleeps while the game opens its `.pak` files are removed | The game gets going about 6 s sooner | `nosleep` |
| 4 | Wasted searches through `.pak` files are skipped | Shorter join freeze, faster background scan | `nopak` |
| 5 | Mod folder file lists are saved instead of rebuilt on every first join | About 20 s off the first join | `nomodcache` |
| 6 | The game's own asset scan cache now gets filled (it was always empty) | Background scan done about 35 s sooner | `noregcache` |
| 7 | Two game bugs that leave you stuck on the loading screen are fixed | Joins that used to hang now finish | `nowaitfix` |
| 8 | Building pieces find their item data faster | Less stutter when you walk up to big bases | `noitem` |
| 9 | Item list checks in mod scripts are faster | Smoother first minute in the world | `noasset` |
| 10 | Windows no longer shows "(Not Responding)" during the join freeze | The loading screen stays on screen | `noghost` |
| 11 | Occlusion culling no longer walks long lists for objects made of many pieces | Less lag when big builds or new areas come into view | `noocclusion` |

Every fix is on by default. See [Turning a fix off](#turning-a-fix-off) for the switches.

## Install

1. Close Conan Exiles.
2. In Steam, right-click Conan Exiles → **Manage** → **Browse local files**, then open `ConanSandbox\Binaries\Win64`.
3. If there is already an `xinput1_3.dll` there (from another mod or tool), rename it to `xinput1_3.dll.bak` first, so you can put it back later.
4. Download `xinput1_3.dll` from the [latest release](https://github.com/ByVassilios/conan-unchained/releases/latest) and copy it into that folder, next to `ConanSandbox.exe`.
5. Launch the game as usual.

> [!TIP]
> **The first launch is slower.** It saves mod checksums, mod folder lists and the asset scan results. The checksums are calculated on 8 cores at once (about 17 s for 57 GB of mods, instead of about 100 s), but the folder lists and the asset scan still take one full pass. From the second launch on it's fast. After a mod update, a game update, or adding or removing a mod, the affected saved data is rebuilt once by itself.

### Playing on several servers

Saved mod folder lists and asset scan results are kept separately for each server, for the last 5 servers you played on (about 64 MB each with 63 mods, less with fewer mods). The server is read from `LastConnected` in the game's `Game.ini`, which the game's server browser and launchers like Conay set before the game starts or restarts with that server's mods.

- Going back to a server you played on recently is fast right away.
- The first launch on a new server, or after that server's mods changed, is slow once and replaces that server's saved data.
- If you play on more than 5 servers, the one unused the longest is deleted.
- Mod checksums are kept per mod file, so they're never rebuilt for a mod you already have.

The log shows which server it picked: `server for saved data: ...`.

## Turning a fix off

Create an empty file named after the switch (see the [table above](#what-it-fixes), no file extension) in the `unchained` folder next to the DLL, `ConanSandbox\Binaries\Win64\unchained`. For example, `unchained\nopak` turns off fix 4. The log confirms it with `switch ... is on`.

- `nomd5` turns off the whole checksum cache, which also turns off fix 5.
- `noprehash` turns off only the parallel hashing; saved checksums stay on.

If a join gets stuck or something looks wrong, try `nopak` first, then the others one at a time.

## Troubleshooting

The quickest check is the bottom-right corner of the main menu. Above the game's revision line it shows `[CONAN UNCHAINED]` and one line per fix: `Loaded`, `Disabled` (switch file), `Failed to load` (your game version differs), or `Turned off by safety check`. The `nomenu` switch turns this off.

After launching, open `unchained\log.txt` in `ConanSandbox\Binaries\Win64`. Near the top you should see lines like:

```
pak mount sleeps removed
window ghosting off
hardware survey skipped at rva 0x...
pak search skip installed at rva 0x... (mod search 0x...)
pak search skip also covers asset folder scans
asset compare shortcut installed
stuck join fix installed
building item shortcut installed (has + find)
mod file list cache installed
asset registry cache: cooked packages are cached
hook installed at rva 0x...
hit  D:/.../SomeMod.pak
```

| Log line | Meaning |
|---|---|
| `hit` | A saved checksum was used. |
| `miss` | The checksum was recalculated. Normal on the first launch or after a mod update. |
| anything with `DISABLED` | A safety check found a difference from the game's own result and switched that fix off for the session. The game keeps working normally. Please report it. |
| `game build differs` or `signature not found` | Your game version isn't the one this was made for, and that fix is off. |

## Uninstall

1. Close Conan Exiles.
2. In `ConanSandbox\Binaries\Win64`, delete `xinput1_3.dll` and the `unchained` folder. The folder holds everything the DLL writes: the log, saved checksums, saved mod folder lists, the asset scan caches parked for other servers, and any switch files you created.
3. Delete the `.bin` files in `ConanSandbox\Intermediate\AssetRegistryCache`. The game rebuilds what it needs.
4. If you renamed an older `xinput1_3.dll` to `xinput1_3.dll.bak` during install, rename it back.
5. If you used the optional net speed setting below, remove those lines from your `Engine.ini`.

<details>
<summary>Leftovers from older versions</summary>

In `Binaries\Win64`: the `legacyfix` folder, `legacyfix.log`, `legacyfix-checksums.txt`, `legacyfix-modfiles*.bin`, `legacyfix-no...` and `md5cache*`. In `Intermediate\AssetRegistryCache`: `legacyfix-fingerprint.txt` and the `legacyfix-sets` folder. All of them can be deleted.

</details>

## Extras

### Bases stream in faster after joining (config, no DLL needed)

By default the game asks servers for at most 25,000 bytes/s, so big bases can take a long time to stream in after the loading screen. Add this to `ConanSandbox\Saved\Config\WindowsNoEditor\Engine.ini` (make a copy of the file first):

```ini
[/Script/Engine.Player]
ConfiguredInternetSpeed=500000
ConfiguredLanSpeed=500000
```

A server can still cap this. Server admins can raise their cap in the server's `Engine.ini`:

```ini
[/Script/OnlineSubsystemUtils.IpNetDriver]
MaxClientRate=500000
MaxInternetClientRate=500000
```

To undo, remove those lines.

### Antivirus

Some antivirus programs may flag this file. A DLL that loads into a game and changes its code in memory looks like a cheat or malware to them. The full source code is included, so you can check it or [build it yourself](#building-it-yourself).

> [!CAUTION]
> **Optional extra speedup, at your own risk.** Windows Defender scans large mod files when the game opens them, which can add 10+ seconds. You can add exclusions for your Conan Exiles folder and `steamapps\workshop\content\440900` in Windows Security → Virus & threat protection → Manage settings → Exclusions. Defender then stops checking anything in those folders, including new mods.

## Building it yourself

If you'd rather not run a downloaded DLL, you can build your own from the source. The first time takes about 5 minutes.

1. Install MinGW-w64 (gcc). The easiest way, in a command prompt:
   `winget install BrechtSanders.WinLibs.POSIX.UCRT`
   Then open a new command prompt so `gcc` is found.
2. In this folder, run `test.bat`. It builds and runs the self-test and should print `ok`.
3. Run `build.bat`. It makes `xinput1_3.dll` in this folder.
4. Install that file as described above.

What `build.bat` runs, if you want to do it by hand:

```
gcc -shared -O2 -s -Wall -Wno-cast-function-type -o xinput1_3.dll src\*.c xinput1_3.def -static-libgcc -lbcrypt
```

<details>
<summary>How the source is laid out</summary>

| File | What's in it |
|---|---|
| `src/main.c` | Log file, switch files, installs every fix at load |
| `src/util.c` | Finding game code by its bytes, hooking functions, patching bytes |
| `src/checksums.c` | 1. Mod checksum cache and parallel hashing |
| `src/small_patches.c` | 2. Hardware survey skip, 3. pak sleeps, 10. "(Not Responding)" fix, 11. Occlusion hash |
| `src/pak_skip.c` | 4. Pak search skip |
| `src/folder_lists.c` | 5. Mod folder file list cache |
| `src/registry_cache.c` | 6. Asset scan cache |
| `src/join_fix.c` | 7. Stuck loading screen fixes |
| `src/building_items.c` | 8. Building piece shortcut |
| `src/asset_compare.c` | 9. Item list check shortcut |
| `src/menu_status.c` | Fix status in the menu's bottom-right corner |
| `src/load_log.c` | Diagnostic load logger (off unless `unchained\loadlog` exists) |
| `src/xinput.c` | Passes controller calls on to Windows' own `xinput1_3.dll` |
| `tests/test.c` | Self-test, with fake versions of the game functions |

</details>

---

## How it works

> [!NOTE]
> All addresses are offsets (RVAs) in `ConanSandbox.exe` of the build this was made for. Every patch first compares the game's bytes at that spot with what it expects, and leaves the game alone if they differ.

<details>
<summary><b>1. Mod checksum cache</b> &nbsp;·&nbsp; <code>about 100 s → 0.7 s</code></summary>
<br>

| Before | After | Off switch | Source |
|---|---|:-:|:-:|
| 79–107 s every launch | 0.7 s (about 17 s on a first launch) | `nomd5`, `noprehash` | [`src/checksums.c`](src/checksums.c) |

#### The problem

At startup, `UModManager::LoadModInfo` computes an MD5 checksum of every mod `.pak` (`FMD5Hash::HashFile`, 0x15EFA20), one file at a time, on every launch. With 65 mods (39 GB) that took 79 to 107 s each time.

#### The fix

The DLL hooks `HashFile` and saves each result in `unchained\checksums.txt`, keyed by file path, size and last-modified time. If all three match, the saved checksum is returned; otherwise the game calculates it and the result is saved. Servers get exactly the checksum the game would have calculated, so modded servers accept you as before.

#### Result

The mod phase of startup went from 79 to 107 s to 0.7 s.

#### Parallel hashing for new checksums

on a first launch, or after mods were added or updated, the game would still hash every new mod one at a time. So at launch, the DLL reads `Mods\modlist.txt` and hashes every mod that has no valid saved checksum on 8 background threads (Windows' own MD5, `BCryptHash`; the game's checksum is a plain MD5 of the whole file). When the game asks for a file, the DLL waits for that thread's result instead of hashing it again. If every mod already has a saved checksum, no threads are started.

> 🛡️ **Safety check:** the first prehashed file under 64 MB each launch is also hashed by the game itself and compared. Any difference writes `prehash DISABLED` and the game hashes everything itself for the rest of the launch.

#### Result

all 84 saved checksums reproduced exactly by the parallel hashing; 56.8 GB in 16.7 s (3.4 GB/s on an NVMe SSD), against about 100 s for the game on one core.

</details>

<details>
<summary><b>2. Hardware survey skip</b> &nbsp;·&nbsp; <code>3–5 s → 0.02 s</code></summary>
<br>

| Before | After | Off switch | Source |
|---|---|:-:|:-:|
| 3.4–5.4 s of WMI queries | 0.02 s | `nosurvey` | [`src/small_patches.c`](src/small_patches.c) |

#### The problem

`UConanGameInstance::Init` asks Windows (WMI: `Win32_Processor`, `Win32_VideoController`, `Win32_PhysicalMemory`, `MSFT_PhysicalDisk`) for your hardware up to 3 times, on the main thread, only to send it to Funcom's telemetry. Each query blocks for about a second.

#### The fix

The conditional jump that guards the survey (0x4A4C0D) always takes the "setting off" branch, the same path the game takes when that setting is turned off.

#### Result

That part of startup went from 3.4 to 5.4 s to 0.02 s.

</details>

<details>
<summary><b>3. Pak sleeps removed</b> &nbsp;·&nbsp; <code>log opens at 3.5 s, not 10 s</code></summary>
<br>

| Before | After | Off switch | Source |
|---|---|:-:|:-:|
| main thread blocked 9.4 s of the first 10 s | log opens 3.5 s after start | `nosleep` | [`src/small_patches.c`](src/small_patches.c) |

#### The problem

Before its log even opens, the game opens and checks its signed `.pak` files. In a trace, the main thread was blocked for 9.4 s of those first 10 s, while the threads it was waiting for used only 0.4 s of CPU. Two sleeps caused it:
- `SetThreadName` (0x162CFC0) calls `Sleep(10)` on the creating thread for every new thread, only so a debugger can catch the thread's name. The pak signature checker (`FChunkCacheWorker`) starts one thread per pak.
- The signed pak reader (0x324B700) waits for its checker by polling with `FPlatformProcess::Sleep(0.001)`. Windows rounds that up to a full 15.6 ms timer tick, while a check takes well under 1 ms.

Raising Windows' timer resolution was tried first and made no difference: Windows 11 ignores that request from a program that doesn't have a visible window yet.

#### The fix

`Sleep(10)` becomes `Sleep(0)` (byte at 0x162CFD0). The reader's poll interval becomes 0 (`movss xmm6,[1 ms]` at 0x324B877 becomes `xorps xmm6,xmm6`), so it yields to other threads (`SwitchToThread`) and checks again right away.

#### Result

The game's log now opens about 3.5 s after the process starts, instead of 10 s.

</details>

<details>
<summary><b>4. Pak search skip</b> &nbsp;·&nbsp; <code>99.5% of searches skipped</code></summary>
<br>

| Before | After | Off switch | Source |
|---|---|:-:|:-:|
| 5.7 million searches per join, 62 s background CPU | 99.5% skipped, 22 s background CPU | `nopak` | [`src/pak_skip.c`](src/pak_skip.c) |

#### The problem

`FPakFile::FindFilesAtPath` (0x3236260) loops over a pak's entire directory index for every folder listing, building a text string per directory. About 45 base-game paks are mounted at the game's root, so every folder listing walks all of them. The mod controller search on your first join did 5.7 million of these searches. The game's background asset scan, which runs from launch until about a minute in, does the same: in one trace it used 62 s of CPU. All these threads also fight over the memory allocator's lock, which slows down the engine's startup loading on another thread.

#### The fix

If a pak returns nothing at all for folder `A/`, it has nothing anywhere under `A/` either. The DLL remembers those empty (pak, folder) pairs and skips later searches inside them. This is active only on threads running the mod controller search (0x37D1F50) or an asset folder scan (0x2139AF0). The list is wiped at the start and end of every mod controller search, so nothing learned before a join is used in it, and nothing learned in it is used later.

> 🛡️ **Safety check:** Folders *above* a pak's mount point are never treated as empty (a pak mounted at `Content/Mods/X/` returns nothing for `Content/Mods/` but has files below it). The first 2000 skips and 1 in 256 after that are re-checked against the real search; any difference writes `pak search skip DISABLED` and turns the skip off. In a test with every skip re-checked, 2,824,302 skips matched the real search exactly.

#### Result

2.85 million pak searches per join, 99.5% skipped. The background scan's discovery thread went from 62 s to 22 s of CPU. Together with fixes 3 and 5: engine startup from 45 s to 27.5 s, Connect to Welcomed from 39.4 s to 13.3 s (one traced run each).

</details>

<details>
<summary><b>5. Mod folder file list cache</b> &nbsp;·&nbsp; <code>mod search 49.5 s → 26.8 s</code></summary>
<br>

| Before | After | Off switch | Source |
|---|---|:-:|:-:|
| every mod folder listed on every first join | lists read from disk | `nomodcache` | [`src/folder_lists.c`](src/folder_lists.c) |

#### The problem

The game keeps its own list of each mod's controller files (`ModControllerCache.json`), and that list was correct for every mod. Even so, on every first join it still listed every file in every mod's folder (0x212D200, a throwaway scan on its own stack that doesn't change anything) just to confirm those files exist. It also listed all of `/Game/DLC/` and `/Game/Mods/` the same way. Together that was most of the join freeze.

#### The fix

Calls from exactly those three spots (return addresses 0x37D2E57 per mod, 0x37D3654 `/Game/Mods/`, 0x37D3719 `/Game/DLC/`; main thread only) are answered from `unchained\modfiles-<server>.bin`. The array is filled using the game's own helper functions (0x2345BB0, 0x2D0200, 0x1DFC200). The saved lists are only used while every mod pak checksummed this launch and every `Content/Paks/*.pak` has the same path, size and modified time as when they were saved. Each server (`LastConnected` in `Game.ini`, `singleplayer` for a local game) has its own file, so switching between servers with different mods keeps each one; the 5 most recently used are kept and older ones deleted. If a server's mods changed, its file no longer matches the fingerprint and is overwritten with new lists.

> 🛡️ **Safety check:** The first per-mod list of each launch and 1 in 32 after that are re-listed and compared with the game's own listing. Any difference writes `mod file list cache DISABLED`, deletes the saved lists and turns this off. The big `/Game/DLC/` and `/Game/Mods/` lists aren't re-listed (that alone took 10.6 s), but they come from the same paks the fingerprint covers.

#### Result

Mod search on the first join: 49.5 s to 26.8 s on its own; about 12 s together with fixes 3, 4 and 6.

</details>

<details>
<summary><b>6. Asset scan cache</b> &nbsp;·&nbsp; <code>scan done at 56 s, not 93 s</code></summary>
<br>

| Before | After | Off switch | Source |
|---|---|:-:|:-:|
| cache always empty, full scan every launch | cache filled and reused per server | `noregcache` | [`src/registry_cache.c`](src/registry_cache.c) |

#### The problem

Right after launch, the game's asset registry scans every package in every game and mod pak on two background threads: one lists files, the other opens, unpacks and reads each package (51 s of CPU in a trace). The engine has a disk cache for exactly this (`Intermediate\AssetRegistryCache\<hash>.bin`), and the game turns it on. But before storing a package, the engine skips any package marked as "cooked" (`PKG_FilterEditorOnly`, the `cmp [rax+50h],0; jl` at 0x213AD73), and in the released game every package is cooked. So the cache file was always empty (24 bytes) and the full scan ran on every launch.

#### The fix

That one jump is replaced with a no-op, so cooked packages are stored, and later launches read them from the cache. Packages without asset data are still never stored (the game's other check is unchanged). The cache's own invalidation only checks per-package timestamps, so the DLL adds its own: it fingerprints every `Content\Paks\*.pak`, every `Mods\*.pak` and every pak in `Mods\modlist.txt` (path, size, modified time) plus the list's order. Each server has its own saved cache, labeled with that fingerprint in `unchained\asset-cache.txt`. When you launch for a different server, the DLL moves the current cache files into `unchained\asset-cache\<previous server>\` before the game starts, and moves the new server's parked files back in if their fingerprint still matches; otherwise the game starts a fresh cache for it. The same server with changed mods also starts fresh, which replaces its old cache. Up to 4 parked servers are kept besides the active one (5 in total), and the one unused the longest is deleted. It uses the list's content, not its modified time, because launchers rewrite `modlist.txt` before every launch.

#### Result

The background scan finished about 56 s after the log opened instead of about 93 s. The package reading thread went from 51 s to 28 s of CPU, and the cache fills further on each launch.

</details>

<details>
<summary><b>7. Stuck loading screen fixes (game bugs)</b> &nbsp;·&nbsp; <code>hung joins now finish</code></summary>
<br>

| Before | After | Off switch | Source |
|---|---|:-:|:-:|
| loading screen forever | join completes by itself | `nowaitfix` | [`src/join_fix.c`](src/join_fix.c) |

#### The problem

After joining, the player controller (`AConanPlayerController`) runs a 3-step "Waiting for streaming completed" check for the join (`game`) and another for the server's teleport (`teleport`). Two bugs can leave you on the loading screen forever:
- Shared counter: both checks use the same step counter (controller offset 0x11C0). If the teleport check finishes first, it resets the counter the join check is using, and the join never completes. Seen in testing: the server's answer to `ServerWaitForNearbyBuildings` took over 80 s, the teleport check finished meanwhile, and the join check never logged `Done`.
- Distance gate: the join check only continues while your camera is within 2000 units (20 m) of a spot the server sends (offset 0x1498). The teleport can put you further away than that (3,473 units in testing), and then the check is never run again. Writing the player's position into that spot on a stuck game made it finish instantly.

#### The fix

The DLL hooks the wait function (0x431350) and gives the teleport check its own counter. When the teleport check has finished, the join is still waiting, and the camera (the game's own `GetPlayerViewPoint`) is 2000 units or more from the server's spot, the spot is moved to the camera. **This doesn't move your character.** The spot is only read by that distance check (0x469AF8); nothing else uses it, and the server isn't told anything. No coordinates are hard-coded.

#### Result

In a traced join the teleport left the player 2,620 units away; the fix moved the spot and the join completed by itself.

</details>

<details>
<summary><b>8. Building piece shortcut</b> &nbsp;·&nbsp; <code>6.43 s → 1.00 s</code></summary>
<br>

| Before | After | Off switch | Source |
|---|---|:-:|:-:|
| 6.43 s of game-thread time | 1.00 s | `noitem` | [`src/building_items.c`](src/building_items.c) |

#### The problem

The first time a building piece type loads (a modded castle brings dozens at once), `ABuildableBase::LoadCDOItemData` finds its item by going through *every* item table row (`HasBuildingItem` 0x625470, then `FindBuildingItem` 0x61A2C0). For each row whose asset isn't loaded, it resolves the row's path to an object (0x167C220), which is slow, and only then compares paths, even though a row with a different path can never match.

#### The fix

Both lookups' resolve calls (0x625626, 0x61A4C7) go through a filter that skips rows whose stored path differs from the building's class path (the game's own case-insensitive compare), and restores those rows' cached bytes exactly afterwards. Main thread only.

> 🛡️ **Safety check:** The first 20 answers and 1 in 64 after that are compared with the game's own; any difference writes `building item shortcut DISABLED` and turns it off.

#### Result

`LoadCDOItemData` went from 6.43 s to 1.00 s of game-thread time; 4.4 million slow lookups skipped in one join.

</details>

<details>
<summary><b>9. Item list check shortcut</b> &nbsp;·&nbsp; <code>3.8 s → 1.0 s</code></summary>
<br>

| Before | After | Off switch | Source |
|---|---|:-:|:-:|
| 3.8 s of game-thread time | 1.0 s | `noasset` | [`src/asset_compare.c`](src/asset_compare.c) |

#### The problem

Mod scripts check "is this in my list?" (`Array Contains`) on lists of asset references. For every entry, the engine (`UAssetObjectProperty::Identical`, 0x16E1B50) builds two temporary asset pointers, reformats each path twice with new strings, compares them and frees them. That happens millions of times while the world loads in.

#### The fix

For paths that reformatting can't change (empty, or starting with `/`, which is every normal path), the stored strings are compared directly with the game's own case-insensitive compare. Anything else goes through the original code.

> 🛡️ **Safety check:** The first 2000 shortcuts and 1 in 1024 after that are compared with the game's answer; any difference writes `asset compare shortcut DISABLED` and turns it off.

#### Result

`Array_Contains` went from 3.8 s to 1.0 s of game-thread time after loading in.

</details>

<details>
<summary><b>10. No "(Not Responding)" during the join freeze</b> &nbsp;·&nbsp; <code>no more white window</code></summary>
<br>

| Before | After | Off switch | Source |
|---|---|:-:|:-:|
| "(Not Responding)" after 5 s | loading screen stays visible | `noghost` | [`src/small_patches.c`](src/small_patches.c) |

#### The problem

During the join, the game thread stays in one frame for 10 to 15 s (Granite texture precache plus the mod controller search). After 5 s without answering window messages, Windows swaps the game window for a white "(Not Responding)" copy and may offer to close it. Keeping the UI animated on another thread isn't possible safely: the engine draws its UI only from the game thread, and Conan's loading screen is an ordinary in-game widget.

#### The fix

The DLL calls Windows' `DisableProcessWindowsGhosting` for the game, so the loading screen stays on screen during the freeze. The freeze is the same length.

#### Result

in a join with a 13 s freeze after Connect, the window stayed on the loading screen and never showed "(Not Responding)".

</details>

<details>
<summary><b>11. Occlusion hash fix</b> &nbsp;·&nbsp; <code>25% → 1.4% of render-thread time</code></summary>
<br>

| Before | After | Off switch | Source |
|---|---|:-:|:-:|
| 7.3 s of 28.7 s render-thread time in one loop | 0.34 s of 24.9 s | `noocclusion` | [`src/small_patches.c`](src/small_patches.c) |

#### The problem

Every frame the renderer checks which objects are hidden behind others (occlusion culling). It keeps a history entry for each object and each of its sub-parts in a hash table. The hash is `object id ^ (sub-part >> 20)`, and sub-part numbers are small, so all sub-parts of one object land in the same bucket. Objects made of many pieces (instanced meshes, foliage, builds) then walk a list of hundreds of entries for every lookup, every frame. After a teleport near big builds, that one loop (0x1CF95D0) was a quarter of the render thread's time.

#### The fix

The DLL changes the hash to `object id ^ (sub-part × 0x9E3779B1)` at all 5 places the game computes it (insert, three lookups, rehash), so sub-parts spread across the table. Objects with only one part hash exactly as before. Only how entries are stored changes, never which objects get drawn.

> 🛡️ **Safety check:** All 5 places are checked byte for byte before anything is changed. If any one differs, none are patched and the log says `occlusion hash: game build differs - left alone.`

#### Result

In a teleport test near big builds, the loop went from 7.3 s of 28.7 s of render-thread time (25%) to 0.34 s of 24.9 s (1.4%).

</details>

### What's left (not fixable here)

In the last measured run (about 77 s from launch to in the world), about 23 s was waiting for the server to send your character and the world. During that time the PC was mostly idle. The rest is the game loading its own content: engine startup (about 24 s), the mod controllers themselves (about 12 s), menu setup (about 6 s) and Granite (about 4 s).

In the world, lag near big builds now comes mostly from the render thread sending draw calls through the graphics driver (about 37% of its time) and from waiting on the GPU. Neither can be patched. Lower view distance and shadow settings help there.
