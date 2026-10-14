#include "StdAfx.h"
#include "DialogMuteHook.h"
#include <windows.h>
#include <psapi.h>
#include <vector>
#include <string>
#include <algorithm>

#pragma comment(lib, "Psapi.lib")

namespace
{
    std::atomic<bool> g_bMuteDialogSounds(false);

    using MessageBeep_t = BOOL(WINAPI*)(UINT);
    using Beep_t = BOOL(WINAPI*)(DWORD, DWORD);
    using PlaySoundW_t = BOOL(WINAPI*)(LPCWSTR, HMODULE, DWORD);
    using PlaySoundA_t = BOOL(WINAPI*)(LPCSTR, HMODULE, DWORD);
    using SndPlaySoundW_t = BOOL(WINAPI*)(LPCWSTR, UINT);
    using SndPlaySoundA_t = BOOL(WINAPI*)(LPCSTR, UINT);

    using AfxMessageBoxW_t = int (WINAPI*)(LPCWSTR, UINT, UINT);
    using AfxMessageBoxA_t = int (WINAPI*)(LPCSTR, UINT, UINT);

    static MessageBeep_t g_realMessageBeep = nullptr;
    static Beep_t g_realBeep = nullptr;
    static PlaySoundW_t g_realPlaySoundW = nullptr;
    static PlaySoundA_t g_realPlaySoundA = nullptr;
    static SndPlaySoundW_t g_realSndPlaySoundW = nullptr;
    static SndPlaySoundA_t g_realSndPlaySoundA = nullptr;

    static AfxMessageBoxW_t g_realAfxMessageBoxW = nullptr;
    static AfxMessageBoxA_t g_realAfxMessageBoxA = nullptr;

    bool wcontains_nocase(const std::wstring& hay, const std::wstring& needle)
    {
        auto it = std::search(hay.begin(), hay.end(), needle.begin(), needle.end(),
            [](wchar_t a, wchar_t b) { return towlower(a) == towlower(b); });
        return it != hay.end();
    }

    BOOL WINAPI ProxyMessageBeep(UINT uType)
    {
        if (g_bMuteDialogSounds.load()) {
            OutputDebugStringW(L"[DialogMuteHook] MessageBeep suppressed\n");
            return TRUE;
        }
        if (g_realMessageBeep) return g_realMessageBeep(uType);
        return MessageBeep(uType);
    }

    BOOL WINAPI ProxyBeep(DWORD dwFreq, DWORD dwDuration)
    {
        if (g_bMuteDialogSounds.load()) {
            OutputDebugStringW(L"[DialogMuteHook] Beep suppressed\n");
            return TRUE;
        }
        if (g_realBeep) return g_realBeep(dwFreq, dwDuration);
        return Beep(dwFreq, dwDuration);
    }

    BOOL WINAPI ProxyPlaySoundW(LPCWSTR pszSound, HMODULE hmod, DWORD fdwSound)
    {
        if (g_bMuteDialogSounds.load()) {
            OutputDebugStringW(L"[DialogMuteHook] PlaySoundW suppressed\n");
            return TRUE;
        }
        if (g_realPlaySoundW) return g_realPlaySoundW(pszSound, hmod, fdwSound);
        return PlaySoundW(pszSound, hmod, fdwSound);
    }

    BOOL WINAPI ProxyPlaySoundA(LPCSTR pszSound, HMODULE hmod, DWORD fdwSound)
    {
        if (g_bMuteDialogSounds.load()) {
            OutputDebugStringW(L"[DialogMuteHook] PlaySoundA suppressed\n");
            return TRUE;
        }
        if (g_realPlaySoundA) return g_realPlaySoundA(pszSound, hmod, fdwSound);
        return PlaySoundA(pszSound, hmod, fdwSound);
    }

    BOOL WINAPI ProxySndPlaySoundW(LPCWSTR pszSound, UINT fuSound)
    {
        if (g_bMuteDialogSounds.load()) {
            OutputDebugStringW(L"[DialogMuteHook] sndPlaySoundW suppressed\n");
            return TRUE;
        }
        if (g_realSndPlaySoundW) return g_realSndPlaySoundW(pszSound, fuSound);
        return sndPlaySoundW(pszSound, fuSound);
    }

    BOOL WINAPI ProxySndPlaySoundA(LPCSTR pszSound, UINT fuSound)
    {
        if (g_bMuteDialogSounds.load()) {
            OutputDebugStringW(L"[DialogMuteHook] sndPlaySoundA suppressed\n");
            return TRUE;
        }
        if (g_realSndPlaySoundA) return g_realSndPlaySoundA(pszSound, fuSound);
        return sndPlaySoundA(pszSound, fuSound);
    }

    // Proxy for AfxMessageBox (Unicode)
    int WINAPI ProxyAfxMessageBoxW(LPCWSTR lpText, UINT nType, UINT nIDHelp)
    {
        OutputDebugStringW(L"[DialogMuteHook] AfxMessageBoxW called\n");
        // Ensure beep suppression applies for the duration of the call if mute is on.
        // We don't change mute here; this is only monitoring. The MessageBeep IAT patch will suppress the sound when needed.
        if (g_realAfxMessageBoxW) return g_realAfxMessageBoxW(lpText, nType, nIDHelp);
        // Fallback: call MessageBoxW so UI still shows.
        return MessageBoxW(nullptr, lpText, L"", nType);
    }

    // Proxy for AfxMessageBox (ANSI)
    int WINAPI ProxyAfxMessageBoxA(LPCSTR lpText, UINT nType, UINT nIDHelp)
    {
        OutputDebugStringA("[DialogMuteHook] AfxMessageBoxA called\n");
        if (g_realAfxMessageBoxA) return g_realAfxMessageBoxA(lpText, nType, nIDHelp);
        // Fallback: call MessageBoxA so UI still shows.
        return MessageBoxA(nullptr, lpText, "", nType);
    }

    bool PatchImportThunk(HMODULE hModule, const char* dllName, const char* funcName, FARPROC replacement)
    {
        if (!hModule || !dllName || !funcName || !replacement) return false;

        auto* pDos = reinterpret_cast<PIMAGE_DOS_HEADER>(hModule);
        if (pDos->e_magic != IMAGE_DOS_SIGNATURE) return false;

        auto* pNt = reinterpret_cast<PIMAGE_NT_HEADERS>(reinterpret_cast<BYTE*>(hModule) + pDos->e_lfanew);
        if (pNt->Signature != IMAGE_NT_SIGNATURE) return false;

        DWORD importRVA = pNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
        if (importRVA == 0) return false;

        auto* pImport = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(reinterpret_cast<BYTE*>(hModule) + importRVA);
        while (pImport && pImport->Name) {
            const char* currentDll = reinterpret_cast<const char*>(reinterpret_cast<BYTE*>(hModule) + pImport->Name);
            if (_stricmp(currentDll, dllName) == 0) {
                auto* pThunkOriginal = reinterpret_cast<PIMAGE_THUNK_DATA>(reinterpret_cast<BYTE*>(hModule) + pImport->OriginalFirstThunk);
                auto* pThunk = reinterpret_cast<PIMAGE_THUNK_DATA>(reinterpret_cast<BYTE*>(hModule) + pImport->FirstThunk);
                while (pThunkOriginal && pThunkOriginal->u1.AddressOfData) {
                    auto* pImportByName = reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(reinterpret_cast<BYTE*>(hModule) + pThunkOriginal->u1.AddressOfData);
                    if (pImportByName && pImportByName->Name && _stricmp(reinterpret_cast<const char*>(pImportByName->Name), funcName) == 0) {
                        DWORD oldProtect = 0;
                        if (VirtualProtect(&pThunk->u1.Function, sizeof(ULONG_PTR), PAGE_READWRITE, &oldProtect)) {
                            pThunk->u1.Function = reinterpret_cast<ULONG_PTR>(replacement);
                            DWORD dummy = 0;
                            VirtualProtect(&pThunk->u1.Function, sizeof(ULONG_PTR), oldProtect, &dummy);
                            return true;
                        }
                        return false;
                    }
                    ++pThunkOriginal;
                    ++pThunk;
                }
            }
            ++pImport;
        }
        return false;
    }

    // Patch imports for AfxMessageBox from any imported MFC DLL name (contains "mfc") in the EXE's import table
    void PatchAfxMessageBoxImportsInModule(HMODULE hModule)
    {
        if (!hModule) return;
        auto* pDos = reinterpret_cast<PIMAGE_DOS_HEADER>(hModule);
        if (pDos->e_magic != IMAGE_DOS_SIGNATURE) return;
        auto* pNt = reinterpret_cast<PIMAGE_NT_HEADERS>(reinterpret_cast<BYTE*>(hModule) + pDos->e_lfanew);
        if (pNt->Signature != IMAGE_NT_SIGNATURE) return;
        DWORD importRVA = pNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
        if (importRVA == 0) return;

        auto* pImport = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(reinterpret_cast<BYTE*>(hModule) + importRVA);
        while (pImport && pImport->Name) {
            const char* currentDll = reinterpret_cast<const char*>(reinterpret_cast<BYTE*>(hModule) + pImport->Name);
            std::string dllNameStr = currentDll ? currentDll : "";
            // Only consider imports coming from an MFC DLL (filename contains "mfc")
            if (dllNameStr.find("mfc") != std::string::npos || dllNameStr.find("MFC") != std::string::npos) {
                // Try patching both ANSI/Unicode/export names
                PatchImportThunk(hModule, currentDll, "AfxMessageBoxW", reinterpret_cast<FARPROC>(&ProxyAfxMessageBoxW));
                PatchImportThunk(hModule, currentDll, "AfxMessageBoxA", reinterpret_cast<FARPROC>(&ProxyAfxMessageBoxA));
                PatchImportThunk(hModule, currentDll, "AfxMessageBox", reinterpret_cast<FARPROC>(&ProxyAfxMessageBoxW));
            }
            ++pImport;
        }
    }

    void PatchThisProcessModuleImports()
    {
        // First, patch the EXE itself
        HMODULE hExe = GetModuleHandleW(nullptr);
        if (hExe) {
            PatchImportThunk(hExe, "user32.dll", "MessageBeep", reinterpret_cast<FARPROC>(&ProxyMessageBeep));
            PatchImportThunk(hExe, "kernel32.dll", "Beep", reinterpret_cast<FARPROC>(&ProxyBeep));
            PatchImportThunk(hExe, "winmm.dll", "PlaySoundW", reinterpret_cast<FARPROC>(&ProxyPlaySoundW));
            PatchImportThunk(hExe, "winmm.dll", "PlaySoundA", reinterpret_cast<FARPROC>(&ProxyPlaySoundA));
            PatchImportThunk(hExe, "winmm.dll", "sndPlaySoundW", reinterpret_cast<FARPROC>(&ProxySndPlaySoundW));
            PatchImportThunk(hExe, "winmm.dll", "sndPlaySoundA", reinterpret_cast<FARPROC>(&ProxySndPlaySoundA));
            // Patch AfxMessageBox imports in the EXE (if EXE imports AfxMessageBox from MFC DLL)
            PatchAfxMessageBoxImportsInModule(hExe);
        }

        // Now enumerate all loaded modules and patch those which are MFC DLLs or the EXE
        HMODULE hMods[512];
        DWORD cbNeeded = 0;
        if (EnumProcessModules(GetCurrentProcess(), hMods, sizeof(hMods), &cbNeeded)) {
            size_t count = cbNeeded / sizeof(HMODULE);
            for (size_t i = 0; i < count; ++i) {
                wchar_t modName[MAX_PATH] = {};
                if (GetModuleFileNameW(hMods[i], modName, _countof(modName))) {
                    std::wstring file = modName;
                    const wchar_t* base = wcsrchr(modName, L'\\');
                    std::wstring filename = base ? (base + 1) : file;
                    if (wcontains_nocase(filename, L"mfc") || hMods[i] == hExe) {
                        // Patch common sound APIs in MFC modules too
                        PatchImportThunk(hMods[i], "user32.dll", "MessageBeep", reinterpret_cast<FARPROC>(&ProxyMessageBeep));
                        PatchImportThunk(hMods[i], "kernel32.dll", "Beep", reinterpret_cast<FARPROC>(&ProxyBeep));
                        PatchImportThunk(hMods[i], "winmm.dll", "PlaySoundW", reinterpret_cast<FARPROC>(&ProxyPlaySoundW));
                        PatchImportThunk(hMods[i], "winmm.dll", "PlaySoundA", reinterpret_cast<FARPROC>(&ProxyPlaySoundA));
                        PatchImportThunk(hMods[i], "winmm.dll", "sndPlaySoundW", reinterpret_cast<FARPROC>(&ProxySndPlaySoundW));
                        PatchImportThunk(hMods[i], "winmm.dll", "sndPlaySoundA", reinterpret_cast<FARPROC>(&ProxySndPlaySoundA));
                        // Patch AfxMessageBox imports (if any module imports AfxMessageBox from another MFC DLL)
                        PatchAfxMessageBoxImportsInModule(hMods[i]);

                        // If this module *is* an MFC DLL, capture the real AfxMessageBox exports so our proxy can forward.
                        HMODULE hMfc = hMods[i];
                        if (hMfc) {
                            // Try Unicode then ANSI
                            FARPROC pW = GetProcAddress(hMfc, "AfxMessageBoxW");
                            FARPROC pA = GetProcAddress(hMfc, "AfxMessageBoxA");
                            FARPROC p = GetProcAddress(hMfc, "AfxMessageBox");
                            if (pW && !g_realAfxMessageBoxW) g_realAfxMessageBoxW = reinterpret_cast<AfxMessageBoxW_t>(pW);
                            if (pA && !g_realAfxMessageBoxA) g_realAfxMessageBoxA = reinterpret_cast<AfxMessageBoxA_t>(pA);
                            if (p && !g_realAfxMessageBoxW) g_realAfxMessageBoxW = reinterpret_cast<AfxMessageBoxW_t>(p);
                        }
                    }
                }
            }
        }
    }
}

void DialogMuteHook::InstallDialogMuteHook()
{
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    HMODULE hKernel = GetModuleHandleW(L"kernel32.dll");
    HMODULE hWinmm = GetModuleHandleW(L"winmm.dll");

    if (hUser) {
        g_realMessageBeep = reinterpret_cast<MessageBeep_t>(GetProcAddress(hUser, "MessageBeep"));
    }
    if (hKernel) {
        g_realBeep = reinterpret_cast<Beep_t>(GetProcAddress(hKernel, "Beep"));
    }
    if (hWinmm) {
        g_realPlaySoundW = reinterpret_cast<PlaySoundW_t>(GetProcAddress(hWinmm, "PlaySoundW"));
        g_realPlaySoundA = reinterpret_cast<PlaySoundA_t>(GetProcAddress(hWinmm, "PlaySoundA"));
        g_realSndPlaySoundW = reinterpret_cast<SndPlaySoundW_t>(GetProcAddress(hWinmm, "sndPlaySoundW"));
        g_realSndPlaySoundA = reinterpret_cast<SndPlaySoundA_t>(GetProcAddress(hWinmm, "sndPlaySoundA"));
    }

    OutputDebugStringW(L"[DialogMuteHook] InstallDialogMuteHook\n");
    PatchThisProcessModuleImports();
    OutputDebugStringW(L"[DialogMuteHook] Patched MFC / EXE / system module import table entries\n");
}

void DialogMuteHook::UninstallDialogMuteHook()
{
    g_bMuteDialogSounds.store(false);
    OutputDebugStringW(L"[DialogMuteHook] UninstallDialogMuteHook\n");
}

void DialogMuteHook::SetDialogMute(bool mute)
{
    g_bMuteDialogSounds.store(mute);
    OutputDebugStringW(mute ? L"[DialogMuteHook] Global mute ON\n" : L"[DialogMuteHook] Global mute OFF\n");
}

bool DialogMuteHook::GetDialogMute()
{
    return g_bMuteDialogSounds.load();
}
