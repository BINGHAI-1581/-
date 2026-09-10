#include "login.h"

#include <winhttp.h>
#include <bcrypt.h>
#include <shlobj.h>

#include <string>
#include <vector>
#include <thread>

#include "resource.h"

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "shell32.lib")

// ---------------- 字符串 / 编码 ----------------
static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

static std::wstring GbkToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(936, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(936, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

static std::wstring Trim(const std::wstring& s) {
    size_t b = s.find_first_not_of(L" \t\r\n");
    if (b == std::wstring::npos) return L"";
    size_t e = s.find_last_not_of(L" \t\r\n");
    return s.substr(b, e - b + 1);
}

// ---------------- Hex ----------------
static std::wstring BytesToHex(const std::string& b) {
    static const wchar_t* hex = L"0123456789abcdef";
    std::wstring out;
    out.reserve(b.size() * 2);
    for (unsigned char c : b) { out += hex[c >> 4]; out += hex[c & 15]; }
    return out;
}

static std::string HexToBytes(const std::wstring& h) {
    auto nib = [](wchar_t c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
    };
    std::string out;
    out.reserve(h.size() / 2);
    for (size_t i = 0; i + 1 < h.size(); i += 2)
        out += (char)((nib(h[i]) << 4) | nib(h[i + 1]));
    return out;
}

// ---------------- AES-128-ECB（ZeroPadding，与门户 crypto.js 的 pa_aes_encode/pa_aes_decode 一致） ----------------
static const std::string kAesKey = "Panabit@1024_key";  // 16 字节

static bool AesEcb(const std::string& in, const std::string& key, bool encrypt, std::string& out) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_KEY_HANDLE hKey = nullptr;
    bool ok = false;
    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0) != 0) return false;
    if (BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_ECB,
                          sizeof(BCRYPT_CHAIN_MODE_ECB), 0) != 0) goto done;
    if (BCryptGenerateSymmetricKey(hAlg, &hKey, nullptr, 0, (PUCHAR)key.data(), (ULONG)key.size(), 0) != 0) goto done;
    out.resize(in.size());
    ULONG written = 0;
    NTSTATUS st = encrypt
        ? BCryptEncrypt(hKey, (PUCHAR)in.data(), (ULONG)in.size(), nullptr, nullptr, 0,
                        (PUCHAR)out.data(), (ULONG)out.size(), &written, 0)
        : BCryptDecrypt(hKey, (PUCHAR)in.data(), (ULONG)in.size(), nullptr, nullptr, 0,
                        (PUCHAR)out.data(), (ULONG)out.size(), &written, 0);
    ok = (st == 0);
    out.resize(written);
done:
    if (hKey) BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return ok;
}

static std::wstring AesEncryptHex(const std::wstring& plain) {
    std::string p = WideToUtf8(plain);
    size_t pad = (16 - (p.size() % 16)) % 16;   // ZeroPadding
    if (pad) p.append(pad, '\0');
    std::string enc;
    if (!AesEcb(p, kAesKey, true, enc)) return L"";
    return BytesToHex(enc);
}

static std::wstring AesDecryptHex(const std::wstring& hex) {
    std::string b = HexToBytes(hex);
    std::string dec;
    if (!AesEcb(b, kAesKey, false, dec)) return L"";
    while (!dec.empty() && dec.back() == '\0') dec.pop_back();
    return Utf8ToWide(dec);
}

// ---------------- URL ----------------
static std::wstring BaseOfUrl(const std::wstring& url) {
    size_t s = url.find(L"://");
    size_t start = (s == std::wstring::npos) ? 0 : s + 3;
    size_t end = url.find_first_of(L"/?#", start);
    return url.substr(0, (end == std::wstring::npos) ? url.size() : end);
}

// ---------------- 门户探测（WinHTTP，不跟随重定向） ----------------
struct PortalProbe {
    bool online = false;
    std::wstring portalUrl;
};

static PortalProbe ProbePortal(const std::wstring& probeUrl) {
    PortalProbe r;
    URL_COMPONENTS uc = {0};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {0};
    wchar_t path[2048] = {0};
    uc.lpszHostName = host; uc.dwHostNameLength = _countof(host);
    uc.lpszUrlPath = path; uc.dwUrlPathLength = _countof(path);
    if (!WinHttpCrackUrl(probeUrl.c_str(), 0, 0, &uc)) return r;

    HINTERNET session = WinHttpOpen(L"CampusNetLogin/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return r;
    HINTERNET conn = WinHttpConnect(session, host, uc.nPort, 0);
    if (!conn) { WinHttpCloseHandle(session); return r; }
    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET req = WinHttpOpenRequest(conn, L"GET", (path[0] ? path : L"/"), nullptr,
                                       WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!req) { WinHttpCloseHandle(conn); WinHttpCloseHandle(session); return r; }

    DWORD dis = WINHTTP_DISABLE_REDIRECTS;
    WinHttpSetOption(req, WINHTTP_OPTION_DISABLE_FEATURE, &dis, sizeof(dis));
    DWORD timeout = 3000;
    WinHttpSetTimeouts(req, timeout, timeout, timeout, timeout);

    if (WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(req, nullptr)) {
        DWORD status = 0, sz = sizeof(status);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
        if (status == 200 || status == 204) {
            r.online = true;
        } else if (status >= 300 && status < 400) {
            DWORD need = 0;
            WinHttpQueryHeaders(req, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                                WINHTTP_NO_OUTPUT_BUFFER, &need, WINHTTP_NO_HEADER_INDEX);
            if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && need > 0) {
                std::wstring loc(need / sizeof(wchar_t), L'\0');
                DWORD got = need;
                if (WinHttpQueryHeaders(req, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                                        &loc[0], &got, WINHTTP_NO_HEADER_INDEX))
                    r.portalUrl = loc.c_str();
            }
        }
    }
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    return r;
}

// ---------------- 连接无线网络（netsh） ----------------
static void ConnectToNetwork(const std::wstring& ssid) {
    if (ssid.empty()) return;
    std::wstring cmd = L"netsh wlan connect name=\"" + ssid + L"\"";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');
    buf.resize(buf.size() + 64, L'\0');
    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                       nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 8000);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

// ---------------- HTTP POST ----------------
static bool HttpPostApi(const std::wstring& baseUrl, const std::wstring& query,
                        const std::string& body, std::string& response) {
    URL_COMPONENTS uc = {0};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {0};
    uc.lpszHostName = host; uc.dwHostNameLength = _countof(host);
    if (!WinHttpCrackUrl(baseUrl.c_str(), 0, 0, &uc)) return false;
    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;

    HINTERNET session = WinHttpOpen(L"CampusNetLogin/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;
    HINTERNET conn = WinHttpConnect(session, host, uc.nPort, 0);
    if (!conn) { WinHttpCloseHandle(session); return false; }

    std::wstring path = L"/api?" + query;
    HINTERNET req = WinHttpOpenRequest(conn, L"POST", path.c_str(), nullptr,
                                       WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!req) { WinHttpCloseHandle(conn); WinHttpCloseHandle(session); return false; }

    const wchar_t* headers = L"Content-Type: application/x-www-form-urlencoded; charset=GB2312\r\n";
    DWORD bodyLen = (DWORD)body.size();
    LPVOID bodyPtr = body.empty() ? nullptr : (LPVOID)body.data();
    BOOL ok = WinHttpSendRequest(req, headers, (DWORD)wcslen(headers), bodyPtr, bodyLen, bodyLen, 0);
    if (ok) ok = WinHttpReceiveResponse(req, nullptr);

    response.clear();
    if (ok) {
        DWORD avail = 0;
        do {
            WinHttpQueryDataAvailable(req, &avail);
            if (avail == 0) break;
            std::vector<char> buf(avail);
            DWORD read = 0;
            if (!WinHttpReadData(req, buf.data(), avail, &read) || read == 0) break;
            response.append(buf.data(), read);
        } while (avail > 0);
    }
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    return ok;
}

// ---------------- 简单 JSON 取值 ----------------
static std::wstring JsonStr(const std::wstring& j, const wchar_t* key) {
    std::wstring pat = L"\"" + std::wstring(key) + L"\":\"";
    size_t p = j.find(pat);
    if (p == std::wstring::npos) return L"";
    p += pat.size();
    size_t e = j.find(L'"', p);
    if (e == std::wstring::npos) return L"";
    return j.substr(p, e - p);
}

static long JsonInt(const std::wstring& j, const wchar_t* key) {
    std::wstring pat = L"\"" + std::wstring(key) + L"\":";
    size_t p = j.find(pat);
    if (p == std::wstring::npos) return -1;
    p += pat.size();
    return wcstol(j.c_str() + p, nullptr, 10);
}

// ---------------- 登录线程 ----------------
static void PostStatus(HWND wnd, const std::wstring& msg) {
    std::wstring* m = new std::wstring(msg);
    PostMessageW(wnd, WM_APP_STATUS, 0, (LPARAM)m);
}

static void LoginThreadProc(Config cfg, HWND mainWnd) {
    auto done = [&](bool ok) { PostMessageW(mainWnd, WM_APP_DONE, ok ? 1 : 0, 0); };

    if (!cfg.network_name.empty()) {
        PostStatus(mainWnd, L"正在连接网络：" + cfg.network_name + L"…");
        ConnectToNetwork(cfg.network_name);
        Sleep(3000);
    }

    std::wstring base = Trim(cfg.auth_url);   // auth_url 现在表示门户基础地址（留空自动探测）
    if (base.empty()) {
        PostStatus(mainWnd, L"正在探测认证门户地址…");
        PortalProbe pr = ProbePortal(cfg.probe_url);
        if (pr.online) {
            PostStatus(mainWnd, L"当前已联网，无需登录");
            done(true);
            return;
        }
        if (!pr.portalUrl.empty()) base = BaseOfUrl(pr.portalUrl);
    }
    if (base.empty()) {
        PostStatus(mainWnd, L"未找到认证门户地址");
        done(false);
        return;
    }

    // 1) 获取认证配置（auth_type、本机在门户侧的 IP）
    PostStatus(mainWnd, L"正在获取认证配置…");
    std::string resp;
    if (!HttpPostApi(base, L"route=portal&action=load_portal_conf&device=pc", std::string(), resp)) {
        PostStatus(mainWnd, L"连接认证服务器失败");
        done(false);
        return;
    }
    std::wstring conf = GbkToWide(resp);
    long c = JsonInt(conf, L"code");
    if (c != 0) {
        PostStatus(mainWnd, L"获取配置失败：" + JsonStr(conf, L"msg"));
        done(false);
        return;
    }
    std::wstring ip = JsonStr(conf, L"wlanuserip");
    std::wstring authType = JsonStr(conf, L"auth1");

    // 2) 获取验证码并解密
    if (!HttpPostApi(base, L"route=webauth&action=get_verify_code&ip=" + ip, std::string(), resp)) {
        PostStatus(mainWnd, L"获取验证码失败");
        done(false);
        return;
    }
    std::wstring vc = GbkToWide(resp);
    std::wstring captcha = AesDecryptHex(JsonStr(vc, L"data"));
    if (captcha.empty()) {
        PostStatus(mainWnd, L"验证码解析失败");
        done(false);
        return;
    }

    // 3) 登录
    PostStatus(mainWnd, L"正在登录…");
    std::wstring codeEnc = AesEncryptHex(captcha);
    std::wstring pwdEnc = AesEncryptHex(cfg.password);
    std::wstring query = L"route=webauth&action=user_login&auth_type=" + authType +
                         L"&ip=" + ip + L"&code=" + codeEnc +
                         L"&username=" + cfg.username + L"&password=" + pwdEnc + L"&remember_me=0";
    std::string body = "u8name=" + WideToUtf8(cfg.username);
    if (!HttpPostApi(base, query, body, resp)) {
        PostStatus(mainWnd, L"登录请求失败");
        done(false);
        return;
    }
    std::wstring lr = GbkToWide(resp);
    long lcode = JsonInt(lr, L"code");
    if (lcode == 0) {
        PostStatus(mainWnd, L"登录成功，已联网");
        done(true);
    } else {
        PostStatus(mainWnd, L"登录失败：" + JsonStr(lr, L"msg"));
        done(false);
    }
}

// ---------------- LoginRunner ----------------
LoginRunner::LoginRunner(HWND mainWindow) : mainWnd_(mainWindow) {}

LoginRunner::~LoginRunner() { Shutdown(); }

bool LoginRunner::Start(const Config& cfg) {
    if (running_.exchange(true)) return false;
    std::thread(LoginThreadProc, cfg, mainWnd_).detach();
    return true;
}

void LoginRunner::Shutdown() {
    running_.store(false);
}
