#include "fs.h"
#include "disk.h"
#include "string.h"
#include "uart.h"

namespace {

fs::Node nodes[fs::MAX_NODES];
i32 cwd_index = 0; // Current working directory

i32 alloc_node() {
  for (usize i = 0; i < fs::MAX_NODES; i++) {
    if (!nodes[i].used) {
      str::memset(&nodes[i], 0, sizeof(fs::Node));
      nodes[i].used = true;
      for (usize j = 0; j < fs::MAX_CHILDREN; j++) {
        nodes[i].children[j] = -1;
      }
      nodes[i].parent = -1;
      return static_cast<i32>(i);
    }
  }
  return -1;
}

i32 find_child(i32 dir_idx, const char *name) {
  if (dir_idx < 0 || !nodes[dir_idx].used)
    return -1;
  auto &dir = nodes[dir_idx];
  for (usize i = 0; i < dir.child_count; i++) {
    i32 ci = dir.children[i];
    if (ci >= 0 && nodes[ci].used && str::cmp(nodes[ci].name, name) == 0) {
      return ci;
    }
  }
  return -1;
}

bool add_child(i32 dir_idx, i32 child_idx) {
  auto &dir = nodes[dir_idx];
  if (dir.child_count >= fs::MAX_CHILDREN)
    return false;
  dir.children[dir.child_count++] = child_idx;
  nodes[child_idx].parent = dir_idx;
  return true;
}

void build_path(i32 idx, char *buf, usize buf_size) {
  if (idx <= 0) {
    str::ncpy(buf, "/", buf_size);
    return;
  }

  // Build path by walking up to root
  char temp[512];
  char segment[512];
  temp[0] = '\0';

  i32 current = idx;
  while (current > 0) {
    str::cpy(segment, "/");
    str::cat(segment, nodes[current].name);
    str::cat(segment, temp);
    str::cpy(temp, segment);
    current = nodes[current].parent;
  }

  if (temp[0] == '\0') {
    str::ncpy(buf, "/", buf_size);
  } else {
    str::ncpy(buf, temp, buf_size);
  }
}

// Resolve a path (absolute or relative) to a node index
i32 resolve_path(const char *path) {
  if (!path || path[0] == '\0')
    return cwd_index;

  i32 current;
  const char *p = path;

  if (p[0] == '/') {
    current = 0; // Start from root
    p++;
    if (*p == '\0')
      return 0;
  } else {
    current = cwd_index;
  }

  char component[fs::MAX_NAME];
  while (*p) {
    // Skip slashes
    while (*p == '/')
      p++;
    if (*p == '\0')
      break;

    // Extract component
    usize i = 0;
    while (*p && *p != '/' && i < fs::MAX_NAME - 1) {
      component[i++] = *p++;
    }
    component[i] = '\0';

    if (str::cmp(component, ".") == 0) {
      continue;
    } else if (str::cmp(component, "..") == 0) {
      if (nodes[current].parent >= 0) {
        current = nodes[current].parent;
      }
    } else {
      i32 child = find_child(current, component);
      if (child < 0)
        return -1;
      current = child;
    }
  }
  return current;
}

} // anonymous namespace

namespace fs {

void init() {
  str::memset(nodes, 0, sizeof(nodes));
  // Create root directory
  nodes[0].used = true;
  str::cpy(nodes[0].name, "/");
  nodes[0].type = NodeType::Directory;
  nodes[0].parent = -1;
  for (usize i = 0; i < MAX_CHILDREN; i++) {
    nodes[0].children[i] = -1;
  }
  cwd_index = 0;

  // Create some default directories
  mkdir("bin");
  mkdir("home");
  mkdir("etc");
  mkdir("tmp");
  mkdir("var");

  // Create a welcome file
  cd("home");
  mkdir("Desktop");
  touch("welcome.txt");
  write("welcome.txt", "Welcome to OguzOS!\n"
                       "This is a minimal ARM64 operating system.\n"
                       "Type 'help' to see available commands.\n");

  touch("readme.txt");
  write("readme.txt", "OguzOS v1.0 - ARM64\n"
                      "Built for QEMU virt machine / UTM\n"
                      "Features: UART console, shell, in-memory filesystem\n");
  cd("/");

  // Create /etc/hostname
  cd("etc");
  touch("hostname");
  write("hostname", "oguzos");
  touch("version");
  write("version", "1.0.0-arm64");
  cd("/");
}

void get_cwd(char *buf, usize buf_size) {
  build_path(cwd_index, buf, buf_size);
}

bool cd(const char *path) {
  i32 target = resolve_path(path);
  if (target < 0 || nodes[target].type != NodeType::Directory) {
    return false;
  }
  cwd_index = target;
  return true;
}

void ls() {
  auto &dir = nodes[cwd_index];
  if (dir.child_count == 0) {
    uart::puts("  (empty)\n");
    return;
  }
  for (usize i = 0; i < dir.child_count; i++) {
    i32 ci = dir.children[i];
    if (ci >= 0 && nodes[ci].used) {
      if (nodes[ci].type == NodeType::Directory) {
        uart::puts("  \033[1;34m"); // Bold blue for dirs
        uart::puts(nodes[ci].name);
        uart::puts("/\033[0m\n");
      } else {
        uart::puts("  ");
        uart::puts(nodes[ci].name);
        uart::puts("  (");
        uart::put_int(static_cast<i64>(nodes[ci].content_len));
        uart::puts(" bytes)\n");
      }
    }
  }
}

bool mkdir(const char *name) {
  if (find_child(cwd_index, name) >= 0) {
    uart::puts("mkdir: already exists: ");
    uart::puts(name);
    uart::putc('\n');
    return false;
  }
  i32 idx = alloc_node();
  if (idx < 0) {
    uart::puts("mkdir: no space left\n");
    return false;
  }
  str::ncpy(nodes[idx].name, name, MAX_NAME);
  nodes[idx].type = NodeType::Directory;
  return add_child(cwd_index, idx);
}

bool touch(const char *name) {
  if (find_child(cwd_index, name) >= 0) {
    return true; // File already exists, no error
  }
  i32 idx = alloc_node();
  if (idx < 0) {
    uart::puts("touch: no space left\n");
    return false;
  }
  str::ncpy(nodes[idx].name, name, MAX_NAME);
  nodes[idx].type = NodeType::File;
  return add_child(cwd_index, idx);
}

bool write(const char *name, const char *content) {
  i32 idx = find_child(cwd_index, name);
  if (idx < 0) {
    uart::puts("write: file not found: ");
    uart::puts(name);
    uart::putc('\n');
    return false;
  }
  if (nodes[idx].type != NodeType::File) {
    uart::puts("write: not a file: ");
    uart::puts(name);
    uart::putc('\n');
    return false;
  }
  usize len = str::len(content);
  if (len >= MAX_CONTENT)
    len = MAX_CONTENT - 1;
  str::ncpy(nodes[idx].content, content, len + 1);
  nodes[idx].content_len = len;
  return true;
}

bool append(const char *name, const char *content) {
  i32 idx = find_child(cwd_index, name);
  if (idx < 0)
    return false;
  if (nodes[idx].type != NodeType::File)
    return false;

  usize existing = nodes[idx].content_len;
  usize add_len = str::len(content);
  if (existing + add_len >= MAX_CONTENT) {
    add_len = MAX_CONTENT - 1 - existing;
  }
  str::memcpy(nodes[idx].content + existing, content, add_len);
  nodes[idx].content[existing + add_len] = '\0';
  nodes[idx].content_len = existing + add_len;
  return true;
}

const char *cat(const char *name) {
  i32 idx = find_child(cwd_index, name);
  if (idx < 0)
    return nullptr;
  if (nodes[idx].type != NodeType::File) {
    uart::puts("cat: is a directory: ");
    uart::puts(name);
    uart::putc('\n');
    return nullptr;
  }
  return nodes[idx].content;
}

bool rm(const char *name) {
  i32 idx = find_child(cwd_index, name);
  if (idx < 0) {
    uart::puts("rm: not found: ");
    uart::puts(name);
    uart::putc('\n');
    return false;
  }
  if (nodes[idx].type == NodeType::Directory && nodes[idx].child_count > 0) {
    uart::puts("rm: directory not empty: ");
    uart::puts(name);
    uart::putc('\n');
    return false;
  }

  // Remove from parent's children
  auto &dir = nodes[cwd_index];
  for (usize i = 0; i < dir.child_count; i++) {
    if (dir.children[i] == idx) {
      // Shift remaining children
      for (usize j = i; j < dir.child_count - 1; j++) {
        dir.children[j] = dir.children[j + 1];
      }
      dir.child_count--;
      break;
    }
  }
  nodes[idx].used = false;
  return true;
}

bool rm_recursive(i32 node_idx) {
  if (node_idx < 0 || node_idx >= static_cast<i32>(MAX_NODES) ||
      !nodes[node_idx].used)
    return false;

  // Recursively delete children first
  if (nodes[node_idx].type == NodeType::Directory) {
    while (nodes[node_idx].child_count > 0) {
      i32 child = nodes[node_idx].children[0];
      rm_recursive(child);
      // After recursive delete, remove child from parent's list
      // (rm_recursive on a leaf marks it unused, but we need to update parent)
      // If child is still in list (it was freed but not unlinked from parent),
      // unlink it
      if (nodes[node_idx].child_count > 0 &&
          nodes[node_idx].children[0] == child) {
        for (usize j = 0; j < nodes[node_idx].child_count - 1; j++)
          nodes[node_idx].children[j] = nodes[node_idx].children[j + 1];
        nodes[node_idx].child_count--;
      }
    }
  }

  // Remove from parent's children list
  i32 par = nodes[node_idx].parent;
  if (par >= 0 && nodes[par].used) {
    auto &pdir = nodes[par];
    for (usize i = 0; i < pdir.child_count; i++) {
      if (pdir.children[i] == node_idx) {
        for (usize j = i; j < pdir.child_count - 1; j++)
          pdir.children[j] = pdir.children[j + 1];
        pdir.child_count--;
        break;
      }
    }
  }

  nodes[node_idx].used = false;
  return true;
}

bool stat(const char *name) {
  i32 idx = find_child(cwd_index, name);
  if (idx < 0) {
    uart::puts("stat: not found: ");
    uart::puts(name);
    uart::putc('\n');
    return false;
  }
  uart::puts("  Name: ");
  uart::puts(nodes[idx].name);
  uart::putc('\n');
  uart::puts("  Type: ");
  uart::puts(nodes[idx].type == NodeType::Directory ? "directory" : "file");
  uart::putc('\n');
  if (nodes[idx].type == NodeType::File) {
    uart::puts("  Size: ");
    uart::put_int(static_cast<i64>(nodes[idx].content_len));
    uart::puts(" bytes\n");
  } else {
    uart::puts("  Children: ");
    uart::put_int(static_cast<i64>(nodes[idx].child_count));
    uart::putc('\n');
  }
  return true;
}

bool sync_to_disk() {
  if (!disk::is_available())
    return false;

  // Sector 0: header
  alignas(512) u8 header[512];
  str::memset(header, 0, 512);

  // Magic "OGUZFS01"
  str::memcpy(header, "OGUZFS01", 8);
  u32 version = 1;
  str::memcpy(header + 8, &version, 4);
  u32 node_count = MAX_NODES;
  str::memcpy(header + 12, &node_count, 4);
  u32 node_size = sizeof(Node);
  str::memcpy(header + 16, &node_size, 4);
  str::memcpy(header + 20, &cwd_index, 4);

  if (!disk::write_sector(0, header))
    return false;

  // Write nodes array starting at sector 1
  u8 *data = reinterpret_cast<u8 *>(nodes);
  usize total = sizeof(nodes);
  usize sectors = (total + 511) / 512;

  alignas(512) u8 sec[512];
  for (usize i = 0; i < sectors; i++) {
    usize off = i * 512;
    usize chunk = (total - off > 512) ? 512 : (total - off);
    str::memset(sec, 0, 512);
    str::memcpy(sec, data + off, chunk);
    if (!disk::write_sector(1 + i, sec))
      return false;
  }
  return true;
}

i32 resolve(const char *path) { return resolve_path(path); }

const Node *get_node(i32 idx) {
  if (idx < 0 || idx >= static_cast<i32>(MAX_NODES))
    return nullptr;
  if (!nodes[idx].used)
    return nullptr;
  return &nodes[idx];
}

bool load_from_disk() {
  if (!disk::is_available())
    return false;

  // Read header
  alignas(512) u8 header[512];
  if (!disk::read_sector(0, header))
    return false;

  // Verify magic
  if (str::ncmp(reinterpret_cast<char *>(header), "OGUZFS01", 8) != 0)
    return false;

  // Verify version
  u32 version;
  str::memcpy(&version, header + 8, 4);
  if (version != 1)
    return false;

  // Verify node size matches this build
  u32 node_size;
  str::memcpy(&node_size, header + 16, 4);
  if (node_size != sizeof(Node))
    return false;

  // Restore cwd
  i32 saved_cwd;
  str::memcpy(&saved_cwd, header + 20, 4);

  // Read nodes from sector 1+
  u8 *data = reinterpret_cast<u8 *>(nodes);
  usize total = sizeof(nodes);
  usize sectors = (total + 511) / 512;

  alignas(512) u8 sec[512];
  for (usize i = 0; i < sectors; i++) {
    if (!disk::read_sector(1 + i, sec))
      return false;
    usize off = i * 512;
    usize chunk = (total - off > 512) ? 512 : (total - off);
    str::memcpy(data + off, sec, chunk);
  }

  cwd_index = saved_cwd;
  return true;
}

} // namespace fs
