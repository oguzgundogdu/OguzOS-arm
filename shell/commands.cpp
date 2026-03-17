#include "commands.h"
#include "csharp.h"
#include "disk.h"
#include "env.h"
#include "fs.h"
#include "net.h"
#include "settings.h"
#include "string.h"
#include "syslog.h"

namespace {

// Small int-to-string helper
void i2s(char *buf, i64 val) {
  if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
  char tmp[20]; i32 i = 0;
  bool neg = val < 0;
  if (neg) val = -val;
  while (val > 0) { tmp[i++] = '0' + static_cast<char>(val % 10); val /= 10; }
  i32 j = 0;
  if (neg) buf[j++] = '-';
  while (i > 0) buf[j++] = tmp[--i];
  buf[j] = '\0';
}

// Recursive tree helper
void tree_recurse(cmd::OutFn out, void *ctx, i32 dir_idx, i32 depth, char *prefix) {
  const fs::Node *dir = fs::get_node(dir_idx);
  if (!dir || dir->type != fs::NodeType::Directory) return;
  if (depth > 8) return; // safety limit

  for (usize i = 0; i < dir->child_count; i++) {
    i32 ci = dir->children[i];
    const fs::Node *child = fs::get_node(ci);
    if (!child || !child->used) continue;

    out(ctx, prefix);
    bool last = (i == dir->child_count - 1);
    out(ctx, last ? "`-- " : "|-- ");
    out(ctx, child->name);
    if (child->type == fs::NodeType::Directory) out(ctx, "/");
    out(ctx, "\n");

    if (child->type == fs::NodeType::Directory) {
      char new_prefix[128];
      str::ncpy(new_prefix, prefix, 120);
      str::cat(new_prefix, last ? "    " : "|   ");
      tree_recurse(out, ctx, ci, depth + 1, new_prefix);
    }
  }
}

// Recursive find helper
void find_recurse(cmd::OutFn out, void *ctx, i32 dir_idx,
                  const char *pattern, char *path_buf) {
  const fs::Node *dir = fs::get_node(dir_idx);
  if (!dir) return;

  usize plen = str::len(path_buf);

  for (usize i = 0; i < dir->child_count; i++) {
    i32 ci = dir->children[i];
    const fs::Node *child = fs::get_node(ci);
    if (!child || !child->used) continue;

    // Build child path
    char child_path[256];
    str::ncpy(child_path, path_buf, 255);
    if (plen > 1) str::cat(child_path, "/");
    str::cat(child_path, child->name);

    // Check if name contains pattern
    bool match = false;
    usize nlen = str::len(child->name);
    usize patlen = str::len(pattern);
    if (patlen == 0) {
      match = true;
    } else {
      for (usize j = 0; j + patlen <= nlen; j++) {
        if (str::ncmp(child->name + j, pattern, patlen) == 0) {
          match = true;
          break;
        }
      }
    }

    if (match) {
      out(ctx, child_path);
      out(ctx, "\n");
    }

    if (child->type == fs::NodeType::Directory)
      find_recurse(out, ctx, ci, pattern, child_path);
  }
}

// Simple substring search (case-sensitive)
bool contains(const char *haystack, const char *needle) {
  usize hlen = str::len(haystack);
  usize nlen = str::len(needle);
  if (nlen == 0) return true;
  if (nlen > hlen) return false;
  for (usize i = 0; i + nlen <= hlen; i++) {
    if (str::ncmp(haystack + i, needle, nlen) == 0)
      return true;
  }
  return false;
}

} // anonymous namespace

namespace cmd {

void cp(OutFn out, void *ctx, const char *src, const char *dst) {
  const char *content = fs::cat(src);
  if (!content) {
    out(ctx, "cp: cannot read: ");
    out(ctx, src);
    out(ctx, "\n");
    return;
  }
  fs::touch(dst);
  fs::write(dst, content);
}

void mv(OutFn out, void *ctx, const char *src, const char *dst) {
  const char *content = fs::cat(src);
  if (!content) {
    out(ctx, "mv: cannot read: ");
    out(ctx, src);
    out(ctx, "\n");
    return;
  }
  fs::touch(dst);
  fs::write(dst, content);
  fs::rm(src);
}

void head(OutFn out, void *ctx, const char *file, i32 lines) {
  const char *content = fs::cat(file);
  if (!content) {
    out(ctx, "head: file not found: ");
    out(ctx, file);
    out(ctx, "\n");
    return;
  }
  i32 count = 0;
  const char *p = content;
  while (*p && count < lines) {
    const char *eol = p;
    while (*eol && *eol != '\n') eol++;
    // Output this line
    char line[512];
    usize len = static_cast<usize>(eol - p);
    if (len > 511) len = 511;
    str::memcpy(line, p, len);
    line[len] = '\0';
    out(ctx, line);
    out(ctx, "\n");
    count++;
    p = *eol ? eol + 1 : eol;
  }
}

void tail(OutFn out, void *ctx, const char *file, i32 lines) {
  const char *content = fs::cat(file);
  if (!content) {
    out(ctx, "tail: file not found: ");
    out(ctx, file);
    out(ctx, "\n");
    return;
  }
  // Count total lines
  i32 total = 0;
  for (const char *p = content; *p; p++)
    if (*p == '\n') total++;
  if (content[0] && content[str::len(content) - 1] != '\n') total++;

  i32 skip = total - lines;
  if (skip < 0) skip = 0;

  i32 cur = 0;
  const char *p = content;
  while (*p) {
    const char *eol = p;
    while (*eol && *eol != '\n') eol++;
    if (cur >= skip) {
      char line[512];
      usize len = static_cast<usize>(eol - p);
      if (len > 511) len = 511;
      str::memcpy(line, p, len);
      line[len] = '\0';
      out(ctx, line);
      out(ctx, "\n");
    }
    cur++;
    p = *eol ? eol + 1 : eol;
  }
}

void wc(OutFn out, void *ctx, const char *file) {
  const char *content = fs::cat(file);
  if (!content) {
    out(ctx, "wc: file not found: ");
    out(ctx, file);
    out(ctx, "\n");
    return;
  }
  i32 lines = 0, words = 0, bytes = 0;
  bool in_word = false;
  for (const char *p = content; *p; p++) {
    bytes++;
    if (*p == '\n') lines++;
    if (*p == ' ' || *p == '\n' || *p == '\t') {
      in_word = false;
    } else if (!in_word) {
      in_word = true;
      words++;
    }
  }
  char buf[64];
  out(ctx, "  ");
  i2s(buf, lines); out(ctx, buf);
  out(ctx, "  ");
  i2s(buf, words); out(ctx, buf);
  out(ctx, "  ");
  i2s(buf, bytes); out(ctx, buf);
  out(ctx, " ");
  out(ctx, file);
  out(ctx, "\n");
}

void find(OutFn out, void *ctx, const char *name) {
  char root_path[256];
  str::ncpy(root_path, "/", 255);
  find_recurse(out, ctx, 0, name, root_path);
}

void tree(OutFn out, void *ctx, const char *path, i32 /*depth*/) {
  i32 dir_idx = fs::resolve(path);
  if (dir_idx < 0) {
    out(ctx, "tree: not found: ");
    out(ctx, path);
    out(ctx, "\n");
    return;
  }
  const fs::Node *dir = fs::get_node(dir_idx);
  if (!dir || dir->type != fs::NodeType::Directory) {
    out(ctx, "tree: not a directory: ");
    out(ctx, path);
    out(ctx, "\n");
    return;
  }
  out(ctx, path);
  out(ctx, "\n");
  char prefix[128];
  prefix[0] = '\0';
  tree_recurse(out, ctx, dir_idx, 0, prefix);
}

void df(OutFn out, void *ctx) {
  i32 used_nodes = 0;
  i32 used_files = 0;
  i64 total_bytes = 0;
  for (i32 i = 0; i < static_cast<i32>(fs::MAX_NODES); i++) {
    const fs::Node *n = fs::get_node(i);
    if (n) {
      used_nodes++;
      if (n->type == fs::NodeType::File) {
        used_files++;
        total_bytes += static_cast<i64>(n->content_len);
      }
    }
  }
  char buf[32];
  out(ctx, "Filesystem    Nodes   Files   Used\n");
  out(ctx, "oguzfs        ");
  i2s(buf, used_nodes); out(ctx, buf);
  out(ctx, "/");
  i2s(buf, static_cast<i64>(fs::MAX_NODES)); out(ctx, buf);
  out(ctx, "   ");
  i2s(buf, used_files); out(ctx, buf);
  out(ctx, "       ");
  i2s(buf, total_bytes); out(ctx, buf);
  out(ctx, "B\n");
  if (disk::is_available()) {
    out(ctx, "disk          ");
    i2s(buf, static_cast<i64>(disk::get_capacity() / 2));
    out(ctx, buf);
    out(ctx, " KB\n");
  }
}

void grep(OutFn out, void *ctx, const char *pattern, const char *file) {
  const char *content = fs::cat(file);
  if (!content) {
    out(ctx, "grep: file not found: ");
    out(ctx, file);
    out(ctx, "\n");
    return;
  }
  const char *p = content;
  while (*p) {
    const char *eol = p;
    while (*eol && *eol != '\n') eol++;
    char line[512];
    usize len = static_cast<usize>(eol - p);
    if (len > 511) len = 511;
    str::memcpy(line, p, len);
    line[len] = '\0';
    if (contains(line, pattern)) {
      out(ctx, line);
      out(ctx, "\n");
    }
    p = *eol ? eol + 1 : eol;
  }
}

void date(OutFn out, void *ctx) {
  u64 epoch = net::get_epoch();
  if (epoch == 0) {
    // Fallback to uptime
    u64 cnt, freq;
    asm volatile("mrs %0, cntpct_el0" : "=r"(cnt));
    asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    if (freq == 0) freq = 1;
    char buf[32];
    i2s(buf, static_cast<i64>(cnt / freq));
    out(ctx, "uptime: ");
    out(ctx, buf);
    out(ctx, "s (no NTP sync)\n");
    return;
  }
  // Add timezone offset
  i32 tz = settings::get_tz_offset();
  i64 local = static_cast<i64>(epoch) + tz * 60;
  if (local < 0) local = 0;
  u64 t = static_cast<u64>(local);

  // Break down epoch into date/time
  u64 days = t / 86400;
  u64 rem = t % 86400;
  u64 hh = rem / 3600;
  u64 mm = (rem % 3600) / 60;
  u64 ss = rem % 60;

  // Date from days since 1970-01-01
  i32 y = 1970;
  while (true) {
    u64 ydays = ((y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365);
    if (days < ydays) break;
    days -= ydays;
    y++;
  }
  constexpr i32 mdays[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  bool leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
  i32 m = 0;
  while (m < 12) {
    i32 md = mdays[m];
    if (m == 1 && leap) md++;
    if (days < static_cast<u64>(md)) break;
    days -= static_cast<u64>(md);
    m++;
  }

  char buf[48];
  buf[0] = '\0';
  char tmp[8];
  i2s(tmp, y); str::cat(buf, tmp); str::cat(buf, "-");
  i2s(tmp, m + 1);
  if (m + 1 < 10) str::cat(buf, "0");
  str::cat(buf, tmp); str::cat(buf, "-");
  i2s(tmp, static_cast<i64>(days) + 1);
  if (days + 1 < 10) str::cat(buf, "0");
  str::cat(buf, tmp); str::cat(buf, " ");
  i2s(tmp, static_cast<i64>(hh));
  if (hh < 10) str::cat(buf, "0");
  str::cat(buf, tmp); str::cat(buf, ":");
  i2s(tmp, static_cast<i64>(mm));
  if (mm < 10) str::cat(buf, "0");
  str::cat(buf, tmp); str::cat(buf, ":");
  i2s(tmp, static_cast<i64>(ss));
  if (ss < 10) str::cat(buf, "0");
  str::cat(buf, tmp);

  if (tz != 0) {
    str::cat(buf, " UTC");
    if (tz > 0) str::cat(buf, "+");
    i2s(tmp, tz / 60); str::cat(buf, tmp);
    if (tz % 60 != 0) {
      str::cat(buf, ":");
      i2s(tmp, (tz < 0 ? -tz : tz) % 60);
      if ((tz < 0 ? -tz : tz) % 60 < 10) str::cat(buf, "0");
      str::cat(buf, tmp);
    }
  } else {
    str::cat(buf, " UTC");
  }
  str::cat(buf, "\n");
  out(ctx, buf);
}

void hostname(OutFn out, void *ctx) {
  char saved[256];
  fs::get_cwd(saved, sizeof(saved));
  fs::cd("/etc");
  const char *h = fs::cat("hostname");
  fs::cd(saved);
  out(ctx, h ? h : "oguzos");
  if (!h || (h[str::len(h) - 1] != '\n'))
    out(ctx, "\n");
}

void whoami(OutFn out, void *ctx) {
  const char *u = env::get("USER");
  out(ctx, u ? u : "root");
  out(ctx, "\n");
}

void free_cmd(OutFn out, void *ctx) {
  // Report RAM (fixed 1GB for QEMU virt)
  out(ctx, "         total    used\n");
  out(ctx, "Mem:     1024 MB  (no MMU tracking)\n");
  // FS node usage
  i32 used = 0;
  for (i32 i = 0; i < static_cast<i32>(fs::MAX_NODES); i++) {
    if (fs::get_node(i)) used++;
  }
  char buf[32];
  out(ctx, "Nodes:   ");
  i2s(buf, used); out(ctx, buf);
  out(ctx, " / ");
  i2s(buf, static_cast<i64>(fs::MAX_NODES)); out(ctx, buf);
  out(ctx, "\n");
}

void dmesg(OutFn out, void *ctx) {
  char saved[256];
  fs::get_cwd(saved, sizeof(saved));
  fs::cd("/var/log");
  const char *log = fs::cat("syslog");
  fs::cd(saved);
  if (log && log[0]) {
    out(ctx, log);
    if (log[str::len(log) - 1] != '\n')
      out(ctx, "\n");
  } else {
    out(ctx, "(no log)\n");
  }
}

void which(OutFn out, void *ctx, const char *name) {
  const char *resolved = env::resolve_command(name);
  if (resolved) {
    out(ctx, "/bin/");
    out(ctx, resolved);
    out(ctx, "\n");
  } else {
    out(ctx, name);
    out(ctx, " not found\n");
  }
}

void xxd(OutFn out, void *ctx, const char *file) {
  const char *content = fs::cat(file);
  if (!content) {
    out(ctx, "xxd: file not found: ");
    out(ctx, file);
    out(ctx, "\n");
    return;
  }
  usize len = str::len(content);
  const char *hex = "0123456789abcdef";
  for (usize off = 0; off < len; off += 16) {
    // Offset
    char line[80];
    i32 li = 0;
    for (i32 shift = 28; shift >= 0; shift -= 4)
      line[li++] = hex[(off >> shift) & 0xF];
    line[li++] = ':'; line[li++] = ' ';
    // Hex bytes
    for (usize j = 0; j < 16; j++) {
      if (off + j < len) {
        u8 b = static_cast<u8>(content[off + j]);
        line[li++] = hex[b >> 4];
        line[li++] = hex[b & 0xF];
      } else {
        line[li++] = ' '; line[li++] = ' ';
      }
      line[li++] = ' ';
      if (j == 7) line[li++] = ' ';
    }
    line[li++] = ' ';
    // ASCII
    for (usize j = 0; j < 16 && off + j < len; j++) {
      char c = content[off + j];
      line[li++] = (c >= 32 && c <= 126) ? c : '.';
    }
    line[li++] = '\n';
    line[li] = '\0';
    out(ctx, line);
  }
}

void csrun(OutFn out, void *ctx, const char *filepath) {
  if (!filepath || filepath[0] == '\0') {
    out(ctx, "usage: csrun <file.cs>\n");
    return;
  }

  // Resolve the file
  i32 idx = fs::resolve(filepath);
  if (idx < 0) {
    out(ctx, "csrun: file not found: ");
    out(ctx, filepath);
    out(ctx, "\n");
    return;
  }

  const fs::Node *node = fs::get_node(idx);
  if (!node || node->type != fs::NodeType::File) {
    out(ctx, "csrun: not a file: ");
    out(ctx, filepath);
    out(ctx, "\n");
    return;
  }

  // Run the interpreter
  char output[1024];
  bool ok = csharp::run(node->content, output, 1024);

  // Print output
  if (output[0])
    out(ctx, output);

  if (!ok && output[0] == '\0')
    out(ctx, "csrun: execution failed\n");
}

} // namespace cmd
