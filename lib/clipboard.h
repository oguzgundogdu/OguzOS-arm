#pragma once

#include "types.h"

namespace clipboard {

void copy(const char *text);
const char *paste();
bool has_data();
void clear();

} // namespace clipboard
