// xinput1_3 passthrough: every export calls the real DLL from the Windows system folder, loaded on
// first use (not in DllMain). Only this one file has to be shipped.

#include "unchained.h"

static FARPROC system_xinput_proc(const char *name) {
    static HMODULE m;
    if (!m) {
        wchar_t p[MAX_PATH];
        UINT n = GetSystemDirectoryW(p, MAX_PATH);
        if (!n || n > MAX_PATH - 20) return NULL;
        wcscat(p, L"\\xinput1_3.dll");
        m = LoadLibraryW(p);
        if (!m) return NULL;
    }
    return GetProcAddress(m, name);
}
#define ORD(n) ((const char *)(ULONG_PTR)(n))
#define PASS(ret, fail, name, key, params, args) \
    ret WINAPI name params { static ret (WINAPI *f) params; if (!f) f = (ret (WINAPI *) params)system_xinput_proc(key); return f ? f args : fail; }
#define PASSV(name, key, params, args) \
    void WINAPI name params { static void (WINAPI *f) params; if (!f) f = (void (WINAPI *) params)system_xinput_proc(key); if (f) f args; }

PASS(DWORD, ERROR_DEVICE_NOT_CONNECTED, XInputGetState, "XInputGetState", (DWORD a, void *b), (a, b))
PASS(DWORD, ERROR_DEVICE_NOT_CONNECTED, XInputSetState, "XInputSetState", (DWORD a, void *b), (a, b))
PASS(DWORD, ERROR_DEVICE_NOT_CONNECTED, XInputGetCapabilities, "XInputGetCapabilities", (DWORD a, DWORD b, void *c), (a, b, c))
PASSV(XInputEnable, "XInputEnable", (BOOL a), (a))
PASS(DWORD, ERROR_DEVICE_NOT_CONNECTED, XInputGetDSoundAudioDeviceGuids, "XInputGetDSoundAudioDeviceGuids", (DWORD a, GUID *b, GUID *c), (a, b, c))
PASS(DWORD, ERROR_DEVICE_NOT_CONNECTED, XInputGetBatteryInformation, "XInputGetBatteryInformation", (DWORD a, BYTE b, void *c), (a, b, c))
PASS(DWORD, ERROR_DEVICE_NOT_CONNECTED, XInputGetKeystroke, "XInputGetKeystroke", (DWORD a, DWORD b, void *c), (a, b, c))
PASS(DWORD, ERROR_DEVICE_NOT_CONNECTED, XInputGetStateEx, ORD(100), (DWORD a, void *b), (a, b))
PASS(DWORD, ERROR_DEVICE_NOT_CONNECTED, XInputWaitForGuideButton, ORD(101), (DWORD a, DWORD b, void *c), (a, b, c))
PASS(DWORD, ERROR_DEVICE_NOT_CONNECTED, XInputCancelGuideButtonWait, ORD(102), (DWORD a), (a))
PASS(DWORD, ERROR_DEVICE_NOT_CONNECTED, XInputPowerOffController, ORD(103), (DWORD a), (a))
