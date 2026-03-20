#pragma once

/*
 * User-space API compatibility layer.
 *
 * Under -DUSERSPACE, provides SVC-wrapped versions of kernel services.
 * Apps keep using gfx::fill_rect(), fs::cat(), etc. — they resolve
 * to user-space stubs that issue SVC to the kernel.
 */

#ifdef USERSPACE

#include "types.h"
#include "syscall.h"
#include "ugfx.h"
#include "string.h"   /* str:: is pure, works at EL0 */

/* ── User-space filesystem ──────────────────────────────────────────── */
namespace ufs {
    /* Mirror the fs:: types for user code */
    constexpr usize MAX_NAME = 64;
    constexpr usize MAX_CONTENT = 8192;
    constexpr usize MAX_CHILDREN = 32;
    constexpr usize MAX_NODES = 128;

    enum class NodeType : u8 { File, Directory };

    struct Node {
        char name[MAX_NAME];
        NodeType type;
        bool used;
        char content[MAX_CONTENT];
        usize content_len;
        i32 children[MAX_CHILDREN];
        usize child_count;
        i32 parent;
    };

    inline const char* cat(const char *name)
        { return (const char*)_svc1(SYS_FS_CAT, (u64)name); }
    inline bool write(const char *name, const char *content)
        { return _svc2(SYS_FS_WRITE, (u64)name, (u64)content) != 0; }
    inline bool touch(const char *name)
        { return _svc1(SYS_FS_TOUCH, (u64)name) != 0; }
    inline bool mkdir(const char *name)
        { return _svc1(SYS_FS_MKDIR, (u64)name) != 0; }
    inline bool cd(const char *path)
        { return _svc1(SYS_FS_CD, (u64)path) != 0; }
    inline bool rm(const char *name)
        { return _svc1(SYS_FS_RM, (u64)name) != 0; }
    inline void get_cwd(char *buf, usize size)
        { _svc2(SYS_FS_GET_CWD, (u64)buf, size); }
    inline i32 resolve(const char *path)
        { return (i32)(i64)_svc1(SYS_FS_RESOLVE, (u64)path); }
    inline const Node* get_node(i32 idx)
        { return (const Node*)_svc1(SYS_FS_GET_NODE, (u64)idx); }
    inline bool sync_to_disk()
        { return _svc0(SYS_FS_SYNC) != 0; }
    inline bool append(const char *name, const char *content)
        { return _svc2(SYS_FS_APPEND, (u64)name, (u64)content) != 0; }
}

/* ── User-space syslog ──────────────────────────────────────────────── */
namespace usyslog {
    /* Variadic overloads that discard format args (no snprintf in freestanding).
       The format string itself is still logged, which is useful for debugging. */
    template<typename... Args>
    inline void info(const char *sub, const char *msg, Args...)
        { _svc2(SYS_SYSLOG_INFO, (u64)sub, (u64)msg); }
    template<typename... Args>
    inline void error(const char *sub, const char *msg, Args...)
        { _svc2(SYS_SYSLOG_ERROR, (u64)sub, (u64)msg); }
    template<typename... Args>
    inline void warn(const char *sub, const char *msg, Args...)
        { _svc2(SYS_SYSLOG_WARN, (u64)sub, (u64)msg); }
    template<typename... Args>
    inline void debug(const char *sub, const char *msg, Args...)
        { _svc2(SYS_SYSLOG_INFO, (u64)sub, (u64)msg); }
}

/* ── User-space GUI ─────────────────────────────────────────────────── */
namespace ugui {
    inline void open_app(const char *id)
        { _svc1(SYS_GUI_OPEN_APP, (u64)id); }
    inline void open_file(const char *path, const char *content)
        { _svc2(SYS_GUI_OPEN_FILE, (u64)path, (u64)content); }
    inline i32 get_window_count()
        { return (i32)_svc0(SYS_GUI_WIN_COUNT); }
    inline const char* get_window_title(i32 idx)
        { return (const char*)_svc1(SYS_GUI_WIN_TITLE, (u64)idx); }
    inline bool is_window_active(i32 idx)
        { return _svc1(SYS_GUI_WIN_ACTIVE, (u64)idx) != 0; }
    inline i32 get_window_type(i32 idx)
        { return (i32)_svc1(SYS_GUI_WIN_TYPE, (u64)idx); }
    inline const char* get_window_app_id(i32 idx)
        { return (const char*)_svc1(SYS_GUI_WIN_APP_ID, (u64)idx); }
    /* Window type constants (mirror gui.cpp WinType enum) */
    constexpr i32 WTYPE_EXPLORER = 0;
    constexpr i32 WTYPE_TEXTVIEW = 1;
    constexpr i32 WTYPE_APP      = 2;
}

/* ── User-space app registry ────────────────────────────────────────── */
namespace uapps {
    inline i32 count()
        { return (i32)_svc0(SYS_APPS_COUNT); }
    inline const char* get_name(i32 idx)
        { return (const char*)_svc1(SYS_APPS_GET_NAME, (u64)idx); }
    inline const char* get_id(i32 idx)
        { return (const char*)_svc1(SYS_APPS_GET_ID, (u64)idx); }
    inline bool find(const char *id)
        { return _svc1(SYS_APPS_FIND, (u64)id) != 0; }
}

/* ── User-space settings ────────────────────────────────────────────── */
namespace usettings {
    inline i32 get_tz_offset()      { return (i32)(i64)_svc0(SYS_SET_GET_TZ); }
    inline void set_tz_offset(i32 m){ _svc1(SYS_SET_SET_TZ, (u64)(i64)m); }
    inline u32 get_desktop_color()  { return (u32)_svc0(SYS_SET_GET_COLOR); }
    inline void set_desktop_color(u32 c) { _svc1(SYS_SET_SET_COLOR, (u64)c); }
    inline i32 get_kbd_layout()     { return (i32)(i64)_svc0(SYS_SET_GET_KBD); }
    inline void set_kbd_layout(i32 i){ _svc1(SYS_SET_SET_KBD, (u64)(i64)i); }
    inline void save()              { _svc0(SYS_SET_SAVE); }
    inline u32 get_res_w()          { return (u32)_svc0(SYS_SET_GET_RES_W); }
    inline u32 get_res_h()          { return (u32)_svc0(SYS_SET_GET_RES_H); }
    inline void set_resolution(u32 w, u32 h) { _svc2(SYS_SET_SET_RES, w, h); }
}

/* ── User-space file associations ───────────────────────────────────── */
namespace uassoc {
    inline const char* get(const char *ext)
        { return (const char*)_svc1(SYS_ASSOC_GET, (u64)ext); }
    inline void set(const char *ext, const char *app)
        { _svc2(SYS_ASSOC_SET, (u64)ext, (u64)app); }
    inline const char* find_for_file(const char *name)
        { return (const char*)_svc1(SYS_ASSOC_FIND, (u64)name); }
    inline bool unset(const char *ext)
        { return _svc1(SYS_ASSOC_UNSET, (u64)ext) != 0; }
    inline i32 count()
        { return (i32)_svc0(SYS_ASSOC_COUNT); }
    inline void save()
        { _svc0(SYS_ASSOC_SAVE); }
    /* These query by index — reuse the transfer buffer approach.
       We need separate syscalls; for now route through assoc::get. */
    inline const char* ext_at(i32 idx)
        { return (const char*)_svc1(SYS_ASSOC_EXT_AT, (u64)idx); }
    inline const char* app_at(i32 idx)
        { return (const char*)_svc1(SYS_ASSOC_APP_AT, (u64)idx); }
}

/* ── User-space menu ────────────────────────────────────────────────── */
namespace umenu {
    inline i32 count()
        { return (i32)_svc0(SYS_MENU_COUNT); }
    inline const char* get_label(i32 idx)
        { return (const char*)_svc1(SYS_MENU_GET_LABEL, (u64)idx); }
    inline i32 get_type(i32 idx)
        { return (i32)_svc1(SYS_MENU_GET_TYPE, (u64)idx); }
    inline i32 add(i32 type, const char *label, const char *id)
        { return (i32)_svc3(SYS_MENU_ADD, (u64)type, (u64)label, (u64)id); }
    inline bool remove(i32 idx)
        { return _svc1(SYS_MENU_REMOVE, (u64)idx) != 0; }
    inline bool move(i32 from, i32 to)
        { return _svc2(SYS_MENU_MOVE, (u64)from, (u64)to) != 0; }
    inline i32 find(const char *id)
        { return (i32)(i64)_svc1(SYS_MENU_FIND, (u64)id); }
    inline bool has_app(const char *id)
        { return _svc1(SYS_MENU_HAS_APP, (u64)id) != 0; }
    inline void save()
        { _svc0(SYS_MENU_SAVE); }
    inline bool insert(i32 pos, i32 type, const char *label, const char *id)
        { return _svc4(SYS_MENU_INSERT, (u64)pos, (u64)type, (u64)label, (u64)id) != 0; }
}

/* ── User-space environment ─────────────────────────────────────────── */
namespace uenv {
    inline const char* get(const char *key)
        { return (const char*)_svc1(SYS_ENV_GET, (u64)key); }
    inline void set(const char *key, const char *val)
        { _svc2(SYS_ENV_SET, (u64)key, (u64)val); }
    inline bool unset(const char *key)
        { return _svc1(SYS_ENV_UNSET, (u64)key) != 0; }
    inline i32 count()
        { return (i32)_svc0(SYS_ENV_COUNT); }
    inline const char* key_at(i32 idx)
        { return (const char*)_svc1(SYS_ENV_KEY_AT, (u64)idx); }
    inline const char* value_at(i32 idx)
        { return (const char*)_svc1(SYS_ENV_VALUE_AT, (u64)idx); }
    inline const char* resolve_command(const char *name)
        { return (const char*)_svc1(SYS_ENV_GET, (u64)name); }
}

/* ── User-space network ─────────────────────────────────────────────── */
namespace unet {
    inline bool is_available()
        { return _svc0(SYS_NET_AVAILABLE) != 0; }
    inline void ping(const char *target, u32 count)
        { _svc2(SYS_NET_PING, (u64)target, (u64)count); }
    inline void curl(const char *url)
        { _svc1(SYS_NET_CURL, (u64)url); }
    inline u32 http_get_bin(const char *url, u8 *buf, u32 sz)
        { return (u32)_svc3(SYS_NET_HTTP_GET_BIN, (u64)url, (u64)buf, (u64)sz); }
    inline u64 get_epoch()
        { return _svc0(SYS_NET_GET_EPOCH); }
    inline void ifconfig()
        { _svc0(SYS_NET_IFCONFIG); }
    inline bool ntp_sync()
        { return _svc0(SYS_NET_NTP_SYNC) != 0; }
    inline void dhcp()
        { /* re-run DHCP — not implemented as syscall, no-op */ }
}

/* ── User-space UART ────────────────────────────────────────────────── */
namespace uuart {
    inline void capture_start(char *buf, usize size)
        { _svc2(SYS_UART_CAPTURE_ON, (u64)buf, size); }
    inline void capture_stop()
        { _svc0(SYS_UART_CAPTURE_OFF); }
    inline void puts(const char *s)
        { _svc1(SYS_UART_PUTS, (u64)s); }
}

/* ── User-space disk ────────────────────────────────────────────────── */
namespace udisk {
    inline bool is_available()
        { return _svc0(SYS_DISK_AVAILABLE) != 0; }
    inline u64 get_capacity()
        { return _svc0(SYS_DISK_CAPACITY); }
}

/* ── User-space framebuffer ─────────────────────────────────────────── */
namespace ufb {
    inline u32 width()          { return (u32)_svc0(SYS_FB_WIDTH); }
    inline u32 height()         { return (u32)_svc0(SYS_FB_HEIGHT); }
    inline bool is_available()  { return _svc0(SYS_FB_AVAILABLE) != 0; }
    inline bool set_resolution(u32 w, u32 h)
        { return _svc2(SYS_FB_SET_RES, w, h) != 0; }
}

/* ── User-space keyboard ────────────────────────────────────────────── */
namespace ukeyboard {
    inline const char* get_layout_name(i32 idx)
        { return (const char*)_svc1(SYS_KBD_LAYOUT_NAME, (u64)idx); }
    inline i32 layout_count()
        { return (i32)_svc0(SYS_KBD_LAYOUT_COUNT); }
    inline bool is_available()
        { return true; /* keyboard availability checked by kernel */ }
}

/* ── User-space mouse ───────────────────────────────────────────────── */
namespace umouse {
    inline bool is_available()
        { return _svc0(SYS_MOUSE_AVAILABLE) != 0; }
}

/* ── User-space netdev ──────────────────────────────────────────────── */
namespace unetdev {
    inline bool is_available()
        { return _svc0(SYS_NETDEV_AVAILABLE) != 0; }
    inline void get_mac(u8 *buf)
        { str::cpy((char*)buf, "52:54:00:12:34:56"); }
}

/* ── User-space C# interpreter ──────────────────────────────────────── */
namespace ucsharp {
    inline bool run(const char *src, char *out, i32 sz)
        { return _svc3(SYS_CS_RUN, (u64)src, (u64)out, (u64)sz) != 0; }
    inline bool init(const char *src)
        { return _svc1(SYS_CS_INIT, (u64)src) != 0; }
    inline void call_draw()         { _svc0(SYS_CS_CALL_DRAW); }
    inline void call_click(i32 x, i32 y) { _svc2(SYS_CS_CALL_CLICK, (u64)x, (u64)y); }
    inline bool call_key(char k)    { return _svc1(SYS_CS_CALL_KEY, (u64)k) != 0; }
    inline void call_arrow(char d)  { _svc1(SYS_CS_CALL_ARROW, (u64)d); }
    inline bool should_close()      { return _svc0(SYS_CS_SHOULD_CLOSE) != 0; }
    inline bool has_error()         { return _svc0(SYS_CS_HAS_ERROR) != 0; }
    inline const char* get_error()  { return (const char*)_svc0(SYS_CS_GET_ERROR); }
    inline void gui_cleanup()       { _svc0(SYS_CS_GUI_CLEANUP); }
    inline bool has_func(const char *n) { return _svc1(SYS_CS_HAS_FUNC, (u64)n) != 0; }
    inline void set_draw_ctx(i32 cx, i32 cy, i32 cw, i32 ch)
        { _svc4(SYS_CS_SET_DRAW_CTX, (u64)cx, (u64)cy, (u64)cw, (u64)ch); }
}

/* ── Shell command execution ────────────────────────────────────────── */
namespace ucmd {
    /* Execute a shell command, output written to buf. Returns bytes written. */
    inline usize exec(const char *line, char *buf, usize bufsz)
        { return (usize)_svc3(SYS_CMD_EXEC, (u64)line, (u64)buf, bufsz); }
}

/* ══════════════════════════════════════════════════════════════════════
 * Namespace aliases — apps keep using gfx::, fs::, etc.
 * ══════════════════════════════════════════════════════════════════════ */
namespace gfx      = ugfx;
namespace fs       = ufs;
namespace syslog   = usyslog;
namespace gui      = ugui;
namespace settings = usettings;
namespace assoc    = uassoc;
namespace env      = uenv;
namespace net      = unet;
namespace uart     = uuart;
namespace disk     = udisk;
namespace fb       = ufb;
namespace keyboard = ukeyboard;
namespace mouse    = umouse;
namespace netdev   = unetdev;
namespace csharp   = ucsharp;

/* apps:: is NOT aliased — registry.h is always included for the
   register_* functions. User code that queries apps should use
   uapps:: directly (e.g., taskman). */

/* menu:: needs entry type constants and Entry struct */
namespace menu {
    constexpr i32 MAX_ENTRIES = 24;
    constexpr i32 MAX_LABEL = 32;
    constexpr i32 MAX_ID = 32;

    enum EntryType : i32 {
        ENTRY_APP = 0, ENTRY_SHORTCUT = 1, ENTRY_SEP = 2,
        ENTRY_EXPLORER = 3, ENTRY_ABOUT = 4, ENTRY_SHUTDOWN = 5,
        ENTRY_COMMAND = 6, ENTRY_RESTART = 7,
    };

    struct Entry {
        EntryType type;
        char label[MAX_LABEL];
        char id[MAX_ID];
    };

    inline i32 count()       { return umenu::count(); }

    /* get() returns a pointer to a static user-accessible Entry */
    inline const Entry* get(i32 idx) {
        static Entry _user_entry;
        const char *lbl = umenu::get_label(idx);
        if (!lbl) return nullptr;
        _user_entry.type = (EntryType)umenu::get_type(idx);
        /* Copy label — it's in transfer buf, copy before it's overwritten */
        for (i32 j = 0; j < MAX_LABEL - 1 && lbl[j]; j++)
            _user_entry.label[j] = lbl[j];
        _user_entry.label[MAX_LABEL - 1] = '\0';
        /* id is not easily retrievable via current syscalls, leave empty */
        _user_entry.id[0] = '\0';
        return &_user_entry;
    }

    inline i32 add(EntryType t, const char *l, const char *id) { return umenu::add(t, l, id); }
    inline bool remove(i32 i)   { return umenu::remove(i); }
    inline bool move(i32 f, i32 t) { return umenu::move(f, t); }
    inline i32 find(const char *id) { return umenu::find(id); }
    inline bool has_app(const char *id) { return umenu::has_app(id); }
    inline void save()       { umenu::save(); }
    inline void load()       { /* no-op from user space */ }
    inline bool insert(i32 pos, EntryType t, const char *l, const char *id)
        { return umenu::insert(pos, t, l, id); }
}

/* Transfer buffer declared at file scope to avoid namespace mangling */
extern char gfx_transfer_buf[8192];

/* cmd:: namespace for terminal — wraps each command via SYS_CMD_EXEC */
namespace cmd {
    using OutFn = void (*)(void *ctx, const char *text);

    inline void _run(OutFn out, void *ctx, const char *cmdline) {
        ucmd::exec(cmdline, gfx_transfer_buf, 8192);
        out(ctx, gfx_transfer_buf);
    }
    inline void _run2(OutFn out, void *ctx, const char *c, const char *a) {
        char buf[256]; str::ncpy(buf, c, 60); str::cat(buf, " ");
        str::cat(buf, a); _run(out, ctx, buf);
    }
    inline void _run3(OutFn out, void *ctx, const char *c, const char *a, const char *b) {
        char buf[256]; str::ncpy(buf, c, 60); str::cat(buf, " ");
        str::cat(buf, a); str::cat(buf, " "); str::cat(buf, b);
        _run(out, ctx, buf);
    }

    inline void cp(OutFn o, void *c, const char *a, const char *b) { _run3(o,c,"cp",a,b); }
    inline void mv(OutFn o, void *c, const char *a, const char *b) { _run3(o,c,"mv",a,b); }
    inline void head(OutFn o, void *c, const char *f, i32) { _run2(o,c,"head",f); }
    inline void tail(OutFn o, void *c, const char *f, i32) { _run2(o,c,"tail",f); }
    inline void wc(OutFn o, void *c, const char *f)   { _run2(o,c,"wc",f); }
    inline void find(OutFn o, void *c, const char *n)  { _run2(o,c,"find",n); }
    inline void tree(OutFn o, void *c, const char *p, i32) { _run2(o,c,"tree",p); }
    inline void df(OutFn o, void *c)                   { _run(o,c,"df"); }
    inline void grep(OutFn o, void *c, const char *p, const char *f) { _run3(o,c,"grep",p,f); }
    inline void csrun(OutFn o, void *c, const char *f) { _run2(o,c,"csrun",f); }
    inline void csgui(OutFn o, void *c, const char *f) { _run2(o,c,"csgui",f); }
    inline void date(OutFn o, void *c)                 { _run(o,c,"date"); }
    inline void hostname(OutFn o, void *c)             { _run(o,c,"hostname"); }
    inline void whoami(OutFn o, void *c)               { _run(o,c,"whoami"); }
    inline void free_cmd(OutFn o, void *c)             { _run(o,c,"free"); }
    inline void dmesg(OutFn o, void *c)                { _run(o,c,"dmesg"); }
    inline void which(OutFn o, void *c, const char *n) { _run2(o,c,"which",n); }
    inline void xxd(OutFn o, void *c, const char *f)   { _run2(o,c,"xxd",f); }
}

#else
/* When not USERSPACE, apps include kernel headers directly (no change) */
#endif
