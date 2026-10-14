#include "StdAfx.h"
#include "TrayThread.h"
#include "DialogMuteHook.h"
#include <windows.h>
#include <shellapi.h>
#include <strsafe.h>
#include <atomic>

#pragma comment(lib, "shell32.lib")

// Messages and commands
#define WM_TRAY_NOTIFY     (WM_APP + 100)
#define WM_TRAY_CMD_TO_MAIN (WM_APP + 101)
#define WM_TRAY_SHUTDOWN   (WM_APP + 102)

#define TRAY_CMD_TOGGLE_MUTE 1
#define TRAY_CMD_EXIT        2

// Resource IDs - use resource icons in resource.rc
#ifndef IDI_SOUND_ON
#define IDI_SOUND_ON 101
#endif
#ifndef IDI_SOUND_OFF
#define IDI_SOUND_OFF 102
#endif

namespace {
    // Thread state
    HANDLE g_trayThreadHandle = nullptr;
    DWORD  g_trayThreadId = 0;
    HWND   g_trayWnd = nullptr;
    HWND   g_mainWnd = nullptr;
    HANDLE g_startEvent = nullptr; // signaled when tray window created
    std::atomic<bool> g_shouldExit{ false };

    // NOTIFYICONDATA stored here so Update can access it
    NOTIFYICONDATAW g_nid = {};

    // Internal atomic mute state (fallback if you don't use a separate hook module)
    static std::atomic<bool> g_fallbackMute{ false };
    inline void SetMuteInternal(bool m) { g_fallbackMute.store(m); }
    inline bool GetMuteInternal() { return g_fallbackMute.load(); }

    // Utility: load icon by resource id or fallback to IDI_APPLICATION
    static HICON LoadAppIconOrFallback(int resId) {
        HICON h = nullptr;
        HINSTANCE hInst = GetModuleHandleW(NULL);
        if (hInst) h = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(resId), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
        if (!h) h = (HICON)LoadImageW(NULL, MAKEINTRESOURCEW(IDI_APPLICATION), IMAGE_ICON, 16, 16, LR_SHARED);
        return h;
    }

    // Update tray icon and tooltip based on current state
    void UpdateTrayIcon() {
        if (!g_trayWnd) return;
        bool muted = DialogMuteHook::GetDialogMute();
        HICON hIcon = LoadAppIconOrFallback(muted ? IDI_SOUND_OFF : IDI_SOUND_ON);
        if (g_nid.hIcon && g_nid.hIcon != hIcon) {
            DestroyIcon(g_nid.hIcon);
            g_nid.hIcon = nullptr;
        }
        g_nid.hIcon = hIcon;
        StringCchPrintfW(g_nid.szTip, ARRAYSIZE(g_nid.szTip), L"Dialog sound: %s", muted ? L"Muted" : L"On");
        g_nid.uFlags = NIF_ICON | NIF_TIP;
        Shell_NotifyIconW(NIM_MODIFY, &g_nid);
    }

    // Show context menu on tray thread
    void ShowTrayMenu() {
        HMENU hMenu = CreatePopupMenu();
        if (!hMenu) return;
        bool muted = DialogMuteHook::GetDialogMute();
        // use ASCII/English labels to avoid source-encoding issues in some toolchains
        InsertMenuW(hMenu, 0, MF_BYPOSITION | MF_STRING, TRAY_CMD_TOGGLE_MUTE, muted ? L"Unmute" : L"Mute sound");
        InsertMenuW(hMenu, 1, MF_BYPOSITION | MF_STRING, TRAY_CMD_EXIT, L"Exit");

        POINT pt;
        GetCursorPos(&pt);
        SetForegroundWindow(g_trayWnd); // important
        UINT cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_trayWnd, NULL);
        PostMessageW(g_trayWnd, WM_NULL, 0, 0); // required trick
        if (cmd == TRAY_CMD_TOGGLE_MUTE) {
            bool newMute = !DialogMuteHook::GetDialogMute();
            DialogMuteHook::SetDialogMute(newMute);
            UpdateTrayIcon();
            if (g_mainWnd) PostMessageW(g_mainWnd, WM_TRAY_CMD_TO_MAIN, TRAY_CMD_TOGGLE_MUTE, newMute ? 1 : 0);
        } else if (cmd == TRAY_CMD_EXIT) {
            if (g_mainWnd) PostMessageW(g_mainWnd, WM_TRAY_CMD_TO_MAIN, TRAY_CMD_EXIT, 0);
            g_shouldExit.store(true);
            PostThreadMessageW(g_trayThreadId, WM_QUIT, 0, 0);
        }
        DestroyMenu(hMenu);
    }

    LRESULT CALLBACK TrayWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
        case WM_NCCREATE: {
            CREATESTRUCTW* pcs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            if (pcs && pcs->lpCreateParams) {
                SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pcs->lpCreateParams));
            }
            return DefWindowProcW(hWnd, msg, wParam, lParam);
        }
        case WM_TRAY_NOTIFY: {
            if (lParam == WM_RBUTTONUP) {
                ShowTrayMenu();
            } else if (lParam == WM_LBUTTONDBLCLK) {
                bool newMute = !DialogMuteHook::GetDialogMute();
                DialogMuteHook::SetDialogMute(newMute);
                UpdateTrayIcon();
                if (g_mainWnd) PostMessageW(g_mainWnd, WM_TRAY_CMD_TO_MAIN, TRAY_CMD_TOGGLE_MUTE, newMute ? 1 : 0);
            }
            break;
        }
        case WM_HOTKEY: {
            if (wParam == 1) {
                bool newMute = !DialogMuteHook::GetDialogMute();
                DialogMuteHook::SetDialogMute(newMute);
                UpdateTrayIcon();
                if (g_mainWnd) PostMessageW(g_mainWnd, WM_TRAY_CMD_TO_MAIN, TRAY_CMD_TOGGLE_MUTE, newMute ? 1 : 0);
            }
            break;
        }
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == TRAY_CMD_TOGGLE_MUTE) {
                bool newMute = !DialogMuteHook::GetDialogMute();
                DialogMuteHook::SetDialogMute(newMute);
                UpdateTrayIcon();
                if (g_mainWnd) PostMessageW(g_mainWnd, WM_TRAY_CMD_TO_MAIN, TRAY_CMD_TOGGLE_MUTE, newMute ? 1 : 0);
            } else if (id == TRAY_CMD_EXIT) {
                g_shouldExit.store(true);
                PostThreadMessageW(g_trayThreadId, WM_QUIT, 0, 0);
            }
            break;
        }
        case WM_DESTROY: {
            PostQuitMessage(0);
            break;
        }
        default:
            return DefWindowProcW(hWnd, msg, wParam, lParam);
        }
        return 0;
    }

    // Tray thread proc
    DWORD WINAPI TrayThreadProc(LPVOID lpParameter) {
        g_mainWnd = reinterpret_cast<HWND>(lpParameter);
        HINSTANCE hInst = GetModuleHandleW(NULL);
        const wchar_t* CLASS_NAME = L"MyApp_TrayThread_WindowClass_2026";

        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = TrayWndProc;
        wc.hInstance = hInst;
        wc.lpszClassName = CLASS_NAME;

        ATOM a = RegisterClassExW(&wc);
        (void)a; // ignore

        HWND hwnd = CreateWindowExW(
            0,
            CLASS_NAME,
            L"",
            0,
            0, 0, 0, 0,
            HWND_MESSAGE,
            nullptr,
            hInst,
            nullptr
        );

        if (!hwnd) {
            if (g_startEvent) SetEvent(g_startEvent);
            return 1;
        }

        g_trayWnd = hwnd;
        ZeroMemory(&g_nid, sizeof(g_nid));
        g_nid.cbSize = sizeof(g_nid);
        g_nid.hWnd = g_trayWnd;
        g_nid.uID  = 1001;
        g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        g_nid.uCallbackMessage = WM_TRAY_NOTIFY;
        HICON icon = LoadAppIconOrFallback(DialogMuteHook::GetDialogMute() ? IDI_SOUND_OFF : IDI_SOUND_ON);
        g_nid.hIcon = icon;
        StringCchCopyW(g_nid.szTip, ARRAYSIZE(g_nid.szTip), L"Dialog sound");

        Shell_NotifyIconW(NIM_ADD, &g_nid);

        // register a hotkey (Ctrl+Alt+M). if fails, ignore.
        RegisterHotKey(g_trayWnd, 1, MOD_CONTROL | MOD_ALT, 'M');

        if (g_startEvent) SetEvent(g_startEvent);

        MSG msg;
        while (GetMessageW(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (g_shouldExit.load()) break;
        }

        UnregisterHotKey(g_trayWnd, 1);
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        if (g_nid.hIcon) {
            DestroyIcon(g_nid.hIcon);
            g_nid.hIcon = nullptr;
        }
        if (g_trayWnd) {
            DestroyWindow(g_trayWnd);
            g_trayWnd = nullptr;
        }
        return 0;
    }
}

// Public API
namespace TrayThread {

bool StartTrayThread(HWND hMainWnd) {
    if (g_trayThreadHandle) return true; // already started

    g_shouldExit.store(false);
    g_startEvent = CreateEventW(NULL, TRUE, FALSE, NULL); // manual-reset
    if (!g_startEvent) return false;

    g_mainWnd = hMainWnd;
    g_trayThreadHandle = CreateThread(
        NULL,
        0,
        TrayThreadProc,
        reinterpret_cast<LPVOID>(g_mainWnd),
        0,
        &g_trayThreadId
    );
    if (!g_trayThreadHandle) {
        CloseHandle(g_startEvent);
        g_startEvent = nullptr;
        return false;
    }

    // wait up to 3s for the tray thread to signal ready
    DWORD wait = WaitForSingleObject(g_startEvent, 3000);
    CloseHandle(g_startEvent);
    g_startEvent = nullptr;

    if (wait == WAIT_TIMEOUT) {
        return true; // proceed anyway
    }
    return true;
}

bool StopTrayThread() {
    if (!g_trayThreadHandle) return true;
    g_shouldExit.store(true);
    if (g_trayThreadId) PostThreadMessageW(g_trayThreadId, WM_QUIT, 0, 0);
    DWORD wait = WaitForSingleObject(g_trayThreadHandle, 2000);
    if (wait == WAIT_TIMEOUT) {
        CloseHandle(g_trayThreadHandle);
        g_trayThreadHandle = nullptr;
        g_trayThreadId = 0;
        return false;
    }
    CloseHandle(g_trayThreadHandle);
    g_trayThreadHandle = nullptr;
    g_trayThreadId = 0;
    return true;
}

bool IsMuted() {
    return DialogMuteHook::GetDialogMute();
}

} // namespace TrayThread
