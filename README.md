# MFC Dialog Mute Hook Demo

This demo shows how to intercept Windows dialog beep/play sound APIs in an MFC app using MinHook and provide a system tray toggle to mute/unmute dialog sounds.

Instructions:
1. Clone repo:
   git clone https://github.com/astheticshots6-blip/mfc-dialog-mute-hook-demo.git
2. Initialize submodules (MinHook):
   git submodule update --init --recursive
3. Open `MfcDialogMuteDemo.sln` in Visual Studio 2022 (x64 configuration) and build.

Notes:
- The project is configured to Use MFC in a Shared DLL.
- MinHook is included as a git submodule under `External/MinHook`.
- Replace the `app.ico` resource if needed.

