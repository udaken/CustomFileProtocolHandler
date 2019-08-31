#include "framework.h"
#include "resource.h"
#include "CFileProtocolHandlerService.h"
#include <windowsx.h>
#include <string>
#include <algorithm>

using namespace std::string_literals;

#define MAX_LOADSTRING 100
#define MAX_PROTOCOLNAME 64

// グローバル変数:
HINSTANCE hInst;                                // 現在のインターフェイス
WCHAR szTitle[MAX_LOADSTRING];                  // タイトル バーのテキスト
WCHAR szProtocolName[MAX_PROTOCOLNAME] = L"set-your-protocol-name";

static CFileProtocolHandlerService* s_app;

EXTERN_C INT_PTR CALLBACK    DialogProc(HWND, UINT, WPARAM, LPARAM);

static void HresultErrorMessageBox(HWND hWnd, HRESULT hr, const std::wstring& baseMessage)
{
	HLOCAL msg = nullptr;
	::FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr, hr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPWSTR>(&msg), 0, nullptr);
	::MessageBox(hWnd, (baseMessage + L"\n理由: " + reinterpret_cast<LPCWSTR>(msg)).c_str(), szTitle, MB_OK | MB_ICONERROR);
	::LocalFree(msg);
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE /*hPrevInstance*/,
	_In_ LPWSTR    lpCmdLine,
	_In_ int       nCmdShow)
{
	hInst = hInstance;
	//::CoInitialize(nullptr);
	// グローバル文字列を初期化する
	LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);

	{
		auto const modulePath = getModulePath() + L".protocolname";
		FILE* file = nullptr;
		_wfopen_s(&file, modulePath.c_str(), L"r");
		if (file)
		{
			fgetws(szProtocolName, _countof(szProtocolName), file);
			
			using namespace std;
			std::replace_if(begin(szProtocolName), end(szProtocolName),
				[](auto c) { return iswspace(c) || iswcntrl(c); }, L'\0');
		}
	}

	CFileProtocolHandlerService app{ szProtocolName };

	if (lpCmdLine && wcslen(lpCmdLine) > 0)
	{
		auto hr = app.OpenFile(lpCmdLine);
		if (FAILED(hr) && HRESULT_FROM_WIN32(ERROR_CANCELLED) != hr)
		{
			HresultErrorMessageBox(nullptr, hr, L"ShellExecuteExに失敗しました。:");
		}
		else if (S_FALSE == hr)
		{
			::MessageBox(nullptr, (L"プログラム引数が不正です。:`"s + lpCmdLine + L"`").c_str(), szTitle, MB_OK | MB_ICONERROR);
		}
		return 0;
	}

	s_app = &app;
	DialogBoxParam(hInst, MAKEINTRESOURCE(IDD_FILEPROTOCOLHANDLER_DIALOG), nullptr, DialogProc, reinterpret_cast<LPARAM>(&app));
	return 0;
}

void SetDialogItem(HWND hDlg, CFileProtocolHandlerService* app)
{
	auto regsiterd = app->IsRegistered();
	::EnableWindow(::GetDlgItem(hDlg, IDC_BUTTON_UNREGISTER), regsiterd == S_OK && app->IsValidProtocolName());
	::EnableWindow(::GetDlgItem(hDlg, IDC_BUTTON_REGISTER), regsiterd == S_FALSE);
}

INT_PTR OnCommand(HWND hDlg, int id, HWND hwndCtl, UINT codeNotify)
{
	switch (id)
	{
	case IDOK:
	case IDCANCEL:
		EndDialog(hDlg, id);
		return (INT_PTR)TRUE;
	case IDC_BUTTON_UNREGISTER:
	{
		auto registerdPath = s_app->GetRegisteredPath();
		if (FAILED(registerdPath.first))
		{
			HresultErrorMessageBox(hDlg, registerdPath.first, L"レジストリの読み書きに失敗しました。:");
			return (INT_PTR)TRUE;
		}
		if (IDOK != ::MessageBox(hDlg, (L"下記のコマンドで登録されています。\n続行しますか？\n" + registerdPath.second).c_str(), szTitle,
			MB_OKCANCEL | MB_ICONQUESTION | MB_DEFBUTTON2))
		{
			return (INT_PTR)TRUE;
		}

		auto const hr = s_app->Unregister();
		if (FAILED(hr))
			HresultErrorMessageBox(hDlg, hr, L"解除に失敗しました。");

		SetDialogItem(hDlg, s_app);
		return (INT_PTR)TRUE;
	}
	case IDC_BUTTON_REGISTER:
	{
		auto const hr = s_app->Register();
		if (FAILED(hr))
			HresultErrorMessageBox(hDlg, hr, L"登録に失敗しました。");
		SetDialogItem(hDlg, s_app);
		return (INT_PTR)TRUE;
	}
	}

	return (INT_PTR)FALSE;
}
BOOL OnInitDialog(HWND hDlg, HWND hwndFocus, LPARAM lParam)
{
	auto hIcon = (HICON)LoadImage(hInst, MAKEINTRESOURCE(IDI_SMALL), IMAGE_ICON, 16, 16, 0);
	SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
	::SetDlgItemText(hDlg, IDC_EDIT1, s_app->ProtocolName().c_str());
	if (!s_app->IsValidProtocolName())
	{
		::MessageBox(hDlg, (L"プロトコル名が不正です。:`" + s_app->ProtocolName() + L"`").c_str(), szTitle, MB_OK | MB_ICONERROR);
	}

	SetDialogItem(hDlg, s_app);
	auto regsiterd = s_app->IsRegistered();
	if (FAILED(regsiterd))
	{
		HresultErrorMessageBox(hDlg, regsiterd, L"レジストリの読み書きに失敗しました。:"s);
	}
	return TRUE;
}

// バージョン情報ボックスのメッセージ ハンドラーです。
INT_PTR CALLBACK DialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
		return HANDLE_WM_INITDIALOG(hDlg, wParam, lParam, OnInitDialog);
	case WM_COMMAND:
		return HANDLE_WM_COMMAND(hDlg, wParam, lParam, OnCommand);
	}
	return (INT_PTR)FALSE;
}
