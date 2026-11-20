#pragma once

#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "api/client.h"
#include "config.h"
#include "forum/client.h"
#include "plugin/api.h"

namespace spark {

struct PluginInfo {
  std::string id;
  std::string name;
  std::string version;
  std::string path;
};

struct PluginField {
  std::string key;
  std::string label;
  std::string type = "text";  // text / multiline
  bool required = false;
  std::string initial;
};

struct PluginAction {
  std::string id;
  std::string title;
  std::string description;
  std::vector<std::string> scopes;
  std::vector<PluginField> fields;
  std::string pluginId;
};

// Loads optional plugin shared objects and proxies their requests. The host
// never gives a plugin access to Config, ApiClient or any token; plugins only
// see the narrow C ABI in plugin/api.h.
class PluginHost {
 public:
  PluginHost(ApiClient* api, ForumClient* forum, const Config* config);
  ~PluginHost();

  PluginHost(const PluginHost&) = delete;
  PluginHost& operator=(const PluginHost&) = delete;

  // Scans the standard directories and loads every valid plugin. Invalid or
  // incompatible modules are skipped; they never abort startup.
  void LoadAll();
  // Loads all *.so files from one directory. Returns the number loaded.
  int LoadDirectory(const std::string& directory);

  const std::vector<PluginInfo>& Plugins() const { return plugins_; }
  std::vector<PluginAction> Actions() const;

  // Runs one action. Must be called from a worker thread; requests are
  // proxied through the host, which owns the credentials.
  bool Run(const std::string& pluginId, const std::string& action,
           const nlohmann::json& input, nlohmann::json& result,
           std::string& error);

 private:
  struct Loaded;

  static char* ApiRequestThunk(void* userdata, const char* method,
                               const char* path, const char* body);
  static char* ForumRequestThunk(void* userdata, const char* method,
                                 const char* path, const char* body);
  static char* SettingsThunk(void* userdata);
  static void LogThunk(void* userdata, int level, const char* message);
  static void FreeThunk(char* value);

  int LoadFile(const std::string& path);
  std::string RequestJson(bool forum, const char* method, const char* path,
                          const char* body) const;

  ApiClient* api_ = nullptr;
  ForumClient* forum_ = nullptr;
  const Config* config_ = nullptr;
  std::string dataRoot_;
  spark_host_api hostApi_{};
  std::vector<std::unique_ptr<Loaded>> loaded_;
  std::vector<PluginInfo> plugins_;
};

// Directories scanned for plugins, in order: $SPARK_PLUGIN_PATH, the user
// data directory, the build-time install directory and the system paths.
std::vector<std::string> PluginSearchPaths();

// Non-secret subset of the configuration. Deliberately excludes every token,
// label and credential field.
nlohmann::json SanitizedSettings(const Config& config);

}  // namespace spark
