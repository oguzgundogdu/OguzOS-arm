#pragma once

#include "types.h"

/*
 * OguzOS In-Memory File System
 *
 * Simple hierarchical file system stored entirely in RAM.
 * Supports directories and files with content.
 */

namespace fs {

constexpr usize MAX_NAME = 64;
constexpr usize MAX_CONTENT = 4096;
constexpr usize MAX_CHILDREN = 32;
constexpr usize MAX_NODES = 128;

enum class NodeType : u8 {
  File,
  Directory,
};

struct Node {
  char name[MAX_NAME];
  NodeType type;
  bool used;

  // File content
  char content[MAX_CONTENT];
  usize content_len;

  // Directory children (indices into node pool)
  i32 children[MAX_CHILDREN];
  usize child_count;

  // Parent index (-1 for root)
  i32 parent;
};

// Initialize the file system with a root directory
void init();

// Get the current working directory path
void get_cwd(char *buf, usize buf_size);

// Change directory (supports "..", "/", and relative paths)
bool cd(const char *path);

// List contents of current directory
void ls();

// Create a directory in the current directory
bool mkdir(const char *name);

// Create an empty file in the current directory
bool touch(const char *name);

// Write content to a file in the current directory
bool write(const char *name, const char *content);

// Append content to a file in the current directory
bool append(const char *name, const char *content);

// Read file content (returns nullptr if not found)
const char *cat(const char *name);

// Remove a file or empty directory
bool rm(const char *name);

// Remove a file or directory recursively
bool rm_recursive(i32 node_idx);

// Get info about a node
bool stat(const char *name);

// Save entire filesystem to disk (persistent storage)
bool sync_to_disk();

// Load filesystem from disk (returns false if no valid FS found)
bool load_from_disk();

// GUI accessors: resolve a path to a node index (-1 if not found)
i32 resolve(const char *path);

// GUI accessors: get node by index (nullptr if invalid)
const Node *get_node(i32 idx);

} // namespace fs
