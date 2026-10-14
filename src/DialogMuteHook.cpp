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
    static MessageBeep_t g_realMessageBeep = nullptr;

    // Simple helper: case-insensitive wstring contains
    bool wcontains_nocase(const std::wstring& hay, const std::wstring& needle)
    {
        auto it = std::search(
            hay.begin(), hay.end(),
            needle.begin(), needle.end(),
            [](wchar_t a, wchar_t b){ return towlower(a) == towlower(b); });
        return it != hay.end();
    }

    // Proxy that will be placed into the IAT of the MFC module so AfxMessageBox -> MessageBeep calls land here.
    BOOL WINAPI ProxyMessageBeep(UINT uType)
    {
        if (g_bMuteDialogSounds.load()) {
            OutputDebugStringW(L"[DialogMuteHook] ProxyMessageBeep: suppressed\n");
            return TRUE;
        }
        if (g_realMessageBeep) {
            return g_realMessageBeep(uType);
        }
        return MessageBeep(uType);
    }

    // Patch the import thunk for a single module
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
}

void DialogMuteHook::InstallDialogMuteHook()
{
    // Resolve the real MessageBeep function from user32
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    if (hUser) {
        g_realMessageBeep = reinterpret_cast<MessageBeep_t>(GetProcAddress(hUser, "MessageBeep"));
    }

    // Enumerate loaded modules and patch those that look like MFC DLLs ("mfc" in filename)
    HMODULE hMods[512];
    DWORD cbNeeded = 0;
    if (EnumProcessModules(GetCurrentProcess(), hMods, sizeof(hMods), &cbNeeded)) {
        size_t count = cbNeeded / sizeof(HMODULE);
        for (size_t i = 0; i < count; ++i) {
            wchar_t modName[MAX_PATH];
            if (GetModuleFileNameW(hMods[i], modName, _countof(modName))) {
                std::wstring name(modName);
                // check whether filename contains "\mfc" or starts with mfc in filename
                // we check the module file name component only
                const wchar_t* base = wcsrchr(modName, L'\\');
                std::wstring filename = base ? (base + 1) : name;
                if (wcontains_nocase(filename, L"mfc")) {
                    // Try to patch this module's IAT for MessageBeep
                    BOOL ok = PatchImportThunk(hMods[i], "user32.dll", "MessageBeep", reinterpret_cast<FARPROC>(&ProxyMessageBeep));
                    if (ok) {
                        OutputDebugStringW(L"[DialogMuteHook] Patched MFC module IAT for MessageBeep\n");
                    } else {
                        // Not fatal; continue scanning
                        OutputDebugStringW(L"[DialogMuteHook] Failed to patch MFC module IAT (MessageBeep)\n");
                    }
                }
            }
        }
    } else {
        OutputDebugStringW(L"[DialogMuteHook] EnumProcessModules failed\n");
    }
}

void DialogMuteHook::UninstallDialogMuteHook()
{
    // For demo simplicity we do not restore original IAT entries.
    // We only clear mute state.
    g_bMuteDialogSounds.store(false);
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
