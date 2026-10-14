#include "DialogMuteHook.h"
#include <MinHook.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

using namespace DialogMuteHook;

static std::atomic<bool> g_bMuteDialogSounds(false);

typedef BOOL (WINAPI *MessageBeep_t)(UINT);
static MessageBeep_t fpMessageBeep = nullptr;

typedef BOOL (WINAPI *Beep_t)(DWORD, DWORD);
static Beep_t fpBeep = nullptr;

typedef BOOL (WINAPI *PlaySoundW_t)(LPCWSTR, HMODULE, DWORD);
static PlaySoundW_t fpPlaySoundW = nullptr;

typedef BOOL (WINAPI *PlaySoundA_t)(LPCSTR, HMODULE, DWORD);
static PlaySoundA_t fpPlaySoundA = nullptr;

typedef BOOL (WINAPI *sndPlaySoundW_t)(LPCWSTR, UINT);
static sndPlaySoundW_t fpsndPlaySoundW = nullptr;

typedef BOOL (WINAPI *sndPlaySoundA_t)(LPCSTR, UINT);
static sndPlaySoundA_t fpsndPlaySoundA = nullptr;

BOOL WINAPI HookedMessageBeep(UINT uType) {
    if (g_bMuteDialogSounds.load()) return TRUE;
    return fpMessageBeep ? fpMessageBeep(uType) : FALSE;
}

BOOL WINAPI HookedBeep(DWORD dwFreq, DWORD dwDuration) {
    if (g_bMuteDialogSounds.load()) return TRUE;
    return fpBeep ? fpBeep(dwFreq, dwDuration) : FALSE;
}

BOOL WINAPI HookedPlaySoundW(LPCWSTR pszSound, HMODULE hmod, DWORD fdwSound) {
    if (g_bMuteDialogSounds.load()) return TRUE;
    return fpPlaySoundW ? fpPlaySoundW(pszSound, hmod, fdwSound) : FALSE;
}

BOOL WINAPI HookedPlaySoundA(LPCSTR pszSound, HMODULE hmod, DWORD fdwSound) {
    if (g_bMuteDialogSounds.load()) return TRUE;
    return fpPlaySoundA ? fpPlaySoundA(pszSound, hmod, fdwSound) : FALSE;
}

BOOL WINAPI HooksndPlaySoundW(LPCWSTR pszSound, UINT fuSound) {
    if (g_bMuteDialogSounds.load()) return TRUE;
    return fpsndPlaySoundW ? fpsndPlaySoundW(pszSound, fuSound) : FALSE;
}

BOOL WINAPI HooksndPlaySoundA(LPCSTR pszSound, UINT fuSound) {
    if (g_bMuteDialogSounds.load()) return TRUE;
    return fpsndPlaySoundA ? fpsndPlaySoundA(pszSound, fuSound) : FALSE;
}

void InstallDialogMuteHook() {
    if (MH_Initialize() != MH_OK) {
        return;
    }
    MH_CreateHookApi(L"user32", "MessageBeep", &HookedMessageBeep, reinterpret_cast<void**>(&fpMessageBeep));
    MH_CreateHookApi(L"kernel32", "Beep", &HookedBeep, reinterpret_cast<void**>(&fpBeep));
    MH_CreateHookApi(L"winmm", "PlaySoundW", &HookedPlaySoundW, reinterpret_cast<void**>(&fpPlaySoundW));
    MH_CreateHookApi(L"winmm", "PlaySoundA", &HookedPlaySoundA, reinterpret_cast<void**>(&fpPlaySoundA));
    MH_CreateHookApi(L"winmm", "sndPlaySoundW", &HooksndPlaySoundW, reinterpret_cast<void**>(&fpsndPlaySoundW));
    MH_CreateHookApi(L"winmm", "sndPlaySoundA", &HooksndPlaySoundA, reinterpret_cast<void**>(&fpsndPlaySoundA));

    MH_EnableHook(MH_ALL_HOOKS);
}

void UninstallDialogMuteHook() {
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
}

void SetDialogMute(bool mute) { g_bMuteDialogSounds.store(mute); }
bool GetDialogMute() { return g_bMuteDialogSounds.load(); }
