#include "service_source.h"

#include <regex>
#include <string>
#include <utility>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "api/client.h"
#include "forum/client.h"
#include "util.h"

namespace spark {
namespace {

bool StartsWithBrace(const std::string& body) {
  const std::string trimmed = Trim(body);
  return !trimmed.empty() && trimmed.front() == '{';
}

std::string FirstNonBlankLine(const std::string& body) {
  std::size_t begin = 0;
  while (begin <= body.size()) {
    const std::size_t end = body.find('\n', begin);
    const std::string line =
        Trim(body.substr(begin, end == std::string::npos ? std::string::npos
                                                         : end - begin));
    if (!line.empty()) return line;
    if (end == std::string::npos) break;
    begin = end + 1;
  }
  return {};
}

bool SplitUrl(const std::string& url, std::string& origin, std::string& path) {
  static const std::regex kUrl(
      R"(^(https?)://(\[[0-9A-Fa-f:.]+\]|[^/?#:]+)(:[0-9]+)?([^#]*)?$)",
      std::regex::icase);
  std::smatch match;
  if (!std::regex_match(url, match, kUrl)) return false;

  origin = match[1].str() + "://" + match[2].str();
  if (match[3].matched) origin += match[3].str();

  path = match[4].matched ? match[4].str() : "";
  if (path.empty()) path = "/";
  const std::size_t fragment = path.find('#');
  if (fragment != std::string::npos) path.erase(fragment);
  if (path.empty()) path = "/";
  return true;
}

ServiceSourceResult Failure(std::string error) {
  ServiceSourceResult result;
  result.ok = false;
  result.error = std::move(error);
  return result;
}

}  // namespace

ServiceSourceResult ParseServiceSource(const std::string& body) {
  const std::string trimmed = Trim(body);
  if (trimmed.empty()) return Failure("同步源内容为空");

  if (!StartsWithBrace(trimmed)) {
    const auto home = NormalizeUrl(FirstNonBlankLine(trimmed));
    if (!home) return Failure("同步源不是有效的域名");
    ServiceSourceResult result;
    result.ok = true;
    result.value.home = *home;
    return result;
  }

  nlohmann::json json = nlohmann::json::parse(trimmed, nullptr, false);
  if (json.is_discarded() || !json.is_object()) {
    return Failure("同步源 JSON 格式错误");
  }

  std::string homeText;
  if (json.contains("home_domain") && json.at("home_domain").is_string()) {
    homeText = json.at("home_domain").get<std::string>();
  }

  const nlohmann::json* services = nullptr;
  if (json.contains("services") && json.at("services").is_object()) {
    services = &json.at("services");
  }

  if (Trim(homeText).empty() && services != nullptr &&
      services->contains("HOME") && services->at("HOME").is_string()) {
    homeText = services->at("HOME").get<std::string>();
  }

  std::string statusText;
  if (services != nullptr && services->contains("STATUS") &&
      services->at("STATUS").is_string()) {
    statusText = services->at("STATUS").get<std::string>();
  }

  std::string forumText;
  if (services != nullptr && services->contains("FORUM") &&
      services->at("FORUM").is_string()) {
    forumText = services->at("FORUM").get<std::string>();
  }

  ServiceSourceResult result;
  result.value.home = NormalizeUrl(homeText);
  result.value.status = NormalizeUrl(statusText);
  result.value.forum = NormalizeUrl(forumText);
  if (!result.value.home && !result.value.status && !result.value.forum) {
    return Failure("同步源没有 home_domain、services.STATUS 或 services.FORUM");
  }
  result.ok = true;
  return result;
}

ServiceSourceResult FetchServiceSource(const std::string& url, bool insecure) {
  const auto normalized = NormalizeUrl(url);
  if (!normalized) return Failure("同步源地址无效");

  std::string origin;
  std::string path;
  if (!SplitUrl(*normalized, origin, path)) {
    return Failure("同步源地址无效");
  }

  httplib::Client client(origin);
  client.set_connection_timeout(5, 0);
  client.set_read_timeout(10, 0);
  client.set_write_timeout(10, 0);
  client.set_follow_location(true);
  client.enable_server_certificate_verification(!insecure);

  const httplib::Headers headers = {
      {"Accept", "application/json, text/plain"},
      {"User-Agent", "spark-tui/" SPARK_TUI_VERSION},
  };

  auto response = client.Get(path.c_str(), headers);
  if (!response) {
    return Failure("无法连接同步源（" +
                   std::to_string(static_cast<int>(response.error())) + "）");
  }
  if (response->status < 200 || response->status >= 300) {
    return Failure("同步源返回 HTTP " + std::to_string(response->status));
  }
  return ParseServiceSource(response->body);
}

int ProbeToken(const std::string& baseUrl, bool insecure,
               const std::string& token) {
  if (token.empty()) return 0;

  ApiClient client(baseUrl, insecure, token);
  const auto result = client.Me();
  if (result.ok) return 1;
  if (result.error.status == 401 || result.error.status == 403) return 0;
  if (result.error.status == 0 || result.error.status >= 500) return -1;
  return 0;
}

SyncOutcome SyncInstanceConfig(Config& cfg) {
  SyncOutcome outcome;
  const std::string oldMain = cfg.instance.EffectiveMain();
  const std::string oldStatus = cfg.instance.EffectiveStatus();
  const std::string oldForum = cfg.instance.EffectiveForum();

  const ServiceSourceResult fetched =
      FetchServiceSource(cfg.instance.syncUrl, cfg.insecure);
  if (!fetched.ok) {
    outcome.error = fetched.error;
    outcome.mainUrl = oldMain;
    outcome.statusUrl = oldStatus;
    outcome.forumUrl = oldForum;
    return outcome;
  }

  if (fetched.value.home) {
    const std::string home = *fetched.value.home;
    if (cfg.instance.MainIsManual()) {
      // Manual value wins; refresh the automatic fallback only.
      cfg.instance.autoMain = home;
    } else if (home == oldMain || cfg.token.Empty()) {
      cfg.instance.autoMain = home;
    } else {
      switch (ProbeToken(home, cfg.insecure, cfg.token.plaintext)) {
        case 1:
          cfg.instance.autoMain = home;
          break;
        case 0:
          // Never drop the session silently: keep the old address until the
          // user signs out through the source-review flow.
          outcome.mainHeld = true;
          outcome.mainRejected = true;
          break;
        default:
          outcome.mainHeld = true;
          break;
      }
    }
  }

  if (fetched.value.status) {
    // There is no status API in the TUI yet, so no credential to probe; the
    // value is only cached for display and future use.
    cfg.instance.autoStatus = *fetched.value.status;
  }

  if (fetched.value.forum) {
    // The forum has its own Flarum session; never reuse the Spark token.
    const std::string forum = *fetched.value.forum;
    if (cfg.instance.ForumIsManual() || forum == oldForum ||
        cfg.forum.Empty()) {
      // A manual forum override wins; refresh the fallback without probing.
      cfg.instance.autoForum = forum;
    } else {
      ForumClient probe(forum, cfg.insecure, cfg.forum.token);
      const auto result =
          probe.RawRequest("GET", "/api", nlohmann::json::object());
      if (result.ok) {
        cfg.instance.autoForum = forum;
      } else {
        outcome.forumHeld = true;
        outcome.forumRejected =
            result.error.status == 401 || result.error.status == 403;
      }
    }
  }

  outcome.mainUrl = cfg.instance.EffectiveMain();
  outcome.statusUrl = cfg.instance.EffectiveStatus();
  outcome.forumUrl = cfg.instance.EffectiveForum();
  outcome.mainChanged = outcome.mainUrl != oldMain;
  outcome.statusChanged = outcome.statusUrl != oldStatus;
  outcome.forumChanged = outcome.forumUrl != oldForum;
  outcome.success = true;
  return outcome;
}

}  // namespace spark
