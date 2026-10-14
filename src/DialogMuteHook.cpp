#include "DialogMuteHook.h"
#include <winternl.h>
#include <vector>

namespace
{
    std::atomic<bool> g_bMuteDialogSounds(false);

    // Callback to mutate the process import table for a single DLL function.
    // This is intentionally lightweight and suitable for demo use.
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

    BOOL WINAPI ProxyMessageBeep(UINT uType)
    {
        return g_bMuteDialogSounds.load() ? TRUE : MessageBeep(uType);
    }

    BOOL WINAPI ProxyBeep(DWORD dwFreq, DWORD dwDuration)
    {
        return g_bMuteDialogSounds.load() ? TRUE : Beep(dwFreq, dwDuration);
    }

    BOOL WINAPI ProxyPlaySoundW(LPCWSTR pszSound, HMODULE hmod, DWORD fdwSound)
    {
        return g_bMuteDialogSounds.load() ? TRUE : PlaySoundW(pszSound, hmod, fdwSound);
    }

    BOOL WINAPI ProxyPlaySoundA(LPCSTR pszSound, HMODULE hmod, DWORD fdwSound)
    {
        return g_bMuteDialogSounds.load() ? TRUE : PlaySoundA(pszSound, hmod, fdwSound);
    }

    BOOL WINAPI ProxySndPlaySoundW(LPCWSTR pszSound, UINT fuSound)
    {
        return g_bMuteDialogSounds.load() ? TRUE : sndPlaySoundW(pszSound, fuSound);
    }

    BOOL WINAPI ProxySndPlaySoundA(LPCSTR pszSound, UINT fuSound)
    {
        return g_bMuteDialogSounds.load() ? TRUE : sndPlaySoundA(pszSound, fuSound);
    }
}

void DialogMuteHook::InstallDialogMuteHook()
{
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    HMODULE hKernel = GetModuleHandleW(L"kernel32.dll");
    HMODULE hWinmm = GetModuleHandleW(L"winmm.dll");

    if (hUser) {
        PatchImportThunk((HMODULE)GetModuleHandleW(NULL), "user32.dll", "MessageBeep", reinterpret_cast<FARPROC>(&ProxyMessageBeep));
    }
    if (hKernel) {
        PatchImportThunk((HMODULE)GetModuleHandleW(NULL), "kernel32.dll", "Beep", reinterpret_cast<FARPROC>(&ProxyBeep));
    }
    if (hWinmm) {
        PatchImportThunk((HMODULE)GetModuleHandleW(NULL), "winmm.dll", "PlaySoundW", reinterpret_cast<FARPROC>(&ProxyPlaySoundW));
        PatchImportThunk((HMODULE)GetModuleHandleW(NULL), "winmm.dll", "PlaySoundA", reinterpret_cast<FARPROC>(&ProxyPlaySoundA));
        PatchImportThunk((HMODULE)GetModuleHandleW(NULL), "winmm.dll", "sndPlaySoundW", reinterpret_cast<FARPROC>(&ProxySndPlaySoundW));
        PatchImportThunk((HMODULE)GetModuleHandleW(NULL), "winmm.dll", "sndPlaySoundA", reinterpret_cast<FARPROC>(&ProxySndPlaySoundA));
    }
}

void DialogMuteHook::UninstallDialogMuteHook()
{
    // For demo simplicity this is a process-wide runtime mute toggle.
    // A production version should save/restore original thunks.
    g_bMuteDialogSounds.store(false);
}

void DialogMuteHook::SetDialogMute(bool mute)
{
    g_bMuteDialogSounds.store(mute);
}

bool DialogMuteHook::GetDialogMute()
{
    return g_bMuteDialogSounds.load();
}
