#include "StdAfx.h"

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

    // One-line integration point: install dialog beep mute hook before dialogs are shown.
    DialogMuteHook::InstallDialogMuteHook();

    // Create and initialize tray toggle.
    m_pTray = new TrayIconManager();
    if (!m_pTray->Init(AfxGetInstanceHandle())) {
        delete m_pTray;
        m_pTray = nullptr;
    }

    CMainDlg dlg;
    m_pMainWnd = &dlg;
    INT_PTR nResponse = dlg.DoModal();
    UNREFERENCED_PARAMETER(nResponse);

    if (m_pTray) {
        m_pTray->Shutdown();
        delete m_pTray;
        m_pTray = nullptr;
    }

    DialogMuteHook::UninstallDialogMuteHook();
    return FALSE;
}
