#pragma once

#include <optional>
#include <string>
#include <vector>

namespace spark {

// Built-in defaults, mirroring the Android client.
inline constexpr const char kDefaultMainUrl[] = "https://sparkorg.dpdns.org";
inline constexpr const char kDefaultStatusUrl[] =
    "https://status.sparkorg.dpdns.org";
inline constexpr const char kDefaultForumUrl[] =
    "https://forum.sparkorg.dpdns.org";
inline constexpr const char kDefaultSyncUrl[] =
    "https://spark-org-im.github.io/api/services.json";

struct StoredToken {
  std::string plaintext;
  std::string id;
  std::string label;
  std::string username;
  // Scopes of the current token, as reported when it was issued. Empty means
  // unknown (for example a token saved before scopes were stored); frontends
  // then keep every action visible.
  std::vector<std::string> scopes;

  bool Empty() const { return plaintext.empty(); }
};

// Flarum credentials are independent from the Spark ORG account. Only the
// token is stored; the password is used once for POST /api/token and dropped.
struct ForumAccount {
  std::string token;
  std::string userId;
  std::string username;
  std::string displayName;

  bool Empty() const { return token.empty(); }
};

// The three configurable instance addresses plus their auto-sync fallbacks.
//
//  - syncUrl: static services.json source (required, never blank);
//  - mainOverride / statusOverride / forumOverride: manual values; empty =
//    automatic;
//  - autoMain / autoStatus / autoForum: last successfully synced values, kept
//    even while the corresponding override is set, so a broken sync source
//    never leaves the client without a usable address.
//
// Effective value precedence: manual override -> synced value -> built-in.
struct InstanceConfig {
  std::string syncUrl = kDefaultSyncUrl;
  std::optional<std::string> mainOverride;
  std::optional<std::string> statusOverride;
  std::optional<std::string> forumOverride;
  std::optional<std::string> autoMain;
  std::optional<std::string> autoStatus;
  std::optional<std::string> autoForum;

  std::string EffectiveMain() const;
  std::string EffectiveStatus() const;
  std::string EffectiveForum() const;

  bool MainIsManual() const { return mainOverride.has_value(); }
  bool StatusIsManual() const { return statusOverride.has_value(); }
  bool ForumIsManual() const { return forumOverride.has_value(); }
};

struct Config {
  InstanceConfig instance;
  std::string language;
  bool insecure = false;
  bool syncOnStart = true;
  bool showRawDetails = false;
  StoredToken token;
  ForumAccount forum;

  // Not serialised; where this config was loaded from / is saved to.
  std::string configPath;

  // Loads TOML. When `path` does not exist and ends in ".toml", a legacy
  // "config.json" next to it is migrated automatically.
  static Config Load(const std::string& path);
  bool Save() const;

  std::string EffectiveMain() const { return instance.EffectiveMain(); }
  std::string EffectiveStatus() const { return instance.EffectiveStatus(); }
  std::string EffectiveForum() const { return instance.EffectiveForum(); }
};

}  // namespace spark
