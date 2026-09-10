#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <string>
#include <memory>

#include "resource.h"
#include "config.h"
#include "login.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "version.lib")

#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

static void EnableDpiAwareness() {
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    typedef BOOL(WINAPI* Fn)(DPI_AWARENESS_CONTEXT);
    auto fn = reinterpret_cast<Fn>(GetProcAddress(u32, "SetProcessDpiAwarenessContext"));
    if (fn) fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    else SetProcessDPIAware();
}

class MainWindow {
public:
    MainWindow(HINSTANCE hInst, bool autoRun) : hInst_(hInst), autoRun_(autoRun) {}
    ~MainWindow() = default;

    bool Create() {
        cfg_.probe_url = L"http://www.msftconnecttest.com/connecttest.txt";
        LoadConfig(ConfigFilePath(), cfg_);

        static const wchar_t* kClass = L"CampusNetLoginMain";
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = WndProc;
        wc.hInstance = hInst_;
        wc.lpszClassName = kClass;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        wc.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
        RegisterClassExW(&wc);

        DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        hwnd_ = CreateWindowExW(0, kClass, L"校园网自动登录", style,
                                CW_USEDEFAULT, CW_USEDEFAULT, 470, 326,
                                nullptr, nullptr, hInst_, this);
        return hwnd_ != nullptr;
    }

    void Show(int nCmd = SW_SHOWNORMAL) { ShowWindow(hwnd_, nCmd); UpdateWindow(hwnd_); }

private:
    HINSTANCE hInst_;
    HWND hwnd_ = nullptr;
    HFONT hfont_ = nullptr;
    bool autoRun_ = false;
    float scale_ = 1.0f;
    Config cfg_;
    std::unique_ptr<LoginRunner> login_;

    void SetStatus(const std::wstring& s) {
        SetDlgItemTextW(hwnd_, IDC_STATUS, s.c_str());
    }

    int S(int v) const { return static_cast<int>(v * scale_ + 0.5f); }

    void ReadFields() {
        wchar_t buf[1024];
        GetDlgItemTextW(hwnd_, IDC_NETWORK, buf, 1024); cfg_.network_name = buf;
        GetDlgItemTextW(hwnd_, IDC_AUTH_URL, buf, 1024); cfg_.auth_url = buf;
        GetDlgItemTextW(hwnd_, IDC_USERNAME, buf, 1024); cfg_.username = buf;
        GetDlgItemTextW(hwnd_, IDC_PASSWORD, buf, 1024); cfg_.password = buf;
        cfg_.auto_start = IsDlgButtonChecked(hwnd_, IDC_AUTO_START) == BST_CHECKED;
        cfg_.auto_login = IsDlgButtonChecked(hwnd_, IDC_AUTO_LOGIN) == BST_CHECKED;
    }

    void WriteFields() {
        SetDlgItemTextW(hwnd_, IDC_NETWORK, cfg_.network_name.c_str());
        SetDlgItemTextW(hwnd_, IDC_AUTH_URL, cfg_.auth_url.c_str());
        SetDlgItemTextW(hwnd_, IDC_USERNAME, cfg_.username.c_str());
        SetDlgItemTextW(hwnd_, IDC_PASSWORD, cfg_.password.c_str());
        CheckDlgButton(hwnd_, IDC_AUTO_START, cfg_.auto_start ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hwnd_, IDC_AUTO_LOGIN, cfg_.auto_login ? BST_CHECKED : BST_UNCHECKED);
    }

    void ApplyAutoStart(bool enable) {
        wchar_t exe[MAX_PATH] = {0};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring cmd = L"\"" + std::wstring(exe) + L"\" --autologin";
        HKEY hk = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER,
                            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                            0, nullptr, 0, KEY_SET_VALUE, nullptr, &hk, nullptr) == ERROR_SUCCESS) {
            if (enable) {
                RegSetValueExW(hk, L"CampusNetLogin", 0, REG_SZ,
                               reinterpret_cast<const BYTE*>(cmd.c_str()),
                               static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
            } else {
                RegDeleteValueW(hk, L"CampusNetLogin");
            }
            RegCloseKey(hk);
        }
    }

    void StartLogin() {
        if (!login_ || login_->IsRunning()) return;
        ReadFields();
        SaveConfig(ConfigFilePath(), cfg_);
        ApplyAutoStart(cfg_.auto_start);
        SetStatus(L"正在启动登录…");
        EnableWindow(GetDlgItem(hwnd_, IDC_BTN_LOGIN), FALSE);
        login_->Start(cfg_);
    }

    void OnCreate() {
        UINT dpi = GetDpiForWindow(hwnd_);
        scale_ = dpi / 96.0f;

        DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_STYLE));
        RECT rc = { 0, 0, S(470), S(364) };
        AdjustWindowRectExForDpi(&rc, style, FALSE, 0, dpi);
        SetWindowPos(hwnd_, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

        hfont_ = CreateFontW(-MulDiv(9, static_cast<int>(dpi), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        auto ctl = [this](DWORD st, const wchar_t* cls, const wchar_t* text,
                          int x, int y, int w, int h, int id, DWORD ex = 0) {
            HWND c = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | st,
                                     S(x), S(y), S(w), S(h), hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                     hInst_, nullptr);
            if (hfont_) SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(hfont_), TRUE);
            return c;
        };

        ctl(SS_LEFT, L"STATIC", L"网络名称", 16, 16, 76, 20, 0);
        ctl(WS_TABSTOP | ES_AUTOHSCROLL, L"EDIT", L"", 100, 13, 354, 23, IDC_NETWORK);
        ctl(SS_LEFT, L"STATIC", L"留空则跳过自动连网", 100, 40, 220, 18, 0);

        ctl(SS_LEFT, L"STATIC", L"门户地址", 16, 58, 76, 20, 0);
        ctl(WS_TABSTOP | ES_AUTOHSCROLL, L"EDIT", L"", 100, 55, 354, 23, IDC_AUTH_URL);
        ctl(SS_LEFT, L"STATIC", L"留空则自动探测门户", 100, 82, 220, 18, 0);

        ctl(SS_LEFT, L"STATIC", L"账号", 16, 112, 76, 20, 0);
        ctl(WS_TABSTOP | ES_AUTOHSCROLL, L"EDIT", L"", 100, 109, 354, 23, IDC_USERNAME);

        ctl(SS_LEFT, L"STATIC", L"密码", 16, 150, 76, 20, 0);
        ctl(WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD, L"EDIT", L"", 100, 147, 354, 23, IDC_PASSWORD);

        ctl(BS_AUTOCHECKBOX | WS_TABSTOP, L"BUTTON", L"开机自启", 16, 184, 96, 22, IDC_AUTO_START);
        ctl(BS_AUTOCHECKBOX | WS_TABSTOP, L"BUTTON", L"启动时自动登录", 132, 184, 140, 22, IDC_AUTO_LOGIN);

        ctl(ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL, L"EDIT", L"",
            16, 216, 438, 86, IDC_STATUS);

        ctl(BS_PUSHBUTTON | WS_TABSTOP, L"BUTTON", L"立即登录", 16, 318, 100, 32, IDC_BTN_LOGIN);
        ctl(BS_PUSHBUTTON | WS_TABSTOP, L"BUTTON", L"保存设置", 128, 318, 100, 32, IDC_BTN_SAVE);
        ctl(BS_PUSHBUTTON | WS_TABSTOP, L"BUTTON", L"退出", 240, 318, 100, 32, IDC_BTN_CLOSE);

        WriteFields();
        login_ = std::make_unique<LoginRunner>(hwnd_);
        SetStatus(L"就绪：点击「立即登录」，或勾选「开机自启」后保存。");

        if (autoRun_ && cfg_.auto_login)
            PostMessageW(hwnd_, WM_APP_AUTOSTART, 0, 0);
    }

    void OnCommand(WPARAM wParam) {
        switch (LOWORD(wParam)) {
            case IDC_BTN_LOGIN:
                StartLogin();
                break;
            case IDC_BTN_SAVE:
                ReadFields();
                SaveConfig(ConfigFilePath(), cfg_);
                ApplyAutoStart(cfg_.auto_start);
                SetStatus(L"已保存设置。");
                break;
            case IDC_BTN_CLOSE:
                SendMessageW(hwnd_, WM_CLOSE, 0, 0);
                break;
        }
    }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        MainWindow* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            self = reinterpret_cast<MainWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            if (self) self->hwnd_ = hwnd;
        }
        if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);

        switch (msg) {
            case WM_CREATE:
                self->OnCreate();
                return 0;
            case WM_COMMAND:
                self->OnCommand(wParam);
                return 0;
            case WM_APP_STATUS: {
                std::wstring* m = reinterpret_cast<std::wstring*>(lParam);
                if (m) { self->SetStatus(*m); delete m; }
                return 0;
            }
            case WM_APP_DONE: {
                bool success = (wParam != 0);
                EnableWindow(GetDlgItem(hwnd, IDC_BTN_LOGIN), TRUE);
                if (self->login_) self->login_->MarkDone();
                if (self->autoRun_ && success) {
                    self->SetStatus(L"登录成功，程序即将退出…");
                    SetTimer(hwnd, TIMER_AUTOCLOSE, 1500, nullptr);
                }
                return 0;
            }
            case WM_APP_AUTOSTART:
                self->StartLogin();
                return 0;
            case WM_TIMER:
                if (wParam == TIMER_AUTOCLOSE) {
                    KillTimer(hwnd, TIMER_AUTOCLOSE);
                    SendMessageW(hwnd, WM_CLOSE, 0, 0);
                }
                return 0;
            case WM_CLOSE:
                if (self->login_) self->login_->Shutdown();
                DestroyWindow(hwnd);
                return 0;
            case WM_DESTROY:
                if (self->hfont_) DeleteObject(self->hfont_);
                PostQuitMessage(0);
                return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
};

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    EnableDpiAwareness();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    bool autoRun = false;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i < argc; ++i)
            if (wcscmp(argv[i], L"--autologin") == 0) autoRun = true;
        LocalFree(argv);
    }

    MainWindow wnd(hInst, autoRun);
    if (!wnd.Create()) {
        CoUninitialize();
        return 1;
    }
    wnd.Show();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
