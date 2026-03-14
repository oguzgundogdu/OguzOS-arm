#include "env.h"
#include "fs.h"
#include "registry.h"
#include "string.h"

namespace {

struct EnvVar {
  char key[env::MAX_KEY];
  char value[env::MAX_VALUE];
  bool used;
};

EnvVar vars[env::MAX_VARS];
i32 var_count = 0;

i32 find_var(const char *key) {
  for (i32 i = 0; i < var_count; i++) {
    if (vars[i].used && str::cmp(vars[i].key, key) == 0)
      return i;
  }
  return -1;
}

} // anonymous namespace

namespace env {

void init() {
  str::memset(vars, 0, sizeof(vars));
  var_count = 0;

  set("PATH", "/bin");
  set("HOME", "/home");
  set("USER", "oguz");
  set("HOSTNAME", "oguzos");
  set("SHELL", "ogzsh");
}

void set(const char *key, const char *value) {
  i32 idx = find_var(key);
  if (idx >= 0) {
    str::ncpy(vars[idx].value, value, MAX_VALUE - 1);
    return;
  }
  // Find empty slot
  for (i32 i = 0; i < MAX_VARS; i++) {
    if (!vars[i].used) {
      vars[i].used = true;
      str::ncpy(vars[i].key, key, MAX_KEY - 1);
      str::ncpy(vars[i].value, value, MAX_VALUE - 1);
      if (i >= var_count)
        var_count = i + 1;
      return;
    }
  }
}

const char *get(const char *key) {
  i32 idx = find_var(key);
  if (idx < 0)
    return nullptr;
  return vars[idx].value;
}

bool unset(const char *key) {
  i32 idx = find_var(key);
  if (idx < 0)
    return false;
  vars[idx].used = false;
  vars[idx].key[0] = '\0';
  vars[idx].value[0] = '\0';
  return true;
}

i32 count() { return var_count; }

const char *key_at(i32 index) {
  if (index < 0 || index >= var_count || !vars[index].used)
    return nullptr;
  return vars[index].key;
}

const char *value_at(i32 index) {
  if (index < 0 || index >= var_count || !vars[index].used)
    return nullptr;
  return vars[index].value;
}

const char *resolve_command(const char *name) {
  const char *path = get("PATH");
  if (!path)
    return nullptr;

  // Walk PATH directories (colon-separated, like /bin:/usr/bin)
  const char *p = path;
  while (*p) {
    // Extract one directory from PATH
    char dir[128];
    i32 dlen = 0;
    while (*p && *p != ':' && dlen < 126) {
      dir[dlen++] = *p++;
    }
    dir[dlen] = '\0';
    if (*p == ':')
      p++;

    if (dlen == 0)
      continue;

    // Resolve the directory in the filesystem
    i32 dir_idx = fs::resolve(dir);
    if (dir_idx < 0)
      continue;
    const fs::Node *dir_node = fs::get_node(dir_idx);
    if (!dir_node || dir_node->type != fs::NodeType::Directory)
      continue;

    // Search children for exact match (e.g. "notepad.ogz")
    for (usize i = 0; i < dir_node->child_count; i++) {
      const fs::Node *child = fs::get_node(dir_node->children[i]);
      if (!child || !child->used)
        continue;
      if (str::cmp(child->name, name) == 0)
        return child->name;
    }

    // Search children for match without .ogz extension (e.g. "notepad")
    usize nlen = str::len(name);
    for (usize i = 0; i < dir_node->child_count; i++) {
      const fs::Node *child = fs::get_node(dir_node->children[i]);
      if (!child || !child->used)
        continue;
      usize clen = str::len(child->name);
      // Check if child->name == name + ".ogz"
      if (clen == nlen + 4 &&
          str::ncmp(child->name, name, nlen) == 0 &&
          str::cmp(child->name + nlen, ".ogz") == 0) {
        return child->name;
      }
    }
  }

  return nullptr;
}

} // namespace env
