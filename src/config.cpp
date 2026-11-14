#include "config.h"

#include <sys/stat.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>
#include <toml++/toml.hpp>

#include "util.h"

namespace spark {
namespace {

std::optional<std::string> ReadUrl(const toml::table& table,
                                   std::string_view key) {
  if (const auto value = table[key].value<std::string>()) {
    return NormalizeUrl(*value);
  }
  return std::nullopt;
}

std::optional<std::string> ReadText(const toml::table& table,
                                    std::string_view key) {
  if (const auto value = table[key].value<std::string>()) {
    std::string text = Trim(*value);
    if (!text.empty()) return text;
  }
  return std::nullopt;
}

bool ReadBool(const toml::table& table, std::string_view key, bool fallback) {
  if (const auto value = table[key].value<bool>()) return *value;
  return fallback;
}

std::string JsonString(const nlohmann::json& object, const char* key) {
  if (!object.contains(key) || !object.at(key).is_string()) return {};
  return object.at(key).get<std::string>();
}

std::string JsonId(const nlohmann::json& object, const char* key) {
  if (!object.contains(key)) return {};
  const auto& value = object.at(key);
  if (value.is_string()) return value.get<std::string>();
  if (value.is_number_integer()) return std::to_string(value.get<long long>());
  if (value.is_number_unsigned()) {
    return std::to_string(value.get<unsigned long long>());
  }
  return {};
}

// Old pre-TOML config: ~/.config/spark-tui/config.json.
Config LoadLegacyJson(const std::string& path) {
  Config cfg;

  std::ifstream in(path);
  if (!in) return cfg;

  nlohmann::json json;
  try {
    in >> json;
  } catch (const std::exception&) {
    return cfg;
  }
  if (!json.is_object()) return cfg;

  if (const auto base = NormalizeUrl(JsonString(json, "baseUrl"))) {
    cfg.instance.mainOverride = *base;
  }
  cfg.language = JsonString(json, "language");
  if (json.contains("insecure") && json.at("insecure").is_boolean()) {
    cfg.insecure = json.at("insecure").get<bool>();
  }
  if (json.contains("token") && json.at("token").is_object()) {
    const auto& token = json.at("token");
    cfg.token.plaintext = JsonString(token, "plaintext");
    cfg.token.id = JsonId(token, "id");
    cfg.token.label = JsonString(token, "label");
    cfg.token.username = JsonString(token, "username");
  }
  return cfg;
}

std::string LegacyJsonPath(const std::string& path) {
  constexpr std::string_view kToml = ".toml";
  if (path.size() <= kToml.size() ||
      path.compare(path.size() - kToml.size(), kToml.size(), kToml) != 0) {
    return {};
  }
  return path.substr(0, path.size() - kToml.size()) + ".json";
}

}  // namespace

std::string InstanceConfig::EffectiveMain() const {
  if (mainOverride) return *mainOverride;
  if (autoMain) return *autoMain;
  return kDefaultMainUrl;
}

std::string InstanceConfig::EffectiveStatus() const {
  if (statusOverride) return *statusOverride;
  if (autoStatus) return *autoStatus;
  return kDefaultStatusUrl;
}

std::string InstanceConfig::EffectiveForum() const {
  if (forumOverride) return *forumOverride;
  if (autoForum) return *autoForum;
  return kDefaultForumUrl;
}

Config Config::Load(const std::string& path) {
  Config cfg;
  cfg.configPath = path;

  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) {
    const std::string legacy = LegacyJsonPath(path);
    if (!legacy.empty() && std::filesystem::exists(legacy, ec)) {
      cfg = LoadLegacyJson(legacy);
      cfg.configPath = path;
    }
    return cfg;
  }

  toml::table root;
  try {
    root = toml::parse_file(path);
  } catch (const std::exception&) {
    return cfg;
  }

  if (const toml::table* instance = root["instance"].as_table()) {
    if (const auto sync = ReadUrl(*instance, "sync_url")) {
      cfg.instance.syncUrl = *sync;
    }
    cfg.instance.mainOverride = ReadUrl(*instance, "main");
    cfg.instance.statusOverride = ReadUrl(*instance, "status");
    cfg.instance.forumOverride = ReadUrl(*instance, "forum");
    cfg.instance.autoMain = ReadUrl(*instance, "auto_main");
    cfg.instance.autoStatus = ReadUrl(*instance, "auto_status");
    cfg.instance.autoForum = ReadUrl(*instance, "auto_forum");
    cfg.syncOnStart = ReadBool(*instance, "sync_on_start", cfg.syncOnStart);
  }

  if (const toml::table* ui = root["ui"].as_table()) {
    if (const auto language = ReadText(*ui, "language")) {
      cfg.language = *language;
    }
    cfg.insecure = ReadBool(*ui, "insecure", cfg.insecure);
    cfg.showRawDetails =
        ReadBool(*ui, "show_raw_details", cfg.showRawDetails);
  }

  if (const toml::table* auth = root["auth"].as_table()) {
    if (const auto token = ReadText(*auth, "token")) {
      cfg.token.plaintext = *token;
    }
    if (const auto id = ReadText(*auth, "token_id")) {
      cfg.token.id = *id;
    }
    if (const auto label = ReadText(*auth, "token_label")) {
      cfg.token.label = *label;
    }
    if (const auto username = ReadText(*auth, "username")) {
      cfg.token.username = *username;
    }
  }

  if (const toml::table* forum = root["forum"].as_table()) {
    if (const auto token = ReadText(*forum, "token")) {
      cfg.forum.token = *token;
    }
    if (const auto id = ReadText(*forum, "user_id")) {
      cfg.forum.userId = *id;
    }
    if (const auto username = ReadText(*forum, "username")) {
      cfg.forum.username = *username;
    }
    if (const auto display_name = ReadText(*forum, "display_name")) {
      cfg.forum.displayName = *display_name;
    }
  }

  return cfg;
}

bool Config::Save() const {
  toml::table instance;
  instance.insert_or_assign("sync_url", this->instance.syncUrl);
  instance.insert_or_assign("sync_on_start", syncOnStart);
  if (this->instance.mainOverride) {
    instance.insert_or_assign("main", *this->instance.mainOverride);
  }
  if (this->instance.statusOverride) {
    instance.insert_or_assign("status", *this->instance.statusOverride);
  }
  if (this->instance.forumOverride) {
    instance.insert_or_assign("forum", *this->instance.forumOverride);
  }
  if (this->instance.autoMain) {
    instance.insert_or_assign("auto_main", *this->instance.autoMain);
  }
  if (this->instance.autoStatus) {
    instance.insert_or_assign("auto_status", *this->instance.autoStatus);
  }
  if (this->instance.autoForum) {
    instance.insert_or_assign("auto_forum", *this->instance.autoForum);
  }

  toml::table ui;
  if (!language.empty()) ui.insert_or_assign("language", language);
  ui.insert_or_assign("insecure", insecure);
  ui.insert_or_assign("show_raw_details", showRawDetails);

  toml::table root;
  root.insert_or_assign("instance", std::move(instance));
  root.insert_or_assign("ui", std::move(ui));

  if (!token.Empty()) {
    toml::table auth;
    auth.insert_or_assign("token", token.plaintext);
    if (!token.id.empty()) auth.insert_or_assign("token_id", token.id);
    if (!token.label.empty()) auth.insert_or_assign("token_label", token.label);
    if (!token.username.empty()) auth.insert_or_assign("username", token.username);
    root.insert_or_assign("auth", std::move(auth));
  }

  if (!forum.Empty()) {
    toml::table forum_table;
    forum_table.insert_or_assign("token", forum.token);
    if (!forum.userId.empty()) {
      forum_table.insert_or_assign("user_id", forum.userId);
    }
    if (!forum.username.empty()) {
      forum_table.insert_or_assign("username", forum.username);
    }
    if (!forum.displayName.empty()) {
      forum_table.insert_or_assign("display_name", forum.displayName);
    }
    root.insert_or_assign("forum", std::move(forum_table));
  }

  std::ostringstream text;
  text << "# spark-tui configuration\n"
       << "#\n"
       << "# [instance] sync_url is the services.json source. `main` / `status`\n"
       << "# / `forum` are manual overrides; omit them to follow the sync source.\n"
       << "# auto_main / auto_status / auto_forum are maintained automatically.\n"
       << "\n"
       << root << "\n";

  const std::filesystem::path target(configPath);
  const std::filesystem::path dir = target.parent_path();
  if (!dir.empty() && !EnsureDirectory(dir.string())) return false;

  const std::string tmp = configPath + ".tmp";
  {
    std::ofstream out(tmp, std::ios::trunc);
    if (!out) return false;
    out << text.str();
    if (!out) return false;
  }
  chmod(tmp.c_str(), S_IRUSR | S_IWUSR);  // 0600: holds a bearer token
  std::error_code ec;
  std::filesystem::rename(tmp, target, ec);
  if (ec) {
    std::filesystem::remove(tmp, ec);
    return false;
  }
  return true;
}

}  // namespace spark
