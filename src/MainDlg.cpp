#include "StdAfx.h"
#include "MainDlg.h"

BEGIN_MESSAGE_MAP(CMainDlg, CDialogEx)
    ON_BN_CLICKED(IDC_SHOW_MODAL, &CMainDlg::OnShowModal)
    ON_BN_CLICKED(IDC_TOGGLE_MUTE, &CMainDlg::OnToggleMute)
    ON_BN_CLICKED(IDCANCEL, &CMainDlg::OnClose)
END_MESSAGE_MAP()

void CMainDlg::OnShowModal()
{
    // This shows a second example of a modal dialog that may trigger a system beep.
    // The mute hook suppresses those sounds without altering the modal dialog flow.
    MessageBoxW(NULL, L"This is a modal dialog. It still behaves modally, but the sound can be muted globally.", L"Dialog Mute Demo", MB_OK | MB_ICONINFORMATION);
}

void CMainDlg::OnToggleMute()
{
    bool mute = !DialogMuteHook::GetDialogMute();
    DialogMuteHook::SetDialogMute(mute);
    SetWindowTextW(mute ? L"Dialog Mute Demo - Sound Off" : L"Dialog Mute Demo - Sound On");
    CStringW text = mute ? L"Muted" : L"Unmuted";
    MessageBoxW(NULL, text + L"\r\nThe setting is process-wide and applies to subsequent modal dialogs.", L"Dialog Sound State", MB_OK | MB_ICONINFORMATION);
}

void CMainDlg::OnClose()
{
    EndDialog(IDOK);
}
