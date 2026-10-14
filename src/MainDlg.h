#pragma once

#include "StdAfx.h"
#include "resource.h"

class CMainDlg : public CDialogEx
{
public:
    CMainDlg() : CDialogEx(IDD_MAINDLG) {}

    enum { IDD = IDD_MAINDLG };

protected:
    virtual BOOL OnInitDialog() override {
        CDialogEx::OnInitDialog();
        SetWindowTextW(L"Dialog Mute Demo");
        return TRUE;
    }

    virtual void DoDataExchange(CDataExchange* pDX) override {
        CDialogEx::DoDataExchange(pDX);
    }

    afx_msg void OnShowModal();
    afx_msg void OnToggleMute();
    afx_msg void OnClose();

    DECLARE_MESSAGE_MAP()
};
