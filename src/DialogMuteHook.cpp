#include "StdAfx.h"
#include "DialogMuteHook.h"
#include <windows.h>
#include <psapi.h>
#include <string>
#include <algorithm>
#include <thread>
#include <chrono>
#include <vector>
#include <mutex>

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

    using MessageBoxW_t = int (WINAPI*)(HWND, LPCWSTR, LPCWSTR, UINT);
    using MessageBoxA_t = int (WINAPI*)(HWND, LPCSTR, LPCSTR, UINT);
    using MessageBoxIndirectW_t = int (WINAPI*)(const MSGBOXPARAMSW*);
    using MessageBoxIndirectA_t = int (WINAPI*)(const MSGBOXPARAMSA*);

    using AfxMessageBoxW_t = int (WINAPI*)(LPCWSTR, UINT, UINT);
    using AfxMessageBoxA_t = int (WINAPI*)(LPCSTR, UINT, UINT);

    static MessageBeep_t g_realMessageBeep = nullptr;
    static Beep_t g_realBeep = nullptr;
    static PlaySoundW_t g_realPlaySoundW = nullptr;
    static PlaySoundA_t g_realPlaySoundA = nullptr;
    static SndPlaySoundW_t g_realSndPlaySoundW = nullptr;
    static SndPlaySoundA_t g_realSndPlaySoundA = nullptr;

    static MessageBoxW_t g_realMessageBoxW = nullptr;
    static MessageBoxA_t g_realMessageBoxA = nullptr;
    static MessageBoxIndirectW_t g_realMessageBoxIndirectW = nullptr;
    static MessageBoxIndirectA_t g_realMessageBoxIndirectA = nullptr;

    static AfxMessageBoxW_t g_realAfxMessageBoxW = nullptr;
    static AfxMessageBoxA_t g_realAfxMessageBoxA = nullptr;

    // Inline hook bookkeeping
    struct InlineHookRecord {
        void* target = nullptr;
        void* detour = nullptr;
        void* trampoline = nullptr;
        std::vector<BYTE> originalBytes;
    };

    static std::vector<InlineHookRecord> g_inlineHooks;
    static std::mutex g_hooksMutex;

    static std::thread g_patchThread;
    static std::atomic<bool> g_stopPatchThread(false);

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

    int WINAPI ProxyMessageBoxW(HWND hWnd, LPCWSTR lpText, LPCWSTR lpCaption, UINT uType)
    {
        OutputDebugStringW(L"[DialogMuteHook] MessageBoxW called\n");
        if (g_realMessageBoxW) return g_realMessageBoxW(hWnd, lpText, lpCaption, uType);
        return MessageBoxW(hWnd, lpText, lpCaption, uType);
    }

    int WINAPI ProxyMessageBoxA(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType)
    {
        OutputDebugStringA("[DialogMuteHook] MessageBoxA called\n");
        if (g_realMessageBoxA) return g_realMessageBoxA(hWnd, lpText, lpCaption, uType);
        return MessageBoxA(hWnd, lpText, lpCaption, uType);
    }

    int WINAPI ProxyMessageBoxIndirectW(const MSGBOXPARAMSW* pMsgBoxParams)
    {
        OutputDebugStringW(L"[DialogMuteHook] MessageBoxIndirectW called\n");
        if (g_realMessageBoxIndirectW) return g_realMessageBoxIndirectW(pMsgBoxParams);
        return MessageBoxIndirectW(pMsgBoxParams);
    }

    int WINAPI ProxyMessageBoxIndirectA(const MSGBOXPARAMSA* pMsgBoxParams)
    {
        OutputDebugStringA("[DialogMuteHook] MessageBoxIndirectA called\n");
        if (g_realMessageBoxIndirectA) return g_realMessageBoxIndirectA(pMsgBoxParams);
        return MessageBoxIndirectA(pMsgBoxParams);
    }

    int WINAPI ProxyAfxMessageBoxW(LPCWSTR lpText, UINT nType, UINT nIDHelp)
    {
        OutputDebugStringW(L"[DialogMuteHook] AfxMessageBoxW called\n");
        if (g_bMuteDialogSounds.load()) {
            OutputDebugStringW(L"[DialogMuteHook] AfxMessageBoxW: suppressing sound via global mute\n");
        }
        if (g_realAfxMessageBoxW) return g_realAfxMessageBoxW(lpText, nType, nIDHelp);
        return MessageBoxW(nullptr, lpText, L"", nType);
    }

    int WINAPI ProxyAfxMessageBoxA(LPCSTR lpText, UINT nType, UINT nIDHelp)
    {
        OutputDebugStringA("[DialogMuteHook] AfxMessageBoxA called\n");
        if (g_bMuteDialogSounds.load()) {
            OutputDebugStringA("[DialogMuteHook] AfxMessageBoxA: suppressing sound via global mute\n");
        }
        if (g_realAfxMessageBoxA) return g_realAfxMessageBoxA(lpText, nType, nIDHelp);
        return MessageBoxA(nullptr, lpText, "", nType);
    }

    bool InstallInlineHook(void* target, void* detour, void** outTrampoline)
    {
        if (!target || !detour || !outTrampoline) return false;

        std::lock_guard<std::mutex> lock(g_hooksMutex);
        for (const auto& rec : g_inlineHooks) {
            if (rec.target == target) {
                *outTrampoline = rec.trampoline;
                return true;
            }
        }

        BYTE original[32] = {0};
#ifdef _M_X64
        const SIZE_T needed = 12;
#else
        const SIZE_T needed = 5;
#endif

        DWORD oldProt = 0;
        if (!VirtualProtect(target, needed, PAGE_EXECUTE_READWRITE, &oldProt)) return false;
        memcpy(original, target, needed);

        void* tramp = VirtualAlloc(nullptr, needed + 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!tramp) {
            VirtualProtect(target, needed, oldProt, &oldProt);
            return false;
        }
        memcpy(tramp, original, needed);

        BYTE* pJumpBack = reinterpret_cast<BYTE*>(tramp) + needed;
#ifdef _M_X64
        pJumpBack[0] = 0x48; pJumpBack[1] = 0xB8;
        uintptr_t retAddr = reinterpret_cast<uintptr_t>(reinterpret_cast<BYTE*>(target) + needed);
        memcpy(pJumpBack + 2, &retAddr, sizeof(retAddr));
        pJumpBack[10] = 0xFF; pJumpBack[11] = 0xE0;
#else
        pJumpBack[0] = 0xE9;
        uintptr_t rel = (uintptr_t)(reinterpret_cast<BYTE*>(target) + needed) - (uintptr_t)(pJumpBack) - 5;
        memcpy(pJumpBack + 1, &rel, 4);
#endif

#ifdef _M_X64
        BYTE patch[12];
        patch[0] = 0x48; patch[1] = 0xB8;
        uintptr_t detAddr = reinterpret_cast<uintptr_t>(detour);
        memcpy(patch + 2, &detAddr, sizeof(detAddr));
        patch[10] = 0xFF; patch[11] = 0xE0;
        memcpy(target, patch, sizeof(patch));
#else
        BYTE patch[5];
        patch[0] = 0xE9;
        uintptr_t relToDet = (uintptr_t)detour - (uintptr_t)target - 5;
        memcpy(patch + 1, &relToDet, 4);
        memcpy(target, patch, sizeof(patch));
#endif

        DWORD tmp = 0;
        VirtualProtect(target, needed, oldProt, &tmp);

        InlineHookRecord rec;
        rec.target = target;
        rec.detour = detour;
        rec.trampoline = tramp;
        rec.originalBytes.assign(original, original + needed);
        g_inlineHooks.push_back(rec);

        *outTrampoline = tramp;
        return true;
    }

    void RemoveAllInlineHooks()
    {
        std::lock_guard<std::mutex> lock(g_hooksMutex);
        for (const auto& rec : g_inlineHooks) {
            if (!rec.target) continue;
            DWORD old = 0;
            if (VirtualProtect(rec.target, rec.originalBytes.size(), PAGE_EXECUTE_READWRITE, &old)) {
                memcpy(rec.target, rec.originalBytes.data(), rec.originalBytes.size());
                DWORD tmp = 0;
                VirtualProtect(rec.target, rec.originalBytes.size(), old, &tmp);
            }
            if (rec.trampoline) VirtualFree(rec.trampoline, 0, MEM_RELEASE);
        }
        g_inlineHooks.clear();
    }

    bool TryHookExport(HMODULE hModule, const char* exportName, void* detour, void** originalAddress)
    {
        if (!hModule || !exportName || !detour || !originalAddress) return false;
        FARPROC p = GetProcAddress(hModule, exportName);
        if (!p) return false;
        void* tramp = nullptr;
        if (!InstallInlineHook(reinterpret_cast<void*>(p), detour, &tramp)) return false;
        *originalAddress = reinterpret_cast<void*>(p);
        return true;
    }

    // Enumerate exports and try to find any symbol containing "AfxMessageBox" then hook it.
    void TryHookAfxByScanningExports(HMODULE hModule)
    {
        if (!hModule) return;
        auto* pDos = reinterpret_cast<PIMAGE_DOS_HEADER>(hModule);
        if (pDos->e_magic != IMAGE_DOS_SIGNATURE) return;
        auto* pNt = reinterpret_cast<PIMAGE_NT_HEADERS>(reinterpret_cast<BYTE*>(hModule) + pDos->e_lfanew);
        DWORD exportRVA = pNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
        if (exportRVA == 0) return;
        auto* pExport = reinterpret_cast<PIMAGE_EXPORT_DIRECTORY>(reinterpret_cast<BYTE*>(hModule) + exportRVA);
        DWORD* namesRVA = reinterpret_cast<DWORD*>(reinterpret_cast<BYTE*>(hModule) + pExport->AddressOfNames);
        WORD* ordinals = reinterpret_cast<WORD*>(reinterpret_cast<BYTE*>(hModule) + pExport->AddressOfNameOrdinals);
        DWORD nameCount = pExport->NumberOfNames;
        char buf[512];
        for (DWORD i = 0; i < nameCount; ++i) {
            const char* name = reinterpret_cast<const char*>(reinterpret_cast<BYTE*>(hModule) + namesRVA[i]);
            if (strstr(name, "AfxMessageBox") != nullptr) {
                sprintf_s(buf, "[DialogMuteHook] Found export name: %s\n", name);
                OutputDebugStringA(buf);
                void* tramp = nullptr;
                if (InstallInlineHook(reinterpret_cast<void*>(GetProcAddress(hModule, name)), reinterpret_cast<void*>(&ProxyAfxMessageBoxW), &tramp)) {
                    // assume Unicode version if we hooked a mangled name; store original
                    g_realAfxMessageBoxW = reinterpret_cast<AfxMessageBoxW_t>(GetProcAddress(hModule, name));
                    sprintf_s(buf, "[DialogMuteHook] Hooked export symbol %s at %p\n", name, GetProcAddress(hModule, name));
                    OutputDebugStringA(buf);
                }
            }
        }
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
                    if (pThunkOriginal->u1.AddressOfData & IMAGE_ORDINAL_FLAG) {
                        ++pThunkOriginal;
                        ++pThunk;
                        continue;
                    }

                    auto* pImportByName = reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(reinterpret_cast<BYTE*>(hModule) + pThunkOriginal->u1.AddressOfData);
                    if (!pImportByName || !pImportByName->Name) {
                        ++pThunkOriginal;
                        ++pThunk;
                        continue;
                    }

                    const char* importedName = reinterpret_cast<const char*>(pImportByName->Name);
                    if (_stricmp(importedName, funcName) == 0) {
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

    void PatchAllSoundHooksInModule(HMODULE hModule)
    {
        if (!hModule) return;

        PatchImportThunk(hModule, "user32.dll", "MessageBeep", reinterpret_cast<FARPROC>(&ProxyMessageBeep));
        PatchImportThunk(hModule, "kernel32.dll", "Beep", reinterpret_cast<FARPROC>(&ProxyBeep));
        PatchImportThunk(hModule, "winmm.dll", "PlaySoundW", reinterpret_cast<FARPROC>(&ProxyPlaySoundW));
        PatchImportThunk(hModule, "winmm.dll", "PlaySoundA", reinterpret_cast<FARPROC>(&ProxyPlaySoundA));
        PatchImportThunk(hModule, "winmm.dll", "sndPlaySoundW", reinterpret_cast<FARPROC>(&ProxySndPlaySoundW));
        PatchImportThunk(hModule, "winmm.dll", "sndPlaySoundA", reinterpret_cast<FARPROC>(&ProxySndPlaySoundA));

        PatchImportThunk(hModule, "user32.dll", "MessageBoxW", reinterpret_cast<FARPROC>(&ProxyMessageBoxW));
        PatchImportThunk(hModule, "user32.dll", "MessageBoxA", reinterpret_cast<FARPROC>(&ProxyMessageBoxA));
        PatchImportThunk(hModule, "user32.dll", "MessageBoxIndirectW", reinterpret_cast<FARPROC>(&ProxyMessageBoxIndirectW));
        PatchImportThunk(hModule, "user32.dll", "MessageBoxIndirectA", reinterpret_cast<FARPROC>(&ProxyMessageBoxIndirectA));
    }

    void PatchTargetModulesOnce()
    {
        HMODULE hExe = GetModuleHandleW(nullptr);
        if (hExe) {
            PatchAllSoundHooksInModule(hExe);
        }

        HMODULE hMods[512];
        DWORD cbNeeded = 0;
        if (EnumProcessModules(GetCurrentProcess(), hMods, sizeof(hMods), &cbNeeded)) {
            size_t count = cbNeeded / sizeof(HMODULE);
            for (size_t i = 0; i < count; ++i) {
                wchar_t modName[MAX_PATH] = {};
                if (GetModuleFileNameW(hMods[i], modName, _countof(modName))) {
                    std::wstring filename = modName;
                    const wchar_t* base = wcsrchr(modName, L'\\');
                    if (base) filename = base + 1;

                    if (wcontains_nocase(filename, L"mfc") || hMods[i] == hExe) {
                        PatchAllSoundHooksInModule(hMods[i]);
                    }
                }
            }
        }
    }

    void EnsureInlineHooksInstalled()
    {
        HMODULE hUser = GetModuleHandleW(L"user32.dll");
        HMODULE hKernel = GetModuleHandleW(L"kernel32.dll");
        HMODULE hWinmm = GetModuleHandleW(L"winmm.dll");

        void* tramp = nullptr;
        if (hUser) {
            void* pMessageBeep = reinterpret_cast<void*>(GetProcAddress(hUser, "MessageBeep"));
            if (pMessageBeep) InstallInlineHook(pMessageBeep, reinterpret_cast<void*>(&ProxyMessageBeep), &tramp);
        }
        if (hKernel) {
            void* pBeep = reinterpret_cast<void*>(GetProcAddress(hKernel, "Beep"));
            if (pBeep) InstallInlineHook(pBeep, reinterpret_cast<void*>(&ProxyBeep), &tramp);
        }
        if (hWinmm) {
            void* pPlaySoundW = reinterpret_cast<void*>(GetProcAddress(hWinmm, "PlaySoundW"));
            if (pPlaySoundW) InstallInlineHook(pPlaySoundW, reinterpret_cast<void*>(&ProxyPlaySoundW), &tramp);
            void* pPlaySoundA = reinterpret_cast<void*>(GetProcAddress(hWinmm, "PlaySoundA"));
            if (pPlaySoundA) InstallInlineHook(pPlaySoundA, reinterpret_cast<void*>(&ProxyPlaySoundA), &tramp);
            void* pSndPlaySoundW = reinterpret_cast<void*>(GetProcAddress(hWinmm, "sndPlaySoundW"));
            if (pSndPlaySoundW) InstallInlineHook(pSndPlaySoundW, reinterpret_cast<void*>(&ProxySndPlaySoundW), &tramp);
            void* pSndPlaySoundA = reinterpret_cast<void*>(GetProcAddress(hWinmm, "sndPlaySoundA"));
            if (pSndPlaySoundA) InstallInlineHook(pSndPlaySoundA, reinterpret_cast<void*>(&ProxySndPlaySoundA), &tramp);
        }
    }

    void PatchLoop()
    {
        PatchTargetModulesOnce();
        EnsureInlineHooksInstalled();

        std::vector<HMODULE> seen;
        HMODULE hMods[1024];
        while (!g_stopPatchThread.load()) {
            DWORD cbNeeded = 0;
            if (EnumProcessModules(GetCurrentProcess(), hMods, sizeof(hMods), &cbNeeded)) {
                size_t count = cbNeeded / sizeof(HMODULE);
                for (size_t i = 0; i < count; ++i) {
                    HMODULE m = hMods[i];
                    if (std::find(seen.begin(), seen.end(), m) == seen.end()) {
                        wchar_t modName[MAX_PATH] = {};
                        if (GetModuleFileNameW(m, modName, _countof(modName))) {
                            std::wstring filename = modName;
                            const wchar_t* base = wcsrchr(modName, L'\\');
                            if (base) filename = base + 1;
                            if (wcontains_nocase(filename, L"mfc")) {
                                OutputDebugStringW(L"[DialogMuteHook] Detected new MFC module, patching\n");
                                PatchAllSoundHooksInModule(m);
                                // try hooking exports
                                TryHookExport(m, "AfxMessageBoxW", reinterpret_cast<void*>(&ProxyAfxMessageBoxW), reinterpret_cast<void**>(&g_realAfxMessageBoxW));
                                TryHookExport(m, "AfxMessageBoxA", reinterpret_cast<void*>(&ProxyAfxMessageBoxA), reinterpret_cast<void**>(&g_realAfxMessageBoxA));
                                TryHookExport(m, "AfxMessageBox", reinterpret_cast<void*>(&ProxyAfxMessageBoxW), reinterpret_cast<void**>(&g_realAfxMessageBoxW));
                                // fallback: scan exports for mangled names
                                TryHookAfxByScanningExports(m);
                            }
                        }
                        seen.push_back(m);
                    }
                }
            }
            EnsureInlineHooksInstalled();
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
        }
    }
}

void DialogMuteHook::InstallDialogMuteHook()
{
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    HMODULE hKernel = GetModuleHandleW(L"kernel32.dll");
    HMODULE hWinmm = GetModuleHandleW(L"winmm.dll");

    if (hUser) g_realMessageBeep = reinterpret_cast<MessageBeep_t>(GetProcAddress(hUser, "MessageBeep"));
    if (hKernel) g_realBeep = reinterpret_cast<Beep_t>(GetProcAddress(hKernel, "Beep"));
    if (hWinmm) {
        g_realPlaySoundW = reinterpret_cast<PlaySoundW_t>(GetProcAddress(hWinmm, "PlaySoundW"));
        g_realPlaySoundA = reinterpret_cast<PlaySoundA_t>(GetProcAddress(hWinmm, "PlaySoundA"));
        g_realSndPlaySoundW = reinterpret_cast<SndPlaySoundW_t>(GetProcAddress(hWinmm, "sndPlaySoundW"));
        g_realSndPlaySoundA = reinterpret_cast<SndPlaySoundA_t>(GetProcAddress(hWinmm, "sndPlaySoundA"));
    }

    OutputDebugStringW(L"[DialogMuteHook] InstallDialogMuteHook\n");
    g_stopPatchThread.store(false);
    g_patchThread = std::thread(PatchLoop);
}

void DialogMuteHook::UninstallDialogMuteHook()
{
    OutputDebugStringW(L"[DialogMuteHook] UninstallDialogMuteHook\n");
    g_stopPatchThread.store(true);
    if (g_patchThread.joinable()) g_patchThread.join();
    RemoveAllInlineHooks();
    g_bMuteDialogSounds.store(false);
}

void DialogMuteHook::SetDialogMute(bool mute)
{
    g_bMuteDialogSounds.store(mute);
    OutputDebugStringW(mute ? L"[DialogMuteHook] Global mute ON\n" : L"[DialogMuteHook] Global mute OFF\n");
}

void DialogMuteHook::UninstallDialogMuteHook()
{
    OutputDebugStringW(L"[DialogMuteHook] UninstallDialogMuteHook\n");
    g_stopPatchThread.store(true);
    if (g_patchThread.joinable()) g_patchThread.join();
    RemoveAllInlineHooks();
    g_bMuteDialogSounds.store(false);
}

bool DialogMuteHook::GetDialogMute()
{
    return g_bMuteDialogSounds.load();
}
