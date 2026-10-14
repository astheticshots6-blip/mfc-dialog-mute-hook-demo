#include "StdAfx.h"
#include "DialogMuteHook.h"
#include "third_party/minhook/MinHook.h"
#include <winternl.h>
#include <vector>

namespace
{
    std::atomic<bool> g_bMuteDialogSounds(false);

    // Original function pointers (saved before hooking)
    using MessageBeep_t = BOOL(WINAPI*)(UINT);
    using Beep_t = BOOL(WINAPI*)(DWORD, DWORD);
    using PlaySoundW_t = BOOL(WINAPI*)(LPCWSTR, HMODULE, DWORD);
    using PlaySoundA_t = BOOL(WINAPI*)(LPCSTR, HMODULE, DWORD);
    using SndPlaySoundW_t = BOOL(WINAPI*)(LPCWSTR, UINT);
    using SndPlaySoundA_t = BOOL(WINAPI*)(LPCSTR, UINT);

    static MessageBeep_t g_origMessageBeep = nullptr;
    static Beep_t g_origBeep = nullptr;
    static PlaySoundW_t g_origPlaySoundW = nullptr;
    static PlaySoundA_t g_origPlaySoundA = nullptr;
    static SndPlaySoundW_t g_origSndPlaySoundW = nullptr;
    static SndPlaySoundA_t g_origSndPlaySoundA = nullptr;

    bool PatchImportThunk(HMODULE hModule, const char* dllName, const char* funcName, FARPROC replacement)
    {
        if (!hModule || !dllName || !funcName || !replacement) {
            return false;
        }

        auto* pDos = reinterpret_cast<PIMAGE_DOS_HEADER>(hModule);
        if (pDos->e_magic != IMAGE_DOS_SIGNATURE) {
            return false;
        }

        auto* pNt = reinterpret_cast<PIMAGE_NT_HEADERS>(reinterpret_cast<BYTE*>(hModule) + pDos->e_lfanew);
        if (pNt->Signature != IMAGE_NT_SIGNATURE) {
            return false;
        }

        if (pNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress == 0) {
            return false;
        }

        auto* pImport = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(
            reinterpret_cast<BYTE*>(hModule) + pNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);

        while (pImport && pImport->Name) {
            const char* currentDll = reinterpret_cast<const char*>(reinterpret_cast<BYTE*>(hModule) + pImport->Name);
            if (_stricmp(currentDll, dllName) == 0) {
                auto* pThunkOriginal = reinterpret_cast<PIMAGE_THUNK_DATA>(reinterpret_cast<BYTE*>(hModule) + pImport->OriginalFirstThunk);
                auto* pThunk = reinterpret_cast<PIMAGE_THUNK_DATA>(reinterpret_cast<BYTE*>(hModule) + pImport->FirstThunk);

                while (pThunkOriginal && pThunkOriginal->u1.AddressOfData) {
                    auto* pImportByName = reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(reinterpret_cast<BYTE*>(hModule) + pThunkOriginal->u1.AddressOfData);
                    if (pImportByName->Name[0] == funcName[0] && _stricmp(reinterpret_cast<const char*>(pImportByName->Name), funcName) == 0) {
                        DWORD oldProtect = 0;
                        if (VirtualProtect(&pThunk->u1.Function, sizeof(ULONG_PTR), PAGE_READWRITE, &oldProtect)) {
                            pThunk->u1.Function = reinterpret_cast<ULONG_PTR>(replacement);
                            DWORD dummy = 0;
                            VirtualProtect(&pThunk->u1.Function, sizeof(ULONG_PTR), oldProtect, &dummy);
                            return true;
                        }
                    }
                    ++pThunkOriginal;
                    ++pThunk;
                }
            }
            ++pImport;
        }

        return false;
    }

    // Proxies call the saved originals to avoid recursion.
    BOOL WINAPI ProxyMessageBeep(UINT uType)
    {
        if (g_bMuteDialogSounds.load()) return TRUE;
        if (g_origMessageBeep) return g_origMessageBeep(uType);
        return MessageBeep(uType);
    }

    BOOL WINAPI ProxyBeep(DWORD dwFreq, DWORD dwDuration)
    {
        if (g_bMuteDialogSounds.load()) return TRUE;
        if (g_origBeep) return g_origBeep(dwFreq, dwDuration);
        return Beep(dwFreq, dwDuration);
    }

    BOOL WINAPI ProxyPlaySoundW(LPCWSTR pszSound, HMODULE hmod, DWORD fdwSound)
    {
        if (g_bMuteDialogSounds.load()) return TRUE;
        if (g_origPlaySoundW) return g_origPlaySoundW(pszSound, hmod, fdwSound);
        return PlaySoundW(pszSound, hmod, fdwSound);
    }

    BOOL WINAPI ProxyPlaySoundA(LPCSTR pszSound, HMODULE hmod, DWORD fdwSound)
    {
        if (g_bMuteDialogSounds.load()) return TRUE;
        if (g_origPlaySoundA) return g_origPlaySoundA(pszSound, hmod, fdwSound);
        return PlaySoundA(pszSound, hmod, fdwSound);
    }

    BOOL WINAPI ProxySndPlaySoundW(LPCWSTR pszSound, UINT fuSound)
    {
        if (g_bMuteDialogSounds.load()) return TRUE;
        if (g_origSndPlaySoundW) return g_origSndPlaySoundW(pszSound, fuSound);
        return sndPlaySoundW(pszSound, fuSound);
    }

    BOOL WINAPI ProxySndPlaySoundA(LPCSTR pszSound, UINT fuSound)
    {
        if (g_bMuteDialogSounds.load()) return TRUE;
        if (g_origSndPlaySoundA) return g_origSndPlaySoundA(pszSound, fuSound);
        return sndPlaySoundA(pszSound, fuSound);
    }
}

void DialogMuteHook::InstallDialogMuteHook()
{
    // Save originals
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    HMODULE hKernel = GetModuleHandleW(L"kernel32.dll");
    HMODULE hWinmm = GetModuleHandleW(L"winmm.dll");

    if (hUser) {
        g_origMessageBeep = reinterpret_cast<MessageBeep_t>(GetProcAddress(hUser, "MessageBeep"));
    }
    if (hKernel) {
        g_origBeep = reinterpret_cast<Beep_t>(GetProcAddress(hKernel, "Beep"));
    }
    if (hWinmm) {
        g_origPlaySoundW = reinterpret_cast<PlaySoundW_t>(GetProcAddress(hWinmm, "PlaySoundW"));
        g_origPlaySoundA = reinterpret_cast<PlaySoundA_t>(GetProcAddress(hWinmm, "PlaySoundA"));
        g_origSndPlaySoundW = reinterpret_cast<SndPlaySoundW_t>(GetProcAddress(hWinmm, "sndPlaySoundW"));
        g_origSndPlaySoundA = reinterpret_cast<SndPlaySoundA_t>(GetProcAddress(hWinmm, "sndPlaySoundA"));
    }

    // Initialize MinHook (no-op minimal shim included in repo)
    MH_Initialize();

    // Try to create hooks (the minimal MinHook in this demo records them; PatchImportThunk does the real replacement)
    if (hUser && g_origMessageBeep) {
        MH_CreateHook(reinterpret_cast<void*>(g_origMessageBeep), reinterpret_cast<void*>(&ProxyMessageBeep), reinterpret_cast<void**>(&g_origMessageBeep));
        MH_EnableHook(reinterpret_cast<void*>(g_origMessageBeep));
    }
    if (hKernel && g_origBeep) {
        MH_CreateHook(reinterpret_cast<void*>(g_origBeep), reinterpret_cast<void*>(&ProxyBeep), reinterpret_cast<void**>(&g_origBeep));
        MH_EnableHook(reinterpret_cast<void*>(g_origBeep));
    }
    if (hWinmm && g_origPlaySoundW) {
        MH_CreateHook(reinterpret_cast<void*>(g_origPlaySoundW), reinterpret_cast<void*>(&ProxyPlaySoundW), reinterpret_cast<void**>(&g_origPlaySoundW));
        MH_EnableHook(reinterpret_cast<void*>(g_origPlaySoundW));
    }
    if (hWinmm && g_origPlaySoundA) {
        MH_CreateHook(reinterpret_cast<void*>(g_origPlaySoundA), reinterpret_cast<void*>(&ProxyPlaySoundA), reinterpret_cast<void**>(&g_origPlaySoundA));
        MH_EnableHook(reinterpret_cast<void*>(g_origPlaySoundA));
    }
    if (hWinmm && g_origSndPlaySoundW) {
        MH_CreateHook(reinterpret_cast<void*>(g_origSndPlaySoundW), reinterpret_cast<void*>(&ProxySndPlaySoundW), reinterpret_cast<void**>(&g_origSndPlaySoundW));
        MH_EnableHook(reinterpret_cast<void*>(g_origSndPlaySoundW));
    }
    if (hWinmm && g_origSndPlaySoundA) {
        MH_CreateHook(reinterpret_cast<void*>(g_origSndPlaySoundA), reinterpret_cast<void*>(&ProxySndPlaySoundA), reinterpret_cast<void**>(&g_origSndPlaySoundA));
        MH_EnableHook(reinterpret_cast<void*>(g_origSndPlaySoundA));
    }

    // Also patch our EXE import table as a fallback for calls going through the EXE's IAT.
    HMODULE hExe = GetModuleHandleW(nullptr);
    if (hExe) {
        PatchImportThunk(hExe, "user32.dll", "MessageBeep", reinterpret_cast<FARPROC>(&ProxyMessageBeep));
        PatchImportThunk(hExe, "kernel32.dll", "Beep", reinterpret_cast<FARPROC>(&ProxyBeep));
        PatchImportThunk(hExe, "winmm.dll", "PlaySoundW", reinterpret_cast<FARPROC>(&ProxyPlaySoundW));
        PatchImportThunk(hExe, "winmm.dll", "PlaySoundA", reinterpret_cast<FARPROC>(&ProxyPlaySoundA));
        PatchImportThunk(hExe, "winmm.dll", "sndPlaySoundW", reinterpret_cast<FARPROC>(&ProxySndPlaySoundW));
        PatchImportThunk(hExe, "winmm.dll", "sndPlaySoundA", reinterpret_cast<FARPROC>(&ProxySndPlaySoundA));
    }
}

void DialogMuteHook::UninstallDialogMuteHook()
{
    g_bMuteDialogSounds.store(false);

    // Disable/remove hooks via the MinHook shim
    if (g_origMessageBeep) {
        MH_DisableHook(reinterpret_cast<void*>(g_origMessageBeep));
        MH_RemoveHook(reinterpret_cast<void*>(g_origMessageBeep));
    }
    if (g_origBeep) {
        MH_DisableHook(reinterpret_cast<void*>(g_origBeep));
        MH_RemoveHook(reinterpret_cast<void*>(g_origBeep));
    }
    if (g_origPlaySoundW) {
        MH_DisableHook(reinterpret_cast<void*>(g_origPlaySoundW));
        MH_RemoveHook(reinterpret_cast<void*>(g_origPlaySoundW));
    }
    if (g_origPlaySoundA) {
        MH_DisableHook(reinterpret_cast<void*>(g_origPlaySoundA));
        MH_RemoveHook(reinterpret_cast<void*>(g_origPlaySoundA));
    }
    if (g_origSndPlaySoundW) {
        MH_DisableHook(reinterpret_cast<void*>(g_origSndPlaySoundW));
        MH_RemoveHook(reinterpret_cast<void*>(g_origSndPlaySoundW));
    }
    if (g_origSndPlaySoundA) {
        MH_DisableHook(reinterpret_cast<void*>(g_origSndPlaySoundA));
        MH_RemoveHook(reinterpret_cast<void*>(g_origSndPlaySoundA));
    }

    MH_Uninitialize();
}

void DialogMuteHook::SetDialogMute(bool mute)
{
    g_bMuteDialogSounds.store(mute);
}

bool DialogMuteHook::GetDialogMute()
{
    return g_bMuteDialogSounds.load();
}
