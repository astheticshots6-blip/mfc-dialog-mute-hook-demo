#pragma once

#include <afxwin.h>
#include "TrayIconManager.h"

class CMfcDialogMuteDemoApp : public CWinApp
{
public:
    CMfcDialogMuteDemoApp();

    virtual BOOL InitInstance();

    TrayIconManager* m_pTray = nullptr;
};
