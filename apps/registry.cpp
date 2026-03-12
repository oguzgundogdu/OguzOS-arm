#include "registry.h"
#include "string.h"

namespace {

const OgzApp *app_list[apps::MAX_APPS];
i32 app_count = 0;

} // anonymous namespace

namespace apps {

void register_app(const OgzApp *app) {
  if (app_count < MAX_APPS)
    app_list[app_count++] = app;
}

i32 count() { return app_count; }

const OgzApp *get(i32 index) {
  if (index < 0 || index >= app_count)
    return nullptr;
  return app_list[index];
}

const OgzApp *find(const char *id) {
  for (i32 i = 0; i < app_count; i++) {
    if (str::cmp(app_list[i]->id, id) == 0)
      return app_list[i];
  }
  return nullptr;
}

} // namespace apps
