#pragma once
#include <string>

struct Config {
    std::wstring network_name; // 登录网络的名称（SSID），留空则跳过自动连网
    std::wstring auth_url;    // 为空则自动探测门户（推荐）
    std::wstring probe_url;   // 探测是否联网 / 触发门户跳转的地址
    std::wstring username;
    std::wstring password;
    bool auto_login = false;  // 启动时自动登录
    bool auto_start = false;  // 开机自启
};

bool LoadConfig(const std::wstring& path, Config& cfg);
bool SaveConfig(const std::wstring& path, const Config& cfg);
std::wstring ConfigFilePath();
