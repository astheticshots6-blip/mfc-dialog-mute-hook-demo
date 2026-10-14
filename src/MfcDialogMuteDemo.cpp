#pragma once

#include "StdAfx.h"
#include "MfcDialogMuteDemo.h"
#include "MainDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

BEGIN_MESSAGE_MAP(CMfcDialogMuteDemoApp, CWinApp)
END_MESSAGE_MAP()

CMfcDialogMuteDemoApp::CMfcDialogMuteDemoApp() = default;

CMfcDialogMuteDemoApp theApp;

BOOL CMfcDialogMuteDemoApp::InitInstance()
{
    CWinApp::InitInstance();

    // Install the dialog mute hook before any MFC modal box is shown.
    DialogMuteHook::InstallDialogMuteHook();

    // Start tray thread (independent UI thread handling the tray icon)
    TrayThread::StartTrayThread(nullptr);

    CMainDlg dlg;
    m_pMainWnd = &dlg;
    INT_PTR nResponse = dlg.DoModal();
    UNREFERENCED_PARAMETER(nResponse);

    // Stop tray thread and uninstall hook
    TrayThread::StopTrayThread();
    DialogMuteHook::UninstallDialogMuteHook();

    return FALSE;
}
