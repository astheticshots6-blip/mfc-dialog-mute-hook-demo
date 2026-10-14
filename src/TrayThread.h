#pragma once

#include <Windows.h>

namespace TrayThread
{
    // Start the tray thread. Pass main window HWND (optional) so tray thread can PostMessage there.
    // Returns true if started (thread created). Caller should still verify tray icon presence.
    bool StartTrayThread(HWND hMainWnd);

    // Request tray thread exit and wait for it to stop. Returns true if stopped cleanly.
    bool StopTrayThread();

    // Optional: query current mute state (thread-safe)
    bool IsMuted();
}
