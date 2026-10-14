#pragma once
#include <Windows.h>

class TrayIconManager
{
public:
    TrayIconManager();
    ~TrayIconManager();

    bool Init(HINSTANCE hInstance);
    void Shutdown();
    void ToggleMute();

private:
    HWND CreateMessageWindow(HINSTANCE hInstance);
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void OnTrayCommand(UINT cmd);
    void ShowContextMenu();

private:
    HWND m_hWnd = nullptr;
    HINSTANCE m_hInstance = nullptr;
    NOTIFYICONDATAW m_nid;
    UINT m_uTaskbarMsg = 0;
    bool m_bInited = false;
};
