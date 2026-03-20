#include "syscall.h"
#include "assoc.h"
#include "commands.h"
#include "csharp.h"
#include "disk.h"
#include "env.h"
#include "fb.h"
#include "fs.h"
#include "gui.h"
#include "menu.h"
#include "mouse.h"
#include "net.h"
#include "netdev.h"
#include "registry.h"
#include "settings.h"
#include "string.h"
#include "syslog.h"
#include "uart.h"

/* Transfer buffer in user-accessible memory (defined in graphics.cpp) */
extern char gfx_transfer_buf[8192];

/* Secondary transfer buffer for syscalls that need two string returns */
__attribute__((section(".userbuf")))
static char transfer2[4096];

/* fs::Node copy in user-accessible memory */
__attribute__((section(".userbuf")))
static fs::Node user_node;

/* ── Command execution support ──────────────────────────────────────── */
static char *_cmd_out_ptr;
static usize _cmd_out_pos;
static usize _cmd_out_cap;

static void cmd_buf_out(void * /*ctx*/, const char *text) {
    usize len = str::len(text);
    if (_cmd_out_pos + len >= _cmd_out_cap)
        len = (_cmd_out_cap > _cmd_out_pos) ? _cmd_out_cap - _cmd_out_pos - 1 : 0;
    str::memcpy(_cmd_out_ptr + _cmd_out_pos, text, len);
    _cmd_out_pos += len;
    _cmd_out_ptr[_cmd_out_pos] = '\0';
}

/* ── Copy a kernel string to the user transfer buffer ──────────────── */
static u64 copy_to_transfer(const char *src) {
    if (!src) return 0;
    usize len = str::len(src);
    if (len >= sizeof(gfx_transfer_buf))
        len = sizeof(gfx_transfer_buf) - 1;
    str::memcpy(gfx_transfer_buf, src, len);
    gfx_transfer_buf[len] = '\0';
    return (u64)gfx_transfer_buf;
}

static u64 copy_to_transfer2(const char *src) {
    if (!src) return 0;
    usize len = str::len(src);
    if (len >= sizeof(transfer2))
        len = sizeof(transfer2) - 1;
    str::memcpy(transfer2, src, len);
    transfer2[len] = '\0';
    return (u64)transfer2;
}

/* ── Main dispatcher ──────────────────────────────────────────────── *
 * Called from the exception handler with:
 *   nr   = syscall number (from x8)
 *   regs = pointer to saved {x0..x5} on the kernel stack
 * Returns the value to place in x0 on return to EL0.
 */
extern "C" u64 syscall_dispatch(u64 nr, u64 *regs) {
    switch (nr) {

    /* ── Filesystem ─────────────────────────────────────────────────── */
    case SYS_FS_CAT:
        return copy_to_transfer(fs::cat((const char *)regs[0]));
    case SYS_FS_WRITE:
        return fs::write((const char *)regs[0], (const char *)regs[1]) ? 1 : 0;
    case SYS_FS_TOUCH:
        return fs::touch((const char *)regs[0]) ? 1 : 0;
    case SYS_FS_MKDIR:
        return fs::mkdir((const char *)regs[0]) ? 1 : 0;
    case SYS_FS_CD:
        return fs::cd((const char *)regs[0]) ? 1 : 0;
    case SYS_FS_RM:
        return fs::rm((const char *)regs[0]) ? 1 : 0;
    case SYS_FS_GET_CWD:
        fs::get_cwd((char *)regs[0], (usize)regs[1]);
        return 0;
    case SYS_FS_RESOLVE:
        return (u64)(i64)fs::resolve((const char *)regs[0]);
    case SYS_FS_GET_NODE: {
        const fs::Node *n = fs::get_node((i32)regs[0]);
        if (!n) return 0;
        str::memcpy(&user_node, n, sizeof(fs::Node));
        return (u64)&user_node;
    }
    case SYS_FS_SYNC:
        return fs::sync_to_disk() ? 1 : 0;
    case SYS_FS_APPEND:
        return fs::append((const char *)regs[0], (const char *)regs[1]) ? 1 : 0;

    /* ── Syslog ─────────────────────────────────────────────────────── */
    case SYS_SYSLOG_INFO:
        syslog::info((const char *)regs[0], "%s", (const char *)regs[1]);
        return 0;
    case SYS_SYSLOG_ERROR:
        syslog::error((const char *)regs[0], "%s", (const char *)regs[1]);
        return 0;
    case SYS_SYSLOG_WARN:
        syslog::warn((const char *)regs[0], "%s", (const char *)regs[1]);
        return 0;

    /* ── GUI ────────────────────────────────────────────────────────── */
    case SYS_GUI_OPEN_APP:
        gui::open_app((const char *)regs[0]);
        return 0;
    case SYS_GUI_OPEN_FILE:
        gui::open_file((const char *)regs[0], (const char *)regs[1]);
        return 0;
    case SYS_GUI_WIN_COUNT:
        return (u64)gui::get_window_count();
    case SYS_GUI_WIN_TITLE:
        return copy_to_transfer(gui::get_window_title((i32)regs[0]));
    case SYS_GUI_WIN_ACTIVE:
        return gui::is_window_active((i32)regs[0]) ? 1 : 0;
    case SYS_GUI_WIN_TYPE:
        return (u64)gui::get_window_type((i32)regs[0]);
    case SYS_GUI_WIN_APP_ID:
        return copy_to_transfer(gui::get_window_app_id((i32)regs[0]));

    /* ── App registry ───────────────────────────────────────────────── */
    case SYS_APPS_COUNT:
        return (u64)apps::count();
    case SYS_APPS_GET_NAME: {
        const OgzApp *a = apps::get((i32)regs[0]);
        return a ? copy_to_transfer(a->name) : 0;
    }
    case SYS_APPS_GET_ID: {
        const OgzApp *a = apps::get((i32)regs[0]);
        return a ? copy_to_transfer(a->id) : 0;
    }
    case SYS_APPS_FIND:
        return apps::find((const char *)regs[0]) ? 1 : 0;

    /* ── Settings ───────────────────────────────────────────────────── */
    case SYS_SET_GET_TZ:     return (u64)(i64)settings::get_tz_offset();
    case SYS_SET_SET_TZ:     settings::set_tz_offset((i32)regs[0]); return 0;
    case SYS_SET_GET_COLOR:  return (u64)settings::get_desktop_color();
    case SYS_SET_SET_COLOR:  settings::set_desktop_color((u32)regs[0]); return 0;
    case SYS_SET_GET_KBD:    return (u64)(i64)settings::get_kbd_layout();
    case SYS_SET_SET_KBD:    settings::set_kbd_layout((i32)regs[0]); return 0;
    case SYS_SET_SAVE:       settings::save(); return 0;
    case SYS_SET_GET_RES_W:  return (u64)settings::get_res_w();
    case SYS_SET_GET_RES_H:  return (u64)settings::get_res_h();
    case SYS_SET_SET_RES:    settings::set_resolution((u32)regs[0], (u32)regs[1]); return 0;

    /* ── File associations ──────────────────────────────────────────── */
    case SYS_ASSOC_GET:
        return copy_to_transfer(assoc::get((const char *)regs[0]));
    case SYS_ASSOC_SET:
        assoc::set((const char *)regs[0], (const char *)regs[1]);
        return 0;
    case SYS_ASSOC_FIND:
        return copy_to_transfer(assoc::find_for_file((const char *)regs[0]));
    case SYS_ASSOC_UNSET:
        return assoc::unset((const char *)regs[0]) ? 1 : 0;
    case SYS_ASSOC_COUNT:
        return (u64)assoc::count();
    case SYS_ASSOC_SAVE:
        assoc::save(); return 0;
    case SYS_ASSOC_EXT_AT:
        return copy_to_transfer(assoc::ext_at((i32)regs[0]));
    case SYS_ASSOC_APP_AT:
        return copy_to_transfer2(assoc::app_at((i32)regs[0]));

    /* ── Start menu ─────────────────────────────────────────────────── */
    case SYS_MENU_COUNT:
        return (u64)menu::count();
    case SYS_MENU_GET_LABEL: {
        const menu::Entry *e = menu::get((i32)regs[0]);
        return e ? copy_to_transfer(e->label) : 0;
    }
    case SYS_MENU_GET_TYPE: {
        const menu::Entry *e = menu::get((i32)regs[0]);
        return e ? (u64)e->type : (u64)-1;
    }
    case SYS_MENU_ADD:
        return (u64)menu::add((menu::EntryType)regs[0], (const char *)regs[1], (const char *)regs[2]);
    case SYS_MENU_REMOVE:
        return menu::remove((i32)regs[0]) ? 1 : 0;
    case SYS_MENU_MOVE:
        return menu::move((i32)regs[0], (i32)regs[1]) ? 1 : 0;
    case SYS_MENU_FIND:
        return (u64)(i64)menu::find((const char *)regs[0]);
    case SYS_MENU_HAS_APP:
        return menu::has_app((const char *)regs[0]) ? 1 : 0;
    case SYS_MENU_SAVE:
        menu::save(); return 0;
    case SYS_MENU_INSERT:
        return menu::insert((i32)regs[0], (menu::EntryType)regs[1],
                            (const char *)regs[2], (const char *)regs[3]) ? 1 : 0;

    /* ── Environment ────────────────────────────────────────────────── */
    case SYS_ENV_GET:
        return copy_to_transfer(env::get((const char *)regs[0]));
    case SYS_ENV_SET:
        env::set((const char *)regs[0], (const char *)regs[1]); return 0;
    case SYS_ENV_UNSET:
        return env::unset((const char *)regs[0]) ? 1 : 0;
    case SYS_ENV_COUNT:
        return (u64)env::count();
    case SYS_ENV_KEY_AT:
        return copy_to_transfer(env::key_at((i32)regs[0]));
    case SYS_ENV_VALUE_AT:
        return copy_to_transfer(env::value_at((i32)regs[0]));

    /* ── Network ────────────────────────────────────────────────────── */
    case SYS_NET_AVAILABLE:
        return net::is_available() ? 1 : 0;
    case SYS_NET_PING:
        net::ping((const char *)regs[0], (u32)regs[1]); return 0;
    case SYS_NET_CURL:
        net::curl((const char *)regs[0]); return 0;
    case SYS_NET_HTTP_GET_BIN:
        return (u64)net::http_get_bin((const char *)regs[0], (u8 *)regs[1], (u32)regs[2]);
    case SYS_NET_GET_EPOCH:
        return net::get_epoch();
    case SYS_NET_IFCONFIG:
        net::ifconfig(); return 0;
    case SYS_NET_NTP_SYNC:
        return net::ntp_sync() ? 1 : 0;

    /* ── UART ───────────────────────────────────────────────────────── */
    case SYS_UART_CAPTURE_ON:
        uart::capture_start((char *)regs[0], (usize)regs[1]); return 0;
    case SYS_UART_CAPTURE_OFF:
        uart::capture_stop(); return 0;
    case SYS_UART_PUTS:
        uart::puts((const char *)regs[0]); return 0;

    /* ── Disk ───────────────────────────────────────────────────────── */
    case SYS_DISK_AVAILABLE:
        return disk::is_available() ? 1 : 0;
    case SYS_DISK_CAPACITY:
        return (u64)disk::get_capacity();

    /* ── Framebuffer ────────────────────────────────────────────────── */
    case SYS_FB_WIDTH:
        return (u64)fb::width();
    case SYS_FB_HEIGHT:
        return (u64)fb::height();
    case SYS_FB_AVAILABLE:
        return fb::is_available() ? 1 : 0;
    case SYS_FB_SET_RES:
        return fb::set_resolution((u32)regs[0], (u32)regs[1]) ? 1 : 0;

    /* ── Keyboard ───────────────────────────────────────────────────── */
    case SYS_KBD_LAYOUT_NAME: {
        /* Layout names are hardcoded — no kernel API exists */
        static const char *names[] = {"US","UK","TR","DE","FR","ES","IT","PT"};
        i32 idx = (i32)regs[0];
        if (idx >= 0 && idx < 8) return copy_to_transfer(names[idx]);
        return 0;
    }
    case SYS_KBD_LAYOUT_COUNT:
        return 8;

    /* ── C# interpreter ─────────────────────────────────────────────── */
    case SYS_CS_RUN:
        return csharp::run((const char *)regs[0], (char *)regs[1], (i32)regs[2]) ? 1 : 0;
    case SYS_CS_INIT:
        return csharp::init((const char *)regs[0]) ? 1 : 0;
    case SYS_CS_CALL_DRAW:
        csharp::call_draw(); return 0;
    case SYS_CS_CALL_CLICK:
        csharp::call_click((i32)regs[0], (i32)regs[1]); return 0;
    case SYS_CS_CALL_KEY:
        return csharp::call_key((char)regs[0]) ? 1 : 0;
    case SYS_CS_CALL_ARROW:
        csharp::call_arrow((char)regs[0]); return 0;
    case SYS_CS_SHOULD_CLOSE:
        return csharp::should_close() ? 1 : 0;
    case SYS_CS_HAS_ERROR:
        return csharp::has_error() ? 1 : 0;
    case SYS_CS_GET_ERROR:
        return copy_to_transfer(csharp::get_error());
    case SYS_CS_GUI_CLEANUP:
        csharp::gui_cleanup(); return 0;
    case SYS_CS_HAS_FUNC:
        return csharp::has_func((const char *)regs[0]) ? 1 : 0;
    case SYS_CS_SET_DRAW_CTX:
        csharp::set_draw_ctx((i32)regs[0], (i32)regs[1], (i32)regs[2], (i32)regs[3]);
        return 0;

    /* ── Shell command execution ────────────────────────────────────── */
    case SYS_CMD_EXEC: {
        _cmd_out_ptr = (char *)regs[1];
        _cmd_out_pos = 0;
        _cmd_out_cap = (usize)regs[2];
        if (_cmd_out_cap > 0) _cmd_out_ptr[0] = '\0';
        const char *line = (const char *)regs[0];

        /* Parse command name */
        char tmp[200];
        str::ncpy(tmp, line, 199);
        char *p = tmp;
        while (*p == ' ') p++;
        char *arg = p;
        while (*arg && *arg != ' ') arg++;
        if (*arg == ' ') { *arg = '\0'; arg++; while (*arg == ' ') arg++; }
        char *arg2 = arg;
        while (*arg2 && *arg2 != ' ') arg2++;
        if (*arg2 == ' ') { *arg2 = '\0'; arg2++; while (*arg2 == ' ') arg2++; }

        /* Dispatch to cmd:: functions */
        if (str::cmp(p, "cp") == 0)       cmd::cp(cmd_buf_out, nullptr, arg, arg2);
        else if (str::cmp(p, "mv") == 0)  cmd::mv(cmd_buf_out, nullptr, arg, arg2);
        else if (str::cmp(p, "head") == 0) cmd::head(cmd_buf_out, nullptr, arg, 10);
        else if (str::cmp(p, "tail") == 0) cmd::tail(cmd_buf_out, nullptr, arg, 10);
        else if (str::cmp(p, "wc") == 0)  cmd::wc(cmd_buf_out, nullptr, arg);
        else if (str::cmp(p, "find") == 0) cmd::find(cmd_buf_out, nullptr, arg);
        else if (str::cmp(p, "tree") == 0) cmd::tree(cmd_buf_out, nullptr, *arg ? arg : ".", 3);
        else if (str::cmp(p, "df") == 0)  cmd::df(cmd_buf_out, nullptr);
        else if (str::cmp(p, "grep") == 0) cmd::grep(cmd_buf_out, nullptr, arg, arg2);
        else if (str::cmp(p, "csrun") == 0) cmd::csrun(cmd_buf_out, nullptr, arg);
        else if (str::cmp(p, "date") == 0) cmd::date(cmd_buf_out, nullptr);
        else if (str::cmp(p, "hostname") == 0) cmd::hostname(cmd_buf_out, nullptr);
        else if (str::cmp(p, "whoami") == 0) cmd::whoami(cmd_buf_out, nullptr);
        else if (str::cmp(p, "free") == 0) cmd::free_cmd(cmd_buf_out, nullptr);
        else if (str::cmp(p, "dmesg") == 0) cmd::dmesg(cmd_buf_out, nullptr);
        else if (str::cmp(p, "which") == 0) cmd::which(cmd_buf_out, nullptr, arg);
        else if (str::cmp(p, "xxd") == 0) cmd::xxd(cmd_buf_out, nullptr, arg);
        return _cmd_out_pos;
    }

    /* ── Mouse ──────────────────────────────────────────────────────── */
    case SYS_MOUSE_AVAILABLE:
        return mouse::is_available() ? 1 : 0;

    /* ── Netdev ─────────────────────────────────────────────────────── */
    case SYS_NETDEV_AVAILABLE:
        return netdev::is_available() ? 1 : 0;

    /* ── Exit (return from EL0 callback) ────────────────────────────── */
    case SYS_EXIT:
        /* Handled directly in exception.S — should not reach here */
        return 0;

    default:
        syslog::warn("syscall", "unknown syscall %d", (i32)nr);
        return (u64)-1;
    }
}
