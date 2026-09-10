# 校园网自动登录（中国移动校园网 · Panabit 门户）

一个用 C++ 编写的 Windows 桌面小程序，用于自动登录中国移动校园网的强制门户（Captive Portal）。
该门户由 **Panabit 网关** 提供，程序直接调用其 HTTP 接口完成认证，无需浏览器、无需人工输入验证码。

## 功能特点

- 一键登录，支持「开机自启」与「启动时自动登录」。
- 可自动连接指定 WiFi（SSID）。
- 自动探测认证门户地址，适配「验证链接会随连接变化」的场景。
- 自动获取并解密图形验证码（**无需 OCR**）。
- 账号、密码、门户地址、网络名称均可在界面中修改。
- 密码与验证码经 AES 加密后提交，不发送明文。
- 简约风 GUI，无第三方运行时依赖。

## 技术栈

- C++17
- Win32 GUI（原生控件）
- WinHTTP（HTTP 请求）
- BCrypt（AES 加密，Windows 系统自带）
- 无 Qt / 无 WebView2 / 无第三方库

## 工作原理

```
连接网络(可选) → 探测联网状态/门户地址 → 获取认证配置
            → 获取验证码(AES解密) → 提交登录 → 校验结果
```

具体对应门户接口：

1. `GET/POST /api?route=portal&action=load_portal_conf` —— 获取认证类型 `auth1` 与门户侧本机 IP `wlanuserip`。
2. `POST /api?route=webauth&action=get_verify_code&ip=...` —— 获取加密验证码。
3. `POST /api?route=webauth&action=user_login&...` —— 提交账号、加密后的密码与验证码。

## 加密细节

门户前端 `crypto.js` 对密码与验证码使用：

- 算法：AES
- 模式：ECB
- 密钥：`Panabit@1024_key`（16 字节，即 AES-128）
- 填充：ZeroPadding
- 输出：小写 hex

程序用 Windows 自带的 BCrypt 实现同样的加密，已验证与门户输出完全一致。

## 目录结构

```
campus-net-login/
├── campus-login.exe      # 编译产物
├── config.ini            # 配置文件（账号/密码/网络名称等）
├── build.bat             # 一键构建脚本
├── README.md
└── src/
    ├── main.cpp          # Win32 界面与主流程
    ├── config.h/.cpp     # 配置读写
    ├── login.h/.cpp      # 协议登录（WinHTTP + BCrypt）
    └── resource.h        # 控件 / 消息 ID
```

## 构建

依赖：Windows 10/11 + Visual Studio（含「使用 C++ 的桌面开发」组件）。

```bat
build.bat
```

产物为 `campus-login.exe`。无额外第三方依赖。

## 使用

1. 双击 `campus-login.exe`。
2. 填写「网络名称」（要连的 WiFi）、账号、密码；门户地址留空则自动探测。
3. 点「立即登录」，或勾选「开机自启」「启动时自动登录」后点「保存设置」。

「开机自启」会写入当前用户启动项 `HKCU\...\Run`（无需管理员权限），开机时以 `--autologin` 参数静默登录。

## 注意事项

- 账号密码以明文保存在本机 `config.ini`，请勿外传。
- 本工具仅供登录本人校园网账号使用，请遵守学校网络管理规定。
