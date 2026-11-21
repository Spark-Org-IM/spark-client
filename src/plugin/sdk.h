#pragma once

#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <utility>

#include "plugin/api.h"

namespace spark::plugin {

// Convenience wrapper around the host callbacks. Plugins only see this and
// never the host's Config / ApiClient objects.
class Host {
 public:
  explicit Host(const spark_host_api* api) : api_(api) {}

  bool valid() const { return api_ != nullptr; }

  std::string ApiRequest(const std::string& method, const std::string& path,
                         const std::string& body = {}) const {
    return Call(api_ == nullptr ? nullptr : api_->api_request, method, path,
                body);
  }

  std::string ForumRequest(const std::string& method, const std::string& path,
                           const std::string& body = {}) const {
    return Call(api_ == nullptr ? nullptr : api_->forum_request, method, path,
                body);
  }

  std::string Settings() const {
    if (api_ == nullptr || api_->settings_json == nullptr) return {};
    return Take(api_->settings_json(api_->userdata));
  }

  void Log(int level, const std::string& message) const {
    if (api_ == nullptr || api_->log == nullptr) return;
    api_->log(api_->userdata, level, message.c_str());
  }

 private:
  using RequestFn = char* (*)(void*, const char*, const char*, const char*);

  std::string Call(RequestFn fn, const std::string& method,
                   const std::string& path, const std::string& body) const {
    if (fn == nullptr) return {};
    return Take(fn(api_->userdata, method.c_str(), path.c_str(),
                   body.empty() ? nullptr : body.c_str()));
  }

  std::string Take(char* value) const {
    if (value == nullptr) return {};
    std::string out(value);
    if (api_->free_string != nullptr) api_->free_string(value);
    return out;
  }

  const spark_host_api* api_ = nullptr;
};

namespace detail {

inline std::string Escape(const std::string& text) {
  std::string out;
  out.reserve(text.size() + 8);
  for (char c : text) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out += c;
        break;
    }
  }
  return out;
}

}  // namespace detail

// Base class for plugin authors. The SDK adapter below turns it into the C
// ABI table, so implementations keep using idiomatic C++ inside the module.
class Plugin {
 public:
  explicit Plugin(const spark_host_api* host) : host_(host) {}
  virtual ~Plugin() = default;

  const Host& host() const { return host_; }

  virtual std::string Id() const = 0;
  virtual std::string Name() const { return Id(); }
  virtual std::string Version() const { return "0.0.0"; }
  virtual std::string ActionsJson() const = 0;
  virtual std::string RunJson(const std::string& action,
                              const std::string& input) = 0;

  // The host reads metadata through this instead of calling virtual methods
  // across the module boundary.
  std::string InfoJson() const {
    return "{\"id\":\"" + detail::Escape(Id()) + "\",\"name\":\"" +
           detail::Escape(Name()) + "\",\"version\":\"" +
           detail::Escape(Version()) + "\"}";
  }

 private:
  Host host_;
};

}  // namespace spark::plugin

// Place this once in a plugin translation unit after the class definition:
//
//   SPARK_PLUGIN_EXPORT(NewsAdminPlugin)
//
// The class must have an explicit constructor taking const spark_host_api*.
#define SPARK_PLUGIN_EXPORT(Class)                                           \
  namespace {                                                                \
  char* SparkPluginDup(const std::string& value) {                           \
    char* out = static_cast<char*>(std::malloc(value.size() + 1));           \
    if (out == nullptr) return nullptr;                                      \
    std::memcpy(out, value.c_str(), value.size() + 1);                       \
    return out;                                                              \
  }                                                                          \
  spark_plugin* SparkPluginCreate(const spark_host_api* host) {              \
    return reinterpret_cast<spark_plugin*>(new (std::nothrow) Class(host));  \
  }                                                                          \
  void SparkPluginDestroy(spark_plugin* plugin) {                            \
    delete reinterpret_cast<Class*>(plugin);                                 \
  }                                                                          \
  char* SparkPluginInfo(spark_plugin* plugin) {                              \
    return SparkPluginDup(reinterpret_cast<Class*>(plugin)->InfoJson());     \
  }                                                                          \
  char* SparkPluginActions(spark_plugin* plugin) {                           \
    return SparkPluginDup(reinterpret_cast<Class*>(plugin)->ActionsJson());  \
  }                                                                          \
  char* SparkPluginRun(spark_plugin* plugin, const char* action,             \
                       const char* input) {                                  \
    return SparkPluginDup(reinterpret_cast<Class*>(plugin)->RunJson(         \
        action == nullptr ? "" : action, input == nullptr ? "{}" : input));  \
  }                                                                          \
  void SparkPluginFree(char* value) { std::free(value); }                    \
  const spark_plugin_api SparkPluginApi = {                                  \
      sizeof(spark_plugin_api), SPARK_PLUGIN_ABI_VERSION,                    \
      &SparkPluginCreate, &SparkPluginDestroy, &SparkPluginInfo,             \
      &SparkPluginActions, &SparkPluginRun, &SparkPluginFree};               \
  }                                                                          \
  extern "C" {                                                               \
  SPARK_PLUGIN_ENTRY const spark_plugin_api* spark_plugin_entry(             \
      uint32_t host_abi) {                                                   \
    if (host_abi != SPARK_PLUGIN_ABI_VERSION) return nullptr;                \
    return &SparkPluginApi;                                                  \
  }                                                                          \
  }
