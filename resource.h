#pragma once

// 控件 ID
#define IDC_AUTH_URL    1001
#define IDC_USERNAME    1002
#define IDC_PASSWORD    1003
#define IDC_AUTO_START  1004
#define IDC_AUTO_LOGIN  1005
#define IDC_BTN_LOGIN   1006
#define IDC_BTN_SAVE    1007
#define IDC_BTN_CLOSE   1008
#define IDC_STATUS      1009
#define IDC_NETWORK     1010

// 自定义消息
#define WM_APP_STATUS    (WM_APP + 1)
#define WM_APP_DONE      (WM_APP + 2)
#define WM_APP_AUTOSTART (WM_APP + 3)

// 定时器 ID
#define TIMER_AUTOCLOSE   1
#define TIMER_AUTOSTART   2
#define TIMER_LOGIN_CONFIRM 3
#define TIMER_RETRY       4
