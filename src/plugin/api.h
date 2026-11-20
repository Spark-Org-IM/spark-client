#pragma once

#include <stdint.h>

// Stable C ABI between the host and a plugin shared object.
//
// Plugins are loaded with dlopen() and expose exactly one symbol:
//
//   const spark_plugin_api* spark_plugin_entry(uint32_t host_abi_version);
//
// The returned table is owned by the plugin and must stay valid for the
// lifetime of the process. The host never calls new/delete or any C++ symbol
// across the boundary; all strings and JSON are plain UTF-8 char buffers.
//
// Security contract: the host API deliberately exposes no configuration or
// token access. Requests are proxied by the host, so a plugin never receives
// account credentials or encrypted data. Plugins are trusted native code and
// must be installed by the user.

#define SPARK_PLUGIN_ABI_VERSION 1u

#if defined(_WIN32)
#define SPARK_PLUGIN_ENTRY __declspec(dllexport)
#else
#define SPARK_PLUGIN_ENTRY __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct spark_plugin spark_plugin;

// Capabilities granted to a plugin. Every callback may be called from a
// worker thread, never from the UI thread.
typedef struct spark_host_api {
  uint32_t struct_size;
  uint32_t abi_version;
  void* userdata;

  // Authenticated Spark ORG API request. `path` is an absolute API path such
  // as "/api/v2/news/save"; `body_json` is NULL or a JSON object string.
  // Returns a newly allocated JSON string (host memory), or NULL. On failure
  // the payload is {"error":{"status":..,"code":"..","message":".."}}.
  char* (*api_request)(void* userdata, const char* method, const char* path,
                       const char* body_json);

  // Same for the Flarum API, e.g. "/api/discussions/12".
  char* (*forum_request)(void* userdata, const char* method, const char* path,
                         const char* body_json);

  // Non-secret settings: instance URLs, UI language and a private data root.
  // Never contains tokens, passwords or encrypted data.
  char* (*settings_json)(void* userdata);

  void (*log)(void* userdata, int level, const char* message);
  void (*free_string)(char* value);
} spark_host_api;

// Table exported by the plugin. Modules must keep this layout stable: fields
// may be appended in a later ABI revision, never reordered or removed.
typedef struct spark_plugin_api {
  uint32_t struct_size;
  uint32_t abi_version;

  spark_plugin* (*create)(const spark_host_api* host);
  void (*destroy)(spark_plugin* plugin);

  // {"id":"news-admin","name":"...","version":"0.1.0"}
  char* (*info_json)(spark_plugin* plugin);
  // JSON array of action descriptors:
  // [{"id":"save-news","title":"...","description":"...",
  //   "scopes":["news.edit"],
  //   "fields":[{"key":"slug","label":"Slug","type":"text",
  //              "required":true,"initial":"..."}]}]
  char* (*actions_json)(spark_plugin* plugin);
  // {"ok":true,"message":"...","refresh":"news"} on success,
  // {"ok":false,"message":"..."} on failure.
  char* (*run_json)(spark_plugin* plugin, const char* action,
                    const char* input_json);

  void (*free_string)(char* value);
} spark_plugin_api;

typedef const spark_plugin_api* (*spark_plugin_entry_fn)(uint32_t host_abi);

#ifdef __cplusplus
}
#endif
