#include "TrayIconManager.h"
#include "DialogMuteHook.h"
#include <shellapi.h>
#include <strsafe.h>

#define TRAY_ICON_ID 1001
#define ID_TRAY_TOGGLE 40001
#define ID_TRAY_EXIT 40002

#pragma comment(lib, "Shell32.lib")

TrayIconManager::TrayIconManager() = default;

TrayIconManager::~TrayIconManager()
{
    Shutdown();
}

bool TrayIconManager::Init(HINSTANCE hInstance)
{
    m_hInstance = hInstance;
    m_hWnd = CreateMessageWindow(hInstance);
    if (!m_hWnd) {
        return false;
    }

    ZeroMemory(&m_nid, sizeof(m_nid));
    m_nid.cbSize = sizeof(m_nid);
    m_nid.hWnd = m_hWnd;
    m_nid.uID = TRAY_ICON_ID;
    m_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    m_nid.uCallbackMessage = m_uTaskbarMsg;
    m_nid.hIcon = LoadIconW(hInstance, MAKEINTRESOURCE(101));
    StringCchCopyW(m_nid.szTip, ARRAYSIZE(m_nid.szTip), L"Dialog Mute Demo");

    bool ok = Shell_NotifyIconW(NIM_ADD, &m_nid) != FALSE;
    if (!ok) {
        if (m_nid.hIcon) DestroyIcon(m_nid.hIcon);
        return false;
    }

    m_bInited = true;
    return true;
}

void TrayIconManager::Shutdown()
{
    if (m_bInited) {
        Shell_NotifyIconW(NIM_DELETE, &m_nid);
        if (m_nid.hIcon) {
            DestroyIcon(m_nid.hIcon);
            m_nid.hIcon = nullptr;
        }
        m_bInited = false;
    }

    if (m_hWnd) {
        DestroyWindow(m_hWnd);
        m_hWnd = nullptr;
    }
}

HWND TrayIconManager::CreateMessageWindow(HINSTANCE hInstance)
{
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"DialogMute_TrayWindow";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    return CreateWindowW(wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hInstance, this);
}

LRESULT CALLBACK TrayIconManager::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_CREATE) {
        auto* pcs = reinterpret_cast<CREATESTRUCT*>(lParam);
        auto* pThis = reinterpret_cast<TrayIconManager*>(pcs->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        return 0;
    }

    auto* pThis = reinterpret_cast<TrayIconManager*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    if (!pThis) {
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }

    if (msg == pThis->m_uTaskbarMsg) {
        if (lParam == WM_RBUTTONUP) {
            pThis->ShowContextMenu();
        } else if (lParam == WM_LBUTTONDBLCLK) {
            pThis->ToggleMute();
        }
        return 0;
    }

    if (msg == WM_COMMAND) {
        pThis->OnTrayCommand(LOWORD(wParam));
        return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

void TrayIconManager::ToggleMute()
{
    bool mute = !DialogMuteHook::GetDialogMute();
    DialogMuteHook::SetDialogMute(mute);

    if (m_bInited) {
        wchar_t tip[128] = L"";
        StringCchPrintfW(tip, ARRAYSIZE(tip), L"Dialog Sound: %s", mute ? L"Off" : L"On");
        StringCchCopyW(m_nid.szTip, ARRAYSIZE(m_nid.szTip), tip);
        m_nid.uFlags = NIF_TIP;
        Shell_NotifyIconW(NIM_MODIFY, &m_nid);
    }
}

void TrayIconManager::ShowContextMenu()
{
    HMENU hMenu = CreatePopupMenu();
    bool mute = DialogMuteHook::GetDialogMute();
    InsertMenuW(hMenu, 0, MF_BYPOSITION | MF_STRING, ID_TRAY_TOGGLE, mute ? L"取消静音" : L"静音提示音");
    InsertMenuW(hMenu, 1, MF_BYPOSITION | MF_STRING, ID_TRAY_EXIT, L"退出");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(m_hWnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hWnd, nullptr);
    PostMessageW(m_hWnd, WM_NULL, 0, 0);
    DestroyMenu(hMenu);
}

void TrayIconManager::OnTrayCommand(UINT cmd)
{
    switch (cmd) {
    case ID_TRAY_TOGGLE:
        ToggleMute();
        break;
    case ID_TRAY_EXIT:
        PostQuitMessage(0);
        break;
    }
}
