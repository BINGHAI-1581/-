#include "config.h"

#include <windows.h>
#include <shlwapi.h>

#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>

static std::string W2U8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

static std::wstring U82W(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

static std::string Trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return std::string();
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

static bool ParseBool(const std::string& v) {
    std::string t = Trim(v);
    return t == "1" || t == "true" || t == "yes" || t == "on";
}

bool LoadConfig(const std::wstring& path, Config& cfg) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return false;

    std::stringstream ss;
    ss << in.rdbuf();
    std::string content = ss.str();
    in.close();

    std::stringstream lines(content);
    std::string line;
    while (std::getline(lines, line)) {
        std::string t = Trim(line);
        if (t.empty() || t[0] == '#' || t[0] == ';') continue;
        size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string key = Trim(t.substr(0, eq));
        std::string val = Trim(t.substr(eq + 1));
        if (key == "network_name") cfg.network_name = U82W(val);
        else if (key == "auth_url") cfg.auth_url = U82W(val);
        else if (key == "probe_url") cfg.probe_url = U82W(val);
        else if (key == "username") cfg.username = U82W(val);
        else if (key == "password") cfg.password = U82W(val);
        else if (key == "auto_login") cfg.auto_login = ParseBool(val);
        else if (key == "auto_start") cfg.auto_start = ParseBool(val);
    }
    if (cfg.probe_url.empty())
        cfg.probe_url = L"http://www.msftconnecttest.com/redirect";
    return true;
}

bool SaveConfig(const std::wstring& path, const Config& cfg) {
    std::ostringstream os;
    os << "# 校园网自动登录配置\n"
       << "# network_name: 登录网络名称（SSID）；留空则跳过自动连网\n"
       << "# auth_url: 手动指定登录页地址；留空则自动探测门户（推荐）\n"
       << "# probe_url: 用于检测是否已联网 / 触发门户跳转的探测地址\n"
       << "network_name=" << W2U8(cfg.network_name) << "\n"
       << "auth_url=" << W2U8(cfg.auth_url) << "\n"
       << "probe_url=" << W2U8(cfg.probe_url) << "\n"
       << "username=" << W2U8(cfg.username) << "\n"
       << "password=" << W2U8(cfg.password) << "\n"
       << "auto_login=" << (cfg.auto_login ? 1 : 0) << "\n"
       << "auto_start=" << (cfg.auto_start ? 1 : 0) << "\n";

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;
    out << os.str();
    out.close();
    return true;
}

std::wstring ConfigFilePath() {
    wchar_t buf[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring exe(buf);
    size_t slash = exe.find_last_of(L"\\/");
    if (slash != std::wstring::npos) exe.resize(slash + 1);
    return exe + L"config.ini";
}
