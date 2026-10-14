#pragma once

#include "StdAfx.h"
#include "TrayIconManager.h"
#include "DialogMuteHook.h"

class CMfcDialogMuteDemoApp : public CWinApp
{
public:
    CMfcDialogMuteDemoApp();
    virtual BOOL InitInstance() override;

    TrayIconManager* m_pTray = nullptr;
};
