#include "app.h"
#include "registry.h"

#ifdef USERSPACE
#include "userapi.h"
#else
#include "graphics.h"
#include "net.h"
#include "string.h"
#include "uart.h"
#endif

/*
 * browser.ogz — Yamur Web Browser for OguzOS
 *
 * A proxy-based web browser. Connects to yamur-proxy (Node.js + Puppeteer)
 * running on the host, which renders real web pages and sends back raw pixels.
 *
 * The proxy runs at http://10.0.2.2:8088 (QEMU user-mode networking).
 * Start it with: node scripts/yamur-proxy.js
 */

namespace {

// ─── Constants ───

constexpr u32 PROXY_PORT = 8088;
constexpr char PROXY_HOST[] = "10.0.2.2";
constexpr i32 URL_BAR_H = 28;
constexpr i32 STATUS_BAR_H = 20;
constexpr i32 MAX_URL = 256;
constexpr i32 MAX_LINKS = 64;
constexpr i32 SCROLL_STEP = 100;

// Render viewport sent to proxy
constexpr i32 RENDER_W = 800;
constexpr i32 RENDER_H = 600;

// Pixel buffer: RENDER_W * RENDER_H * 4 bytes = 1,920,000 bytes
// Static since kernel is single-threaded and only one browser window at a time
constexpr u32 PIXEL_BUF_SIZE = RENDER_W * RENDER_H * 4;
static u8 pixel_buf[PIXEL_BUF_SIZE];

// Response buffer (header + links + pixels)
// Header: 16 bytes, links: MAX_LINKS * 280 = 17920, pixels: 1920000
// Total max: ~1.94 MB
constexpr u32 RESP_BUF_SIZE = 16 + (MAX_LINKS * 280) + PIXEL_BUF_SIZE;
static u8 resp_buf[RESP_BUF_SIZE];

// ─── Link map ───

struct Link {
  i32 x, y, w, h;
  char url[264];
};

static Link links[MAX_LINKS];
static i32 link_count = 0;

// ─── App state (fits in 4096 bytes) ───

struct BrowserState {
  char url[MAX_URL];
  char display_url[MAX_URL]; // URL shown in address bar (editable)
  i32 url_cursor;
  i32 scroll_y;
  i32 render_w;
  i32 render_h;
  bool loading;
  bool has_page;
  bool url_focused;
  // Navigation history
  char history[8][MAX_URL];
  i32 hist_count;
  i32 hist_pos;
  // Status message
  char status[128];
};

static_assert(sizeof(BrowserState) <= 4096, "BrowserState exceeds app_state");

// ─── Helpers ───

static u32 read_u32_le(const u8 *p) {
  return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) |
         (static_cast<u32>(p[2]) << 16) | (static_cast<u32>(p[3]) << 24);
}

static i32 read_i32_le(const u8 *p) {
  return static_cast<i32>(read_u32_le(p));
}

static void int_to_str(char *buf, i32 val) {
  if (val == 0) {
    buf[0] = '0';
    buf[1] = '\0';
    return;
  }
  char tmp[12];
  i32 i = 0;
  bool neg = val < 0;
  if (neg) val = -val;
  while (val > 0) {
    tmp[i++] = '0' + (val % 10);
    val /= 10;
  }
  i32 j = 0;
  if (neg) buf[j++] = '-';
  while (i > 0) buf[j++] = tmp[--i];
  buf[j] = '\0';
}

// ─── Page fetching ───

static void fetch_page(BrowserState *s) {
  s->loading = true;
  s->has_page = false;
  link_count = 0;
  str::cpy(s->status, "Loading...");

  if (!net::is_available()) {
    str::cpy(s->status, "No network connection");
    s->loading = false;
    return;
  }

  // Build proxy URL: http://10.0.2.2:8088/render?url=<url>&w=<w>&h=<h>&scroll=<scroll>
  char proxy_url[512];
  proxy_url[0] = '\0';
  str::cat(proxy_url, "http://");
  str::cat(proxy_url, PROXY_HOST);
  str::cat(proxy_url, ":");
  char port_str[8];
  int_to_str(port_str, PROXY_PORT);
  str::cat(proxy_url, port_str);
  str::cat(proxy_url, "/render?url=");

  // Simple URL-encode the target URL (just encode spaces and basic chars)
  char *dst = proxy_url + str::len(proxy_url);
  const char *src = s->url;
  while (*src && dst < proxy_url + sizeof(proxy_url) - 4) {
    if (*src == ' ') {
      *dst++ = '%';
      *dst++ = '2';
      *dst++ = '0';
    } else {
      *dst++ = *src;
    }
    src++;
  }
  *dst = '\0';

  str::cat(proxy_url, "&w=");
  char dim[12];
  int_to_str(dim, s->render_w > 0 ? s->render_w : RENDER_W);
  str::cat(proxy_url, dim);
  str::cat(proxy_url, "&h=");
  int_to_str(dim, s->render_h > 0 ? s->render_h : RENDER_H);
  str::cat(proxy_url, dim);
  str::cat(proxy_url, "&scroll=");
  int_to_str(dim, s->scroll_y);
  str::cat(proxy_url, dim);

  u32 received = net::http_get_bin(proxy_url, resp_buf, RESP_BUF_SIZE);

  if (received < 16) {
    str::cpy(s->status, "Failed to connect to proxy (is yamur-proxy running?)");
    s->loading = false;
    return;
  }

  // Parse header
  u32 w = read_u32_le(resp_buf + 0);
  u32 h = read_u32_le(resp_buf + 4);
  u32 lcount = read_u32_le(resp_buf + 8);

  if (w == 0 || h == 0 || w > 1920 || h > 1080) {
    str::cpy(s->status, "Invalid response from proxy");
    s->loading = false;
    return;
  }

  if (lcount > MAX_LINKS) lcount = MAX_LINKS;

  u32 expected = 16 + (lcount * 280) + (w * h * 4);
  if (received < expected) {
    str::cpy(s->status, "Incomplete response from proxy");
    s->loading = false;
    return;
  }

  // Parse link map
  link_count = static_cast<i32>(lcount);
  for (i32 i = 0; i < link_count; i++) {
    u32 off = 16 + static_cast<u32>(i) * 280;
    links[i].x = read_i32_le(resp_buf + off + 0);
    links[i].y = read_i32_le(resp_buf + off + 4);
    links[i].w = read_i32_le(resp_buf + off + 8);
    links[i].h = read_i32_le(resp_buf + off + 12);
    str::ncpy(links[i].url, reinterpret_cast<const char *>(resp_buf + off + 16), 263);
    links[i].url[263] = '\0';
  }

  // Copy pixel data
  u32 pixel_offset = 16 + lcount * 280;
  u32 pixel_bytes = w * h * 4;
  if (pixel_bytes > PIXEL_BUF_SIZE) pixel_bytes = PIXEL_BUF_SIZE;
  str::memcpy(pixel_buf, resp_buf + pixel_offset, pixel_bytes);

  s->render_w = static_cast<i32>(w);
  s->render_h = static_cast<i32>(h);
  s->has_page = true;
  s->loading = false;
  str::cpy(s->status, s->url);
}

static void navigate(BrowserState *s, const char *url) {
  str::ncpy(s->url, url, MAX_URL - 1);
  s->url[MAX_URL - 1] = '\0';
  str::ncpy(s->display_url, url, MAX_URL - 1);
  s->display_url[MAX_URL - 1] = '\0';
  s->url_cursor = static_cast<i32>(str::len(s->display_url));
  s->scroll_y = 0;

  // Push to history
  if (s->hist_count < 8) {
    str::ncpy(s->history[s->hist_count], url, MAX_URL - 1);
    s->hist_pos = s->hist_count;
    s->hist_count++;
  } else {
    // Shift history
    for (i32 i = 0; i < 7; i++)
      str::cpy(s->history[i], s->history[i + 1]);
    str::ncpy(s->history[7], url, MAX_URL - 1);
    s->hist_pos = 7;
  }

  fetch_page(s);
}

// ─── Callbacks ───

void browser_open(u8 *state) {
  auto *s = reinterpret_cast<BrowserState *>(state);
  str::memset(s, 0, sizeof(BrowserState));
  str::cpy(s->display_url, "https://example.com");
  s->url_cursor = static_cast<i32>(str::len(s->display_url));
  s->url_focused = true;
  s->render_w = RENDER_W;
  s->render_h = RENDER_H;
  str::cpy(s->status, "Enter a URL and press Enter. Start proxy: node scripts/yamur-proxy.js");
}

void browser_draw(u8 *state, i32 cx, i32 cy, i32 cw, i32 ch) {
  auto *s = reinterpret_cast<BrowserState *>(state);

  i32 fw = gfx::font_w();
  i32 fh = gfx::font_h();

  // ─── URL bar ───
  constexpr u32 COL_BAR_BG = 0x00F0F0F0;
  constexpr u32 COL_BAR_BORDER = 0x00CCCCCC;
  constexpr u32 COL_URL_BG = 0x00FFFFFF;
  constexpr u32 COL_URL_TEXT = 0x00333333;
  constexpr u32 COL_URL_CURSOR = 0x00000000;
  constexpr u32 COL_BTN_BG = 0x00E0E0E0;
  constexpr u32 COL_BTN_TEXT = 0x00333333;
  constexpr u32 COL_BTN_GO = 0x004CAF50;
  constexpr u32 COL_BTN_GO_TEXT = 0x00FFFFFF;

  // Bar background
  gfx::fill_rect(cx, cy, cw, URL_BAR_H, COL_BAR_BG);
  gfx::hline(cx, cy + URL_BAR_H - 1, cw, COL_BAR_BORDER);

  // Back button
  i32 btn_w = fw * 3 + 4;
  i32 btn_h = URL_BAR_H - 6;
  i32 btn_y = cy + 3;
  i32 bx = cx + 4;
  gfx::fill_rect(bx, btn_y, btn_w, btn_h, COL_BTN_BG);
  gfx::draw_text(bx + 2, btn_y + 2, " < ", COL_BTN_TEXT, COL_BTN_BG);
  bx += btn_w + 2;

  // Forward button
  gfx::fill_rect(bx, btn_y, btn_w, btn_h, COL_BTN_BG);
  gfx::draw_text(bx + 2, btn_y + 2, " > ", COL_BTN_TEXT, COL_BTN_BG);
  bx += btn_w + 4;

  // URL input field
  i32 go_w = fw * 4 + 4;
  i32 url_x = bx;
  i32 url_w = cw - url_x - go_w - 8;
  gfx::fill_rect(url_x, btn_y, url_w, btn_h, COL_URL_BG);
  gfx::rect(url_x, btn_y, url_w, btn_h, COL_BAR_BORDER);

  // Draw URL text (scrolled if needed)
  i32 max_chars = (url_w - 8) / fw;
  i32 url_len = static_cast<i32>(str::len(s->display_url));
  i32 text_start = 0;
  if (s->url_cursor > max_chars - 2) {
    text_start = s->url_cursor - max_chars + 2;
  }
  char visible[256];
  i32 vi = 0;
  for (i32 i = text_start; i < url_len && vi < max_chars && vi < 255; i++) {
    visible[vi++] = s->display_url[i];
  }
  visible[vi] = '\0';
  gfx::draw_text(url_x + 4, btn_y + 3, visible, COL_URL_TEXT, COL_URL_BG);

  // Draw cursor in URL bar
  if (s->url_focused) {
    i32 cursor_screen = s->url_cursor - text_start;
    if (cursor_screen >= 0 && cursor_screen <= max_chars) {
      gfx::fill_rect(url_x + 4 + cursor_screen * fw, btn_y + 2, 2, btn_h - 4, COL_URL_CURSOR);
    }
  }

  // Go button
  i32 go_x = url_x + url_w + 4;
  gfx::fill_rect(go_x, btn_y, go_w, btn_h, COL_BTN_GO);
  gfx::draw_text(go_x + 2, btn_y + 3, " Go ", COL_BTN_GO_TEXT, COL_BTN_GO);

  // ─── Page content area ───
  i32 content_y = cy + URL_BAR_H;
  i32 content_h = ch - URL_BAR_H - STATUS_BAR_H;

  if (s->has_page) {
    // Blit pixel buffer to screen, scaling if needed
    u32 *fb = reinterpret_cast<u32 *>(pixel_buf);
    i32 pw = s->render_w;
    i32 ph = s->render_h;

    for (i32 dy = 0; dy < content_h && dy < ph; dy++) {
      for (i32 dx = 0; dx < cw && dx < pw; dx++) {
        // Map display coords to render coords
        i32 sx = dx;
        i32 sy = dy;
        if (sx < pw && sy < ph) {
          u32 color = fb[sy * pw + sx];
          gfx::pixel(cx + dx, content_y + dy, color);
        }
      }
    }
    // Fill remaining area if window is larger than render
    if (cw > pw) {
      gfx::fill_rect(cx + pw, content_y, cw - pw, content_h, 0x00FFFFFF);
    }
    if (content_h > ph) {
      gfx::fill_rect(cx, content_y + ph, cw, content_h - ph, 0x00FFFFFF);
    }
  } else {
    // Empty page - draw placeholder
    gfx::fill_rect(cx, content_y, cw, content_h, 0x00FFFFFF);

    // Draw Yamur logo/title
    const char *title = "Yamur";
    i32 title_w = gfx::text_width(title);
    gfx::draw_text(cx + (cw - title_w) / 2, content_y + content_h / 3,
                   title, 0x00333333, 0x00FFFFFF);

    const char *subtitle = "OguzOS Web Browser";
    i32 sub_w = gfx::text_width(subtitle);
    gfx::draw_text(cx + (cw - sub_w) / 2, content_y + content_h / 3 + fh + 8,
                   subtitle, 0x00888888, 0x00FFFFFF);

    if (s->loading) {
      const char *msg = "Loading...";
      i32 msg_w = gfx::text_width(msg);
      gfx::draw_text(cx + (cw - msg_w) / 2, content_y + content_h / 2 + 20,
                     msg, 0x004CAF50, 0x00FFFFFF);
    }
  }

  // ─── Status bar ───
  i32 status_y = cy + ch - STATUS_BAR_H;
  gfx::fill_rect(cx, status_y, cw, STATUS_BAR_H, 0x00E8E8E8);
  gfx::hline(cx, status_y, cw, COL_BAR_BORDER);
  gfx::draw_text(cx + 4, status_y + 3, s->status, 0x00666666, 0x00E8E8E8);
}

bool browser_key(u8 *state, char key) {
  auto *s = reinterpret_cast<BrowserState *>(state);

  if (key == '\r' || key == '\n') {
    // Navigate to URL
    s->url_focused = false;
    navigate(s, s->display_url);
    return true;
  }

  if (key == 0x7F || key == 0x08) { // Backspace
    if (s->url_cursor > 0) {
      i32 len = static_cast<i32>(str::len(s->display_url));
      for (i32 i = s->url_cursor - 1; i < len; i++)
        s->display_url[i] = s->display_url[i + 1];
      s->url_cursor--;
      s->url_focused = true;
    }
    return true;
  }

  if (key == 0x09) { // Tab - toggle URL focus
    s->url_focused = !s->url_focused;
    return true;
  }

  // Printable characters → URL bar
  if (key >= 32 && key <= 126) {
    i32 len = static_cast<i32>(str::len(s->display_url));
    if (len < MAX_URL - 2) {
      for (i32 i = len + 1; i > s->url_cursor; i--)
        s->display_url[i] = s->display_url[i - 1];
      s->display_url[s->url_cursor] = key;
      s->url_cursor++;
      s->url_focused = true;
    }
    return true;
  }

  return false;
}

void browser_arrow(u8 *state, char dir) {
  auto *s = reinterpret_cast<BrowserState *>(state);

  if (s->url_focused) {
    if (dir == 'C' && s->url_cursor < static_cast<i32>(str::len(s->display_url)))
      s->url_cursor++;
    else if (dir == 'D' && s->url_cursor > 0)
      s->url_cursor--;
    return;
  }

  // Arrow keys scroll the page when not focused on URL
  if (dir == 'A') { // Up
    if (s->scroll_y >= SCROLL_STEP) {
      s->scroll_y -= SCROLL_STEP;
      fetch_page(s);
    } else if (s->scroll_y > 0) {
      s->scroll_y = 0;
      fetch_page(s);
    }
  } else if (dir == 'B') { // Down
    s->scroll_y += SCROLL_STEP;
    fetch_page(s);
  }
}

void browser_close(u8 *) {
  // Clear static buffers
  str::memset(pixel_buf, 0, sizeof(pixel_buf));
  link_count = 0;
}

void browser_click(u8 *state, i32 rx, i32 ry, i32 cw, i32 /*ch*/) {
  auto *s = reinterpret_cast<BrowserState *>(state);

  i32 fw = gfx::font_w();

  // Check if click is in URL bar area
  if (ry < URL_BAR_H) {
    i32 btn_w = fw * 3 + 4;
    i32 bx = 4;

    // Back button
    if (rx >= bx && rx < bx + btn_w) {
      if (s->hist_pos > 0) {
        s->hist_pos--;
        str::cpy(s->url, s->history[s->hist_pos]);
        str::cpy(s->display_url, s->url);
        s->url_cursor = static_cast<i32>(str::len(s->display_url));
        s->scroll_y = 0;
        fetch_page(s);
      }
      return;
    }
    bx += btn_w + 2;

    // Forward button
    if (rx >= bx && rx < bx + btn_w) {
      if (s->hist_pos < s->hist_count - 1) {
        s->hist_pos++;
        str::cpy(s->url, s->history[s->hist_pos]);
        str::cpy(s->display_url, s->url);
        s->url_cursor = static_cast<i32>(str::len(s->display_url));
        s->scroll_y = 0;
        fetch_page(s);
      }
      return;
    }
    bx += btn_w + 4;

    // Go button (at the right end)
    i32 go_w = fw * 4 + 4;
    i32 go_x = cw - go_w - 4;
    if (rx >= go_x) {
      navigate(s, s->display_url);
      return;
    }

    // Click in URL field - focus it
    s->url_focused = true;
    // Position cursor at click location
    i32 url_x = bx;
    i32 char_pos = (rx - url_x - 4) / fw;
    i32 len = static_cast<i32>(str::len(s->display_url));
    if (char_pos < 0) char_pos = 0;
    if (char_pos > len) char_pos = len;
    s->url_cursor = char_pos;
    return;
  }

  // Check if click is on a link in the rendered page
  i32 page_x = rx;
  i32 page_y = ry - URL_BAR_H;

  if (page_y >= 0 && s->has_page) {
    for (i32 i = 0; i < link_count; i++) {
      if (page_x >= links[i].x && page_x < links[i].x + links[i].w &&
          page_y >= links[i].y && page_y < links[i].y + links[i].h) {
        // Show link URL in status bar on hover/click
        str::ncpy(s->status, links[i].url, 127);
        navigate(s, links[i].url);
        return;
      }
    }
    // Clicked on page but not a link - unfocus URL bar
    s->url_focused = false;
  }
}

void browser_scroll(u8 *state, i32 delta) {
  auto *s = reinterpret_cast<BrowserState *>(state);

  if (!s->has_page) return;

  if (delta < 0) { // Scroll down
    s->scroll_y += SCROLL_STEP;
    fetch_page(s);
  } else if (delta > 0 && s->scroll_y > 0) { // Scroll up
    s->scroll_y -= SCROLL_STEP;
    if (s->scroll_y < 0) s->scroll_y = 0;
    fetch_page(s);
  }
}

void browser_mouse_move(u8 *state, i32 rx, i32 ry, i32 cw, i32 ch) {
  auto *s = reinterpret_cast<BrowserState *>(state);
  (void)cw;
  (void)ch;

  // Show link URL in status bar on hover
  i32 page_x = rx;
  i32 page_y = ry - URL_BAR_H;

  if (page_y >= 0 && s->has_page) {
    for (i32 i = 0; i < link_count; i++) {
      if (page_x >= links[i].x && page_x < links[i].x + links[i].w &&
          page_y >= links[i].y && page_y < links[i].y + links[i].h) {
        str::ncpy(s->status, links[i].url, 127);
        return;
      }
    }
    // Not hovering a link - show current URL
    str::ncpy(s->status, s->url, 127);
  }
}

const OgzApp browser_app = {
    "Yamur",           // name
    "browser.ogz",     // id
    820,               // default_w
    660,               // default_h
    browser_open,      // on_open
    browser_draw,      // on_draw
    browser_key,       // on_key
    browser_arrow,     // on_arrow
    browser_close,     // on_close
    browser_click,     // on_click
    browser_scroll,    // on_scroll
    nullptr,           // on_mouse_down
    browser_mouse_move, // on_mouse_move
    nullptr,           // on_open_file
};

} // anonymous namespace

namespace apps {
void register_browser() { register_app(&browser_app); }
} // namespace apps
