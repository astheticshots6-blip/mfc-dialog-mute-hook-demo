#pragma once
#include <windows.h>
#include <atomic>

namespace DialogMuteHook
{
    void InstallDialogMuteHook();
    void UninstallDialogMuteHook();
    void SetDialogMute(bool mute);
    bool GetDialogMute();
}
