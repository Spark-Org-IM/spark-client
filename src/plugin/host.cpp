#include "plugin/host.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <dlfcn.h>
#include <glob.h>
#endif

#include "util.h"

namespace spark {
namespace {

constexpr const char* kPluginEntry = "spark_plugin_entry";

#ifdef _WIN32
constexpr const char* kPluginExtension = ".dll";
#else
constexpr const char* kPluginExtension = ".so";
#endif

std::string DataHome() {
  const char* xdg = std::getenv("XDG_DATA_HOME");
  if (xdg != nullptr && *xdg != '\0') return xdg;
#ifdef _WIN32
  const char* local = std::getenv("LOCALAPPDATA");
  if (local != nullptr && *local != '\0') return local;
#endif
  return HomeDir() + "/.local/share";
}

char* Duplicate(const std::string& value) {
  char* out = static_cast<char*>(std::malloc(value.size() + 1));
  if (out == nullptr) return nullptr;
  std::memcpy(out, value.c_str(), value.size() + 1);
  return out;
}

std::string ErrorPayload(const std::string& code, const std::string& message) {
  nlohmann::json error = {
      {"error", {{"status", 0}, {"code", code}, {"message", message}}}};
  return error.dump();
}

std::string TakePluginString(const spark_plugin_api* api, char* value) {
  if (value == nullptr) return {};
  std::string out(value);
  if (api->free_string != nullptr) api->free_string(value);
  return out;
}

std::string PluginDataRoot() {
  return DataHome() + "/spark-client/plugin-data";
}

bool HasPluginExtension(const std::string& name) {
  const std::string suffix = kPluginExtension;
  return name.size() > suffix.size() &&
         name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
}

void* OpenLibrary(const std::string& path) {
#ifdef _WIN32
  return reinterpret_cast<void*>(LoadLibraryA(path.c_str()));
#else
  return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

void CloseLibrary(void* handle) {
  if (handle == nullptr) return;
#ifdef _WIN32
  FreeLibrary(reinterpret_cast<HMODULE>(handle));
#else
  dlclose(handle);
#endif
}

spark_plugin_entry_fn FindEntry(void* handle) {
  if (handle == nullptr) return nullptr;
#ifdef _WIN32
  return reinterpret_cast<spark_plugin_entry_fn>(
      GetProcAddress(reinterpret_cast<HMODULE>(handle), kPluginEntry));
#else
  return reinterpret_cast<spark_plugin_entry_fn>(dlsym(handle, kPluginEntry));
#endif
}

std::string LastLibraryError() {
#ifdef _WIN32
  return "error " + std::to_string(static_cast<unsigned long>(GetLastError()));
#else
  const char* message = dlerror();
  return message == nullptr ? "unknown error" : message;
#endif
}

std::vector<std::string> SplitPathList(const char* value) {
  std::vector<std::string> items;
  if (value == nullptr) return items;
  const std::string list = value;
#ifdef _WIN32
  constexpr char kSeparator = ';';
#else
  constexpr char kSeparator = ':';
#endif
  std::size_t start = 0;
  while (start <= list.size()) {
    const std::size_t end = list.find(kSeparator, start);
    const std::string item = list.substr(
        start, end == std::string::npos ? std::string::npos : end - start);
    if (!item.empty()) items.push_back(item);
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return items;
}

}  // namespace

struct PluginHost::Loaded {
  void* handle = nullptr;
  const spark_plugin_api* api = nullptr;
  spark_plugin* instance = nullptr;
  PluginInfo info;
  std::vector<PluginAction> actions;
  std::mutex runMutex;
};

PluginHost::PluginHost(ApiClient* api, ForumClient* forum,
                       const Config* config)
    : api_(api), forum_(forum), config_(config), dataRoot_(PluginDataRoot()) {
  std::error_code ec;
  std::filesystem::create_directories(dataRoot_, ec);

  hostApi_.struct_size = sizeof(spark_host_api);
  hostApi_.abi_version = SPARK_PLUGIN_ABI_VERSION;
  hostApi_.userdata = this;
  hostApi_.api_request = &PluginHost::ApiRequestThunk;
  hostApi_.forum_request = &PluginHost::ForumRequestThunk;
  hostApi_.settings_json = &PluginHost::SettingsThunk;
  hostApi_.log = &PluginHost::LogThunk;
  hostApi_.free_string = &PluginHost::FreeThunk;
}

PluginHost::~PluginHost() {
  for (auto it = loaded_.rbegin(); it != loaded_.rend(); ++it) {
    Loaded& plugin = **it;
    if (plugin.api != nullptr && plugin.instance != nullptr) {
      plugin.api->destroy(plugin.instance);
      plugin.instance = nullptr;
    }
    CloseLibrary(plugin.handle);
    plugin.handle = nullptr;
  }
}

void PluginHost::LoadAll() {
  for (const std::string& directory : PluginSearchPaths()) {
    LoadDirectory(directory);
  }
}

int PluginHost::LoadDirectory(const std::string& directory) {
  if (directory.empty()) return 0;
  std::error_code ec;
  std::filesystem::directory_iterator it(directory, ec);
  if (ec) return 0;

  std::vector<std::string> files;
  for (const auto& entry : it) {
    const std::string name = entry.path().filename().string();
    if (!HasPluginExtension(name)) continue;
    if (!entry.is_regular_file(ec)) continue;
    files.push_back(entry.path().string());
  }
  std::sort(files.begin(), files.end());

  int loaded = 0;
  for (const std::string& file : files) loaded += LoadFile(file);
  return loaded;
}

int PluginHost::LoadFile(const std::string& path) {
  void* handle = OpenLibrary(path);
  if (handle == nullptr) {
    std::fprintf(stderr, "spark-plugin: cannot load %s: %s\n", path.c_str(),
                 LastLibraryError().c_str());
    return 0;
  }

  spark_plugin_entry_fn entry = FindEntry(handle);
  if (entry == nullptr) {
    std::fprintf(stderr, "spark-plugin: %s has no %s\n", path.c_str(),
                 kPluginEntry);
    CloseLibrary(handle);
    return 0;
  }

  const spark_plugin_api* api = entry(SPARK_PLUGIN_ABI_VERSION);
  if (api == nullptr || api->struct_size < sizeof(spark_plugin_api) ||
      api->abi_version != SPARK_PLUGIN_ABI_VERSION) {
    std::fprintf(stderr, "spark-plugin: %s uses an incompatible ABI\n",
                 path.c_str());
    CloseLibrary(handle);
    return 0;
  }

  spark_plugin* instance = api->create(&hostApi_);
  if (instance == nullptr) {
    std::fprintf(stderr, "spark-plugin: %s failed to initialise\n",
                 path.c_str());
    CloseLibrary(handle);
    return 0;
  }

  const nlohmann::json info = nlohmann::json::parse(
      TakePluginString(api, api->info_json(instance)), nullptr, false);
  if (info.is_discarded() || !info.is_object()) {
    std::fprintf(stderr, "spark-plugin: %s returned invalid info\n",
                 path.c_str());
    api->destroy(instance);
    CloseLibrary(handle);
    return 0;
  }

  PluginInfo plugin;
  plugin.id = info.value("id", std::string{});
  plugin.name = info.value("name", plugin.id);
  plugin.version = info.value("version", std::string{});
  plugin.path = path;
  if (plugin.id.empty()) {
    std::fprintf(stderr, "spark-plugin: %s has an empty id\n", path.c_str());
    api->destroy(instance);
    CloseLibrary(handle);
    return 0;
  }
  for (const PluginInfo& existing : plugins_) {
    if (existing.id == plugin.id) {
      std::fprintf(stderr, "spark-plugin: duplicate id %s in %s\n",
                   plugin.id.c_str(), path.c_str());
      api->destroy(instance);
      CloseLibrary(handle);
      return 0;
    }
  }

  auto loaded = std::make_unique<Loaded>();
  loaded->handle = handle;
  loaded->api = api;
  loaded->instance = instance;
  loaded->info = plugin;

  const nlohmann::json actions = nlohmann::json::parse(
      TakePluginString(api, api->actions_json(instance)), nullptr, false);
  if (actions.is_array()) {
    for (const auto& entry_json : actions) {
      if (!entry_json.is_object()) continue;
      PluginAction action;
      action.id = entry_json.value("id", std::string{});
      action.title = entry_json.value("title", action.id);
      action.description = entry_json.value("description", std::string{});
      action.pluginId = plugin.id;
      if (entry_json.contains("scopes") &&
          entry_json.at("scopes").is_array()) {
        for (const auto& scope : entry_json.at("scopes")) {
          if (scope.is_string()) {
            action.scopes.push_back(scope.get<std::string>());
          }
        }
      }
      if (entry_json.contains("fields") &&
          entry_json.at("fields").is_array()) {
        for (const auto& field_json : entry_json.at("fields")) {
          if (!field_json.is_object()) continue;
          PluginField field;
          field.key = field_json.value("key", std::string{});
          field.label = field_json.value("label", field.key);
          field.type = field_json.value("type", std::string("text"));
          field.required = field_json.value("required", false);
          field.initial = field_json.value("initial", std::string{});
          if (!field.key.empty()) action.fields.push_back(std::move(field));
        }
      }
      if (!action.id.empty()) loaded->actions.push_back(std::move(action));
    }
  }

  plugins_.push_back(plugin);
  loaded_.push_back(std::move(loaded));
  return 1;
}

std::vector<PluginAction> PluginHost::Actions() const {
  std::vector<PluginAction> actions;
  for (const auto& plugin : loaded_) {
    actions.insert(actions.end(), plugin->actions.begin(),
                   plugin->actions.end());
  }
  return actions;
}

bool PluginHost::Run(const std::string& pluginId, const std::string& action,
                     const nlohmann::json& input, nlohmann::json& result,
                     std::string& error) {
  Loaded* target = nullptr;
  for (const auto& plugin : loaded_) {
    if (plugin->info.id == pluginId) {
      target = plugin.get();
      break;
    }
  }
  if (target == nullptr) {
    error = "unknown plugin: " + pluginId;
    return false;
  }

  const std::string inputText = input.is_null() ? "{}" : input.dump();
  std::lock_guard<std::mutex> lock(target->runMutex);
  char* raw = target->api->run_json(target->instance, action.c_str(),
                                    inputText.c_str());
  if (raw == nullptr) {
    error = "plugin returned no result";
    return false;
  }
  const nlohmann::json parsed =
      nlohmann::json::parse(TakePluginString(target->api, raw), nullptr,
                            false);
  if (parsed.is_discarded() || !parsed.is_object()) {
    error = "plugin returned invalid JSON";
    return false;
  }
  result = parsed;
  if (!parsed.value("ok", false)) {
    error = parsed.value("message", std::string("plugin action failed"));
    return false;
  }
  return true;
}

std::string PluginHost::RequestJson(bool forum, const char* method,
                                    const char* path, const char* body) const {
  if (method == nullptr || path == nullptr) {
    return ErrorPayload("invalid_argument", "missing method or path");
  }
  nlohmann::json requestBody = nlohmann::json::object();
  if (body != nullptr && *body != '\0') {
    requestBody = nlohmann::json::parse(body, nullptr, false);
    if (requestBody.is_discarded()) {
      return ErrorPayload("invalid_json", "request body is not valid JSON");
    }
  }
  if (forum) {
    if (forum_ == nullptr) {
      return ErrorPayload("no_forum", "forum client is not available");
    }
    auto result = forum_->RawRequest(method, path, requestBody);
    return result.ok ? result.value.dump() : ErrorPayload(result.error.code,
                                                          result.error.message);
  }
  if (api_ == nullptr) {
    return ErrorPayload("no_api", "API client is not available");
  }
  auto result = api_->RawRequest(method, path, requestBody);
  return result.ok ? result.value.dump()
                   : ErrorPayload(result.error.code, result.error.message);
}

char* PluginHost::ApiRequestThunk(void* userdata, const char* method,
                                  const char* path, const char* body) {
  auto* self = static_cast<PluginHost*>(userdata);
  if (self == nullptr) return nullptr;
  return Duplicate(self->RequestJson(false, method, path, body));
}

char* PluginHost::ForumRequestThunk(void* userdata, const char* method,
                                    const char* path, const char* body) {
  auto* self = static_cast<PluginHost*>(userdata);
  if (self == nullptr) return nullptr;
  return Duplicate(self->RequestJson(true, method, path, body));
}

char* PluginHost::SettingsThunk(void* userdata) {
  auto* self = static_cast<PluginHost*>(userdata);
  if (self == nullptr || self->config_ == nullptr) return nullptr;
  nlohmann::json settings = SanitizedSettings(*self->config_);
  settings["data_root"] = self->dataRoot_;
  return Duplicate(settings.dump());
}

void PluginHost::LogThunk(void* userdata, int level, const char* message) {
  (void)userdata;
  if (message == nullptr) return;
  std::fprintf(stderr, "spark-plugin[%d]: %s\n", level, message);
}

void PluginHost::FreeThunk(char* value) { std::free(value); }

std::vector<std::string> PluginSearchPaths() {
  std::vector<std::string> paths =
      SplitPathList(std::getenv("SPARK_PLUGIN_PATH"));

  paths.push_back(DataHome() + "/spark-client/plugins");
#ifdef SPARK_PLUGIN_DIR
  paths.push_back(SPARK_PLUGIN_DIR);
#endif
#ifndef _WIN32
  paths.push_back("/usr/lib/spark-client/plugins");
  paths.push_back("/usr/local/lib/spark-client/plugins");
  for (const char* pattern : {"/usr/lib/*/spark-client/plugins",
                              "/usr/local/lib/*/spark-client/plugins"}) {
    glob_t matches{};
    if (glob(pattern, GLOB_ONLYDIR, nullptr, &matches) == 0) {
      for (std::size_t i = 0; i < matches.gl_pathc; ++i) {
        paths.emplace_back(matches.gl_pathv[i]);
      }
      globfree(&matches);
    }
  }
#endif

  std::vector<std::string> unique;
  for (const std::string& path : paths) {
    if (path.empty()) continue;
    if (std::find(unique.begin(), unique.end(), path) == unique.end()) {
      unique.push_back(path);
    }
  }
  return unique;
}

nlohmann::json SanitizedSettings(const Config& config) {
  nlohmann::json settings = nlohmann::json::object();
  settings["main"] = config.EffectiveMain();
  settings["status"] = config.EffectiveStatus();
  settings["forum"] = config.EffectiveForum();
  settings["language"] = config.language.empty() ? "system" : config.language;
  return settings;
}

}  // namespace spark
