#include "TrayIconManager.h"
#include "DialogMuteHook.h"
#include <shellapi.h>
#include <strsafe.h>

#define TRAY_ICON_ID 1001
#define WM_TRAY_CALLBACK (WM_USER + 0x100)
#define ID_TRAY_TOGGLE 40001
#define ID_TRAY_EXIT   40002

#pragma comment(lib, "shell32.lib")

TrayIconManager::TrayIconManager() {}
TrayIconManager::~TrayIconManager() { Shutdown(); }

bool TrayIconManager::Init(HINSTANCE hInstance) {
    m_hInstance = hInstance;
    m_hWnd = CreateMessageWindow(hInstance);
    if (!m_hWnd) return false;

    m_uTaskbarMsg = WM_TRAY_CALLBACK;

    ZeroMemory(&m_nid, sizeof(m_nid));
    m_nid.cbSize = sizeof(NOTIFYICONDATAW);
    m_nid.hWnd = m_hWnd;
    m_nid.uID = TRAY_ICON_ID;
    m_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    m_nid.uCallbackMessage = m_uTaskbarMsg;
    m_nid.hIcon = LoadIconW(m_hInstance, MAKEINTRESOURCE(101));
    StringCchCopyW(m_nid.szTip, ARRAYSIZE(m_nid.szTip), L"Dialog Sound: On");

    if (!Shell_NotifyIconW(NIM_ADD, &m_nid)) {
        if (m_nid.hIcon) DestroyIcon(m_nid.hIcon);
        return false;
    }
    m_bInited = true;
    return true;
}

void TrayIconManager::Shutdown() {
    if (m_bInited) {
        Shell_NotifyIconW(NIM_DELETE, &m_nid);
        if (m_nid.hIcon) { DestroyIcon(m_nid.hIcon); m_nid.hIcon = nullptr; }
        m_bInited = false;
    }
    if (m_hWnd) { DestroyWindow(m_hWnd); m_hWnd = nullptr; }
}

HWND TrayIconManager::CreateMessageWindow(HINSTANCE hInstance) {
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = TrayIconManager::WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"DialogMute_TrayMessageWindow";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    HWND hWnd = CreateWindowW(wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hInstance, this);
    return hWnd;
}

LRESULT CALLBACK TrayIconManager::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CREATE) {
        CREATESTRUCT* pcs = (CREATESTRUCT*)lParam;
        TrayIconManager* pThis = (TrayIconManager*)pcs->lpCreateParams;
        SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pThis);
        return 0;
    }

    TrayIconManager* pThis = (TrayIconManager*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
    if (!pThis) return DefWindowProc(hWnd, msg, wParam, lParam);

    if (msg == pThis->m_uTaskbarMsg) {
        if (lParam == WM_RBUTTONUP) { pThis->ShowContextMenu(); }
        else if (lParam == WM_LBUTTONDBLCLK) { pThis->ToggleMute(); }
        return 0;
    }

    if (msg == WM_COMMAND) {
        UINT id = LOWORD(wParam);
        pThis->OnTrayCommand(id);
        return 0;
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}

void TrayIconManager::ShowContextMenu() {
    HMENU hMenu = CreatePopupMenu();
    bool mute = DialogMuteHook::GetDialogMute();
    InsertMenuW(hMenu, 0, MF_BYPOSITION | MF_STRING, ID_TRAY_TOGGLE, mute ? L"取消静音" : L"静音提示音");
    InsertMenuW(hMenu, 1, MF_BYPOSITION | MF_STRING, ID_TRAY_EXIT, L"退出");

    POINT pt; GetCursorPos(&pt);
    SetForegroundWindow(m_hWnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hWnd, NULL);
    PostMessage(m_hWnd, WM_NULL, 0, 0);
    DestroyMenu(hMenu);
}

void TrayIconManager::OnTrayCommand(UINT cmd) {
    switch (cmd) {
    case ID_TRAY_TOGGLE: ToggleMute(); break;
    case ID_TRAY_EXIT: PostQuitMessage(0); break;
    default: break;
    }
}

void TrayIconManager::ToggleMute() {
    bool currently = DialogMuteHook::GetDialogMute();
    DialogMuteHook::SetDialogMute(!currently);

    if (m_bInited) {
        wchar_t tip[128];
        StringCchPrintfW(tip, ARRAYSIZE(tip), L"Dialog Sound: %s", DialogMuteHook::GetDialogMute() ? L"Off" : L"On");
        StringCchCopyW(m_nid.szTip, ARRAYSIZE(m_nid.szTip), tip);
        m_nid.uFlags = NIF_TIP;
        Shell_NotifyIconW(NIM_MODIFY, &m_nid);
    }
}
