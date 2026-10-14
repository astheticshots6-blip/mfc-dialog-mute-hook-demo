#pragma once

#include "StdAfx.h"
#include "resource.h"

class CMainDlg : public CDialogEx
{
public:
    CMainDlg() : CDialogEx(IDD_MAINDLG) {}

protected:
    virtual BOOL OnInitDialog() override {
        CDialogEx::OnInitDialog();
        return TRUE;
    }

    DECLARE_MESSAGE_MAP()
};

BEGIN_MESSAGE_MAP(CMainDlg, CDialogEx)
END_MESSAGE_MAP()
