#include "assoc.h"
#include "fs.h"
#include "string.h"
#include "syslog.h"

namespace {

struct Entry {
  char ext[assoc::MAX_EXT];
  char app_id[assoc::MAX_APP_ID];
  bool used;
};

Entry entries[assoc::MAX_ASSOCS];
i32 entry_count = 0;

i32 find_entry(const char *ext) {
  for (i32 i = 0; i < entry_count; i++) {
    if (entries[i].used && str::cmp(entries[i].ext, ext) == 0)
      return i;
  }
  return -1;
}

} // anonymous namespace

namespace assoc {

void init() {
  str::memset(entries, 0, sizeof(entries));
  entry_count = 0;
}

void set(const char *ext, const char *app_id) {
  i32 idx = find_entry(ext);
  if (idx >= 0) {
    str::ncpy(entries[idx].app_id, app_id, MAX_APP_ID - 1);
    return;
  }
  for (i32 i = 0; i < MAX_ASSOCS; i++) {
    if (!entries[i].used) {
      entries[i].used = true;
      str::ncpy(entries[i].ext, ext, MAX_EXT - 1);
      str::ncpy(entries[i].app_id, app_id, MAX_APP_ID - 1);
      if (i >= entry_count)
        entry_count = i + 1;
      return;
    }
  }
}

const char *get(const char *ext) {
  i32 idx = find_entry(ext);
  if (idx < 0)
    return nullptr;
  return entries[idx].app_id;
}

const char *find_for_file(const char *filename) {
  usize len = str::len(filename);
  // Find last dot
  const char *dot = nullptr;
  for (usize i = 0; i < len; i++) {
    if (filename[i] == '.')
      dot = filename + i;
  }
  if (!dot)
    return nullptr;
  return get(dot);
}

bool unset(const char *ext) {
  i32 idx = find_entry(ext);
  if (idx < 0)
    return false;
  entries[idx].used = false;
  entries[idx].ext[0] = '\0';
  entries[idx].app_id[0] = '\0';
  return true;
}

i32 count() { return entry_count; }

const char *ext_at(i32 index) {
  if (index < 0 || index >= entry_count || !entries[index].used)
    return nullptr;
  return entries[index].ext;
}

const char *app_at(i32 index) {
  if (index < 0 || index >= entry_count || !entries[index].used)
    return nullptr;
  return entries[index].app_id;
}

void save() {
  char old_cwd[256];
  fs::get_cwd(old_cwd, sizeof(old_cwd));

  fs::cd("/");
  fs::mkdir("etc");
  fs::cd("/etc");
  fs::touch("filetypes");

  // Build "ext=app_id\n" lines
  char buf[1024];
  buf[0] = '\0';
  for (i32 i = 0; i < entry_count; i++) {
    if (!entries[i].used)
      continue;
    str::cat(buf, entries[i].ext);
    str::cat(buf, "=");
    str::cat(buf, entries[i].app_id);
    str::cat(buf, "\n");
  }

  fs::write("filetypes", buf);
  fs::sync_to_disk();
  syslog::info("assoc", "saved %d associations to /etc/filetypes", count());

  fs::cd(old_cwd);
}

void load() {
  char old_cwd[256];
  fs::get_cwd(old_cwd, sizeof(old_cwd));

  fs::cd("/etc");
  const char *data = fs::cat("filetypes");
  if (!data || data[0] == '\0') {
    fs::cd(old_cwd);
    return;
  }

  // Parse "ext=app_id\n" lines
  const char *p = data;
  while (*p) {
    // Extract ext
    char ext[MAX_EXT];
    i32 ei = 0;
    while (*p && *p != '=' && *p != '\n' && ei < MAX_EXT - 1)
      ext[ei++] = *p++;
    ext[ei] = '\0';

    if (*p != '=') {
      while (*p && *p != '\n')
        p++;
      if (*p == '\n')
        p++;
      continue;
    }
    p++; // skip '='

    // Extract app_id
    char app_id[MAX_APP_ID];
    i32 ai = 0;
    while (*p && *p != '\n' && ai < MAX_APP_ID - 1)
      app_id[ai++] = *p++;
    app_id[ai] = '\0';

    if (*p == '\n')
      p++;

    if (ext[0] && app_id[0])
      set(ext, app_id);
  }

  syslog::info("assoc", "loaded %d associations from /etc/filetypes", count());
  fs::cd(old_cwd);
}

} // namespace assoc
