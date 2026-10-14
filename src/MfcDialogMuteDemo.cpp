#include "StdAfx.h"
#include "MfcDialogMuteDemo.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

BEGIN_MESSAGE_MAP(CMfcDialogMuteDemoApp, CWinApp)
END_MESSAGE_MAP()

CMfcDialogMuteDemoApp::CMfcDialogMuteDemoApp()
{
}

CMfcDialogMuteDemoApp theApp;

BOOL CMfcDialogMuteDemoApp::InitInstance()
{
    CWinApp::InitInstance();

    // Install the dialog mute hook
    DialogMuteHook::InstallDialogMuteHook();

    // Create tray manager
    m_pTray = new TrayIconManager();
    m_pTray->Init(AfxGetInstanceHandle());

    // Show a simple dialog to demo
    CMainDlg dlg;
    m_pMainWnd = &dlg;
    INT_PTR nResponse = dlg.DoModal();

    // Clean up
    if (m_pTray) {
        m_pTray->Shutdown();
        delete m_pTray;
        m_pTray = nullptr;
    }

    DialogMuteHook::UninstallDialogMuteHook();

    return FALSE; // exit application
}
