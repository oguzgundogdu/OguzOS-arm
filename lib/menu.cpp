#include "menu.h"
#include "fs.h"
#include "string.h"
#include "syslog.h"

namespace {

menu::Entry entries[menu::MAX_ENTRIES];
i32 entry_count = 0;

} // anonymous namespace

namespace menu {

void init() {
  str::memset(entries, 0, sizeof(entries));
  entry_count = 0;
}

i32 add(EntryType type, const char *label, const char *id) {
  if (entry_count >= MAX_ENTRIES)
    return -1;
  Entry &e = entries[entry_count];
  e.type = type;
  str::ncpy(e.label, label ? label : "", MAX_LABEL - 1);
  str::ncpy(e.id, id ? id : "", MAX_ID - 1);
  return entry_count++;
}

bool insert(i32 pos, EntryType type, const char *label, const char *id) {
  if (entry_count >= MAX_ENTRIES || pos < 0 || pos > entry_count)
    return false;
  // Shift down
  for (i32 i = entry_count; i > pos; i--)
    entries[i] = entries[i - 1];
  Entry &e = entries[pos];
  e.type = type;
  str::ncpy(e.label, label ? label : "", MAX_LABEL - 1);
  str::ncpy(e.id, id ? id : "", MAX_ID - 1);
  entry_count++;
  return true;
}

bool remove(i32 index) {
  if (index < 0 || index >= entry_count)
    return false;
  for (i32 i = index; i < entry_count - 1; i++)
    entries[i] = entries[i + 1];
  entry_count--;
  str::memset(&entries[entry_count], 0, sizeof(Entry));
  return true;
}

bool move(i32 from, i32 to) {
  if (from < 0 || from >= entry_count || to < 0 || to >= entry_count ||
      from == to)
    return false;
  Entry tmp = entries[from];
  if (from < to) {
    for (i32 i = from; i < to; i++)
      entries[i] = entries[i + 1];
  } else {
    for (i32 i = from; i > to; i--)
      entries[i] = entries[i - 1];
  }
  entries[to] = tmp;
  return true;
}

i32 count() { return entry_count; }

const Entry *get(i32 index) {
  if (index < 0 || index >= entry_count)
    return nullptr;
  return &entries[index];
}

i32 find(const char *id) {
  for (i32 i = 0; i < entry_count; i++) {
    if (str::cmp(entries[i].id, id) == 0)
      return i;
  }
  return -1;
}

bool has_app(const char *app_id) {
  for (i32 i = 0; i < entry_count; i++) {
    if (entries[i].type == ENTRY_APP && str::cmp(entries[i].id, app_id) == 0)
      return true;
  }
  return false;
}

void save() {
  char old_cwd[256];
  fs::get_cwd(old_cwd, sizeof(old_cwd));

  fs::cd("/");
  fs::mkdir("etc");
  fs::cd("/etc");
  fs::touch("menu");

  // Format: "type|label|id\n" per line
  // type: A=app, S=shortcut, -=sep, E=explorer, ?=about, X=shutdown, C=command
  char buf[2048];
  buf[0] = '\0';
  for (i32 i = 0; i < entry_count; i++) {
    const Entry &e = entries[i];
    char tc;
    switch (e.type) {
    case ENTRY_APP:
      tc = 'A';
      break;
    case ENTRY_SHORTCUT:
      tc = 'S';
      break;
    case ENTRY_SEP:
      tc = '-';
      break;
    case ENTRY_EXPLORER:
      tc = 'E';
      break;
    case ENTRY_ABOUT:
      tc = '?';
      break;
    case ENTRY_SHUTDOWN:
      tc = 'X';
      break;
    case ENTRY_COMMAND:
      tc = 'C';
      break;
    case ENTRY_RESTART:
      tc = 'R';
      break;
    default:
      tc = 'A';
      break;
    }
    char line[80];
    line[0] = tc;
    line[1] = '|';
    line[2] = '\0';
    str::cat(line, e.label);
    str::cat(line, "|");
    str::cat(line, e.id);
    str::cat(line, "\n");
    str::cat(buf, line);
  }

  fs::write("menu", buf);
  fs::sync_to_disk();
  syslog::info("menu", "saved %d entries to /etc/menu", entry_count);
  fs::cd(old_cwd);
}

void load() {
  char old_cwd[256];
  fs::get_cwd(old_cwd, sizeof(old_cwd));

  fs::cd("/etc");
  const char *data = fs::cat("menu");
  if (!data || data[0] == '\0') {
    fs::cd(old_cwd);
    return;
  }

  init(); // clear existing

  const char *p = data;
  while (*p && entry_count < MAX_ENTRIES) {
    // Parse type char
    char tc = *p++;
    if (*p != '|') {
      while (*p && *p != '\n')
        p++;
      if (*p == '\n')
        p++;
      continue;
    }
    p++; // skip '|'

    // Parse label
    char label[MAX_LABEL];
    i32 li = 0;
    while (*p && *p != '|' && *p != '\n' && li < MAX_LABEL - 1)
      label[li++] = *p++;
    label[li] = '\0';

    if (*p != '|') {
      while (*p && *p != '\n')
        p++;
      if (*p == '\n')
        p++;
      continue;
    }
    p++; // skip '|'

    // Parse id
    char id[MAX_ID];
    i32 ii = 0;
    while (*p && *p != '\n' && ii < MAX_ID - 1)
      id[ii++] = *p++;
    id[ii] = '\0';

    if (*p == '\n')
      p++;

    EntryType type;
    switch (tc) {
    case 'A':
      type = ENTRY_APP;
      break;
    case 'S':
      type = ENTRY_SHORTCUT;
      break;
    case '-':
      type = ENTRY_SEP;
      break;
    case 'E':
      type = ENTRY_EXPLORER;
      break;
    case '?':
      type = ENTRY_ABOUT;
      break;
    case 'X':
      type = ENTRY_SHUTDOWN;
      break;
    case 'C':
      type = ENTRY_COMMAND;
      break;
    case 'R':
      type = ENTRY_RESTART;
      break;
    default:
      continue;
    }
    add(type, label, id);
  }

  syslog::info("menu", "loaded %d entries from /etc/menu", entry_count);
  fs::cd(old_cwd);
}

} // namespace menu
