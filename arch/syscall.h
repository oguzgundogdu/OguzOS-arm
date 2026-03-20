#pragma once

#include "types.h"

/* ── Syscall numbers ─────────────────────────────────────────────────
 * Convention: x8 = syscall number, x0–x5 = arguments, x0 = return.
 */

/* Filesystem */
constexpr u64 SYS_FS_CAT           = 0;
constexpr u64 SYS_FS_WRITE         = 1;
constexpr u64 SYS_FS_TOUCH         = 2;
constexpr u64 SYS_FS_MKDIR         = 3;
constexpr u64 SYS_FS_CD            = 4;
constexpr u64 SYS_FS_RM            = 5;
constexpr u64 SYS_FS_GET_CWD       = 6;
constexpr u64 SYS_FS_RESOLVE       = 7;
constexpr u64 SYS_FS_GET_NODE      = 8;
constexpr u64 SYS_FS_SYNC          = 9;
constexpr u64 SYS_FS_APPEND        = 10;

/* Syslog */
constexpr u64 SYS_SYSLOG_INFO      = 20;
constexpr u64 SYS_SYSLOG_ERROR     = 21;
constexpr u64 SYS_SYSLOG_WARN      = 22;

/* GUI */
constexpr u64 SYS_GUI_OPEN_APP     = 30;
constexpr u64 SYS_GUI_OPEN_FILE    = 31;
constexpr u64 SYS_GUI_WIN_COUNT    = 32;
constexpr u64 SYS_GUI_WIN_TITLE    = 33;
constexpr u64 SYS_GUI_WIN_ACTIVE   = 34;
constexpr u64 SYS_GUI_WIN_TYPE     = 35;
constexpr u64 SYS_GUI_WIN_APP_ID   = 36;

/* App registry */
constexpr u64 SYS_APPS_COUNT       = 40;
constexpr u64 SYS_APPS_GET_NAME    = 41;
constexpr u64 SYS_APPS_GET_ID      = 42;
constexpr u64 SYS_APPS_FIND        = 43;

/* Settings */
constexpr u64 SYS_SET_GET_TZ       = 50;
constexpr u64 SYS_SET_SET_TZ       = 51;
constexpr u64 SYS_SET_GET_COLOR    = 52;
constexpr u64 SYS_SET_SET_COLOR    = 53;
constexpr u64 SYS_SET_GET_KBD      = 54;
constexpr u64 SYS_SET_SET_KBD      = 55;
constexpr u64 SYS_SET_SAVE         = 56;
constexpr u64 SYS_SET_GET_RES_W    = 57;
constexpr u64 SYS_SET_GET_RES_H    = 58;
constexpr u64 SYS_SET_SET_RES      = 59;

/* File associations */
constexpr u64 SYS_ASSOC_GET        = 60;
constexpr u64 SYS_ASSOC_SET        = 61;
constexpr u64 SYS_ASSOC_FIND       = 62;
constexpr u64 SYS_ASSOC_UNSET      = 63;
constexpr u64 SYS_ASSOC_COUNT      = 64;
constexpr u64 SYS_ASSOC_SAVE       = 65;
constexpr u64 SYS_ASSOC_EXT_AT     = 66;
constexpr u64 SYS_ASSOC_APP_AT     = 67;

/* Start menu */
constexpr u64 SYS_MENU_COUNT       = 70;
constexpr u64 SYS_MENU_GET_LABEL   = 71;
constexpr u64 SYS_MENU_GET_TYPE    = 72;
constexpr u64 SYS_MENU_ADD         = 73;
constexpr u64 SYS_MENU_REMOVE      = 74;
constexpr u64 SYS_MENU_MOVE        = 75;
constexpr u64 SYS_MENU_FIND        = 76;
constexpr u64 SYS_MENU_HAS_APP     = 77;
constexpr u64 SYS_MENU_SAVE        = 78;
constexpr u64 SYS_MENU_INSERT      = 79;

/* Environment */
constexpr u64 SYS_ENV_GET          = 80;
constexpr u64 SYS_ENV_SET          = 81;
constexpr u64 SYS_ENV_UNSET        = 82;
constexpr u64 SYS_ENV_COUNT        = 83;
constexpr u64 SYS_ENV_KEY_AT       = 84;
constexpr u64 SYS_ENV_VALUE_AT     = 85;

/* Network */
constexpr u64 SYS_NET_AVAILABLE    = 90;
constexpr u64 SYS_NET_PING         = 91;
constexpr u64 SYS_NET_CURL         = 92;
constexpr u64 SYS_NET_HTTP_GET_BIN = 93;
constexpr u64 SYS_NET_GET_EPOCH    = 94;
constexpr u64 SYS_NET_IFCONFIG     = 95;
constexpr u64 SYS_NET_NTP_SYNC     = 96;

/* UART */
constexpr u64 SYS_UART_CAPTURE_ON  = 100;
constexpr u64 SYS_UART_CAPTURE_OFF = 101;
constexpr u64 SYS_UART_PUTS        = 102;

/* Disk */
constexpr u64 SYS_DISK_AVAILABLE   = 110;
constexpr u64 SYS_DISK_CAPACITY    = 111;

/* Framebuffer */
constexpr u64 SYS_FB_WIDTH         = 120;
constexpr u64 SYS_FB_HEIGHT        = 121;
constexpr u64 SYS_FB_AVAILABLE     = 122;
constexpr u64 SYS_FB_SET_RES       = 123;

/* Keyboard */
constexpr u64 SYS_KBD_LAYOUT_NAME  = 130;
constexpr u64 SYS_KBD_LAYOUT_COUNT = 131;

/* C# interpreter */
constexpr u64 SYS_CS_RUN           = 140;
constexpr u64 SYS_CS_INIT          = 141;
constexpr u64 SYS_CS_CALL_DRAW     = 142;
constexpr u64 SYS_CS_CALL_CLICK    = 143;
constexpr u64 SYS_CS_CALL_KEY      = 144;
constexpr u64 SYS_CS_CALL_ARROW    = 145;
constexpr u64 SYS_CS_SHOULD_CLOSE  = 146;
constexpr u64 SYS_CS_HAS_ERROR     = 147;
constexpr u64 SYS_CS_GET_ERROR     = 148;
constexpr u64 SYS_CS_GUI_CLEANUP   = 149;
constexpr u64 SYS_CS_HAS_FUNC      = 150;
constexpr u64 SYS_CS_SET_DRAW_CTX  = 151;

/* Shell command execution (for GUI terminal) */
constexpr u64 SYS_CMD_EXEC         = 160;

/* Mouse */
constexpr u64 SYS_MOUSE_AVAILABLE  = 170;

/* Netdev */
constexpr u64 SYS_NETDEV_AVAILABLE = 180;

/* Special */
constexpr u64 SYS_EXIT             = 255;

/* ── Inline SVC helpers (for user-space code) ────────────────────── */

inline u64 _svc0(u64 nr) {
    register u64 x8 asm("x8") = nr;
    register u64 x0 asm("x0");
    asm volatile("svc #0" : "=r"(x0) : "r"(x8) : "memory");
    return x0;
}
inline u64 _svc1(u64 nr, u64 a0) {
    register u64 x8 asm("x8") = nr;
    register u64 x0 asm("x0") = a0;
    asm volatile("svc #0" : "+r"(x0) : "r"(x8) : "memory");
    return x0;
}
inline u64 _svc2(u64 nr, u64 a0, u64 a1) {
    register u64 x8 asm("x8") = nr;
    register u64 x0 asm("x0") = a0;
    register u64 x1 asm("x1") = a1;
    asm volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x8) : "memory");
    return x0;
}
inline u64 _svc3(u64 nr, u64 a0, u64 a1, u64 a2) {
    register u64 x8 asm("x8") = nr;
    register u64 x0 asm("x0") = a0;
    register u64 x1 asm("x1") = a1;
    register u64 x2 asm("x2") = a2;
    asm volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x8) : "memory");
    return x0;
}
inline u64 _svc4(u64 nr, u64 a0, u64 a1, u64 a2, u64 a3) {
    register u64 x8 asm("x8") = nr;
    register u64 x0 asm("x0") = a0;
    register u64 x1 asm("x1") = a1;
    register u64 x2 asm("x2") = a2;
    register u64 x3 asm("x3") = a3;
    asm volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3), "r"(x8) : "memory");
    return x0;
}
inline u64 _svc5(u64 nr, u64 a0, u64 a1, u64 a2, u64 a3, u64 a4) {
    register u64 x8 asm("x8") = nr;
    register u64 x0 asm("x0") = a0;
    register u64 x1 asm("x1") = a1;
    register u64 x2 asm("x2") = a2;
    register u64 x3 asm("x3") = a3;
    register u64 x4 asm("x4") = a4;
    asm volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x8) : "memory");
    return x0;
}
