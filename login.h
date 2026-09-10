#pragma once
#include <windows.h>
#include <atomic>

#include "config.h"

// 协议级登录：直接调用 Panabit 门户的 /api 接口完成认证（无需浏览器、无需验证码 OCR）。
class LoginRunner {
public:
    LoginRunner(HWND mainWindow);
    ~LoginRunner();

    bool Start(const Config& cfg);   // 后台线程执行登录
    void Shutdown();
    void MarkDone() { running_.store(false); }
    bool IsRunning() const { return running_.load(); }

private:
    HWND mainWnd_;
    std::atomic<bool> running_{false};
};
