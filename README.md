# MFC Dialog Mute Hook Demo

This repository contains a small MFC example that demonstrates how to suppress the system beep for modal dialogs without removing the modal behavior itself.

It is designed for the following scenario:
- MFC application
- many dialogs are modal and trigger system sound (`MessageBeep`, `Beep`, `PlaySound`, etc.)
- you want a small code change and a runtime toggle to mute dialog sounds
- you want to stay mostly transparent to existing dialog code

What is included:
- MFC sample app with a small main dialog
- runtime tray toggle (`静音提示音` / `取消静音`)
- IAT patch implementation to mute sound APIs at runtime with a single initialization call
- a `MinHook` submodule entry for future optional use

Build steps:
1. Clone the repo
   git clone https://github.com/astheticshots6-blip/mfc-dialog-mute-hook-demo.git
2. Update submodule (optional, recommended for future hook library use):
   git submodule update --init --recursive
3. Open `MfcDialogMuteDemo.sln` in Visual Studio 2022
4. Set configuration to `Debug/x64` or `Release/x64`
5. Build

Notes:
- The example intentionally uses a shared MFC runtime.
- The key integration point is `CMfcDialogMuteDemoApp::InitInstance()` on one line: `DialogMuteHook::InstallDialogMuteHook();`
- The sound hook is process-wide and does not require editing every dialog.
