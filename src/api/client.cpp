#include "api/client.h"

#include <regex>

#include <httplib.h>

#include "config.h"
#include "util.h"

namespace spark {
namespace {

ApiError NetworkError(httplib::Error error) {
  ApiError e;
  e.status = 0;
  e.code = "network_error";
  e.message = "无法连接服务器（" +
              std::to_string(static_cast<int>(error)) + "）";
  return e;
}

}  // namespace

ApiClient::ApiClient(std::string baseUrl, bool insecure, std::string token)
    : baseUrl_(std::move(baseUrl)),
      insecure_(insecure),
      token_(std::move(token)) {
  ApplyBaseUrlLocked();
}

void ApiClient::ApplyBaseUrlLocked() {
  if (baseUrl_.empty()) baseUrl_ = kDefaultMainUrl;
  if (baseUrl_.find("://") == std::string::npos) {
    baseUrl_ = "https://" + baseUrl_;
  }

  schemeHostPort_.clear();
  prefix_.clear();

  static const std::regex kUrl(
      R"(^(https?)://(\[[0-9A-Fa-f:.]+\]|[^/:]+)(?::([0-9]+))?(/.*)?$)",
      std::regex::icase);
  std::smatch match;
  if (std::regex_match(baseUrl_, match, kUrl)) {
    schemeHostPort_ = match[1].str() + "://" + match[2].str();
    if (match[3].matched) schemeHostPort_ += ":" + match[3].str();
    if (match[4].matched) {
      prefix_ = match[4].str();
      while (!prefix_.empty() && prefix_.back() == '/') prefix_.pop_back();
    }
  } else {
    schemeHostPort_ = baseUrl_;
  }
}

void ApiClient::SetBaseUrl(std::string baseUrl) {
  std::lock_guard<std::mutex> lock(mutex_);
  baseUrl_ = std::move(baseUrl);
  ApplyBaseUrlLocked();
}

std::string ApiClient::BaseUrl() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return baseUrl_;
}

std::string ApiClient::Origin() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return schemeHostPort_;
}

void ApiClient::SetToken(std::string token) {
  std::lock_guard<std::mutex> lock(mutex_);
  token_ = std::move(token);
}

std::string ApiClient::Token() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return token_;
}

Result<nlohmann::json> ApiClient::HandleResponse(int status,
                                                 const std::string& body) {
  nlohmann::json j = nlohmann::json::parse(body, nullptr, false);
  if (status >= 400) {
    ApiError e;
    e.status = status;
    if (!j.is_discarded() && j.is_object() && j.contains("error") &&
        j.at("error").is_object()) {
      e.code = j.at("error").value("code", "");
      e.message = j.at("error").value("message", "");
    } else {
      e.code = "http_" + std::to_string(status);
      e.message = body.substr(0, 300);
    }
    return Result<nlohmann::json>::Err(std::move(e));
  }
  if (body.empty()) return Result<nlohmann::json>::Ok(nlohmann::json::object());
  if (j.is_discarded()) {
    ApiError e;
    e.status = status;
    e.code = "invalid_json";
    e.message = "服务器返回了无法解析的 JSON";
    return Result<nlohmann::json>::Err(std::move(e));
  }
  return Result<nlohmann::json>::Ok(std::move(j));
}

Result<nlohmann::json> ApiClient::GetJson(const std::string& path,
                                          const Query& query) {
  std::string origin;
  std::string prefix;
  std::string token;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    origin = schemeHostPort_;
    prefix = prefix_;
    token = token_;
  }

  httplib::Client cli(origin);
  cli.set_connection_timeout(5, 0);
  cli.set_read_timeout(20, 0);
  cli.set_write_timeout(20, 0);
  cli.set_follow_location(true);
  cli.enable_server_certificate_verification(!insecure_);

  httplib::Headers headers = {
      {"Accept", "application/json"},
      {"User-Agent", "spark-tui/" SPARK_TUI_VERSION},
  };
  if (!token.empty()) {
    headers.emplace("Authorization", "Bearer " + token);
  }

  httplib::Params params;
  for (const auto& [key, value] : query) params.emplace(key, value);

  const std::string full = prefix + path;
  auto res = cli.Get(full.c_str(), params, headers);
  if (!res) {
    return Result<nlohmann::json>::Err(NetworkError(res.error()));
  }
  return HandleResponse(res->status, res->body);
}

Result<nlohmann::json> ApiClient::PostJson(const std::string& path,
                                           const nlohmann::json& body) {
  std::string origin;
  std::string prefix;
  std::string token;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    origin = schemeHostPort_;
    prefix = prefix_;
    token = token_;
  }

  httplib::Client cli(origin);
  cli.set_connection_timeout(5, 0);
  cli.set_read_timeout(20, 0);
  cli.set_write_timeout(20, 0);
  cli.set_follow_location(true);
  cli.enable_server_certificate_verification(!insecure_);

  httplib::Headers headers = {
      {"Accept", "application/json"},
      {"User-Agent", "spark-tui/" SPARK_TUI_VERSION},
  };
  if (!token.empty()) {
    headers.emplace("Authorization", "Bearer " + token);
  }

  const std::string full = prefix + path;
  auto res = cli.Post(full.c_str(), headers, body.dump(), "application/json");
  if (!res) {
    return Result<nlohmann::json>::Err(NetworkError(res.error()));
  }
  return HandleResponse(res->status, res->body);
}

Result<SiteInfo> ApiClient::Site() {
  auto r = GetJson("/api/v2/site.json");
  if (!r.ok) return Result<SiteInfo>::Err(r.error);
  try {
    return Result<SiteInfo>::Ok(ParseSite(r.value));
  } catch (const std::exception& e) {
    return Result<SiteInfo>::Err({0, "parse_error", e.what()});
  }
}

Result<ListPage> ApiClient::List(const std::string& kind,
                                 const std::string& lang, int limit, int offset,
                                 const std::string& q,
                                 std::optional<bool> active) {
  Query query;
  query.emplace_back("limit", std::to_string(limit));
  query.emplace_back("offset", std::to_string(offset));
  if (!q.empty()) query.emplace_back("q", q);
  if (active.has_value()) {
    query.emplace_back("active", *active ? "true" : "false");
  }

  auto r = GetJson("/api/v2/" + UrlEncode(lang) + "/" + UrlEncode(kind) +
                       ".json",
                   query);
  if (!r.ok) return Result<ListPage>::Err(r.error);
  try {
    return Result<ListPage>::Ok(ParseList(r.value, kind));
  } catch (const std::exception& e) {
    return Result<ListPage>::Err({0, "parse_error", e.what()});
  }
}

Result<nlohmann::json> ApiClient::Item(const std::string& kind,
                                       const std::string& lang,
                                       const std::string& slug) {
  return GetJson("/api/v2/" + UrlEncode(lang) + "/" + UrlEncode(kind) + "/" +
                 UrlEncode(slug) + ".json");
}

Result<SearchResult> ApiClient::Search(const std::string& lang,
                                       const std::string& q, int limit,
                                       int offset) {
  Query query;
  query.emplace_back("q", q);
  query.emplace_back("limit", std::to_string(limit));
  query.emplace_back("offset", std::to_string(offset));

  auto r = GetJson("/api/v2/" + UrlEncode(lang) + "/search.json", query);
  if (!r.ok) return Result<SearchResult>::Err(r.error);
  try {
    return Result<SearchResult>::Ok(ParseSearch(r.value));
  } catch (const std::exception& e) {
    return Result<SearchResult>::Err({0, "parse_error", e.what()});
  }
}

Result<nlohmann::json> ApiClient::HomeNews() {
  return GetJson("/api/v2/home-news.json");
}

Result<nlohmann::json> ApiClient::HomeNewsAdd(const std::string& slug) {
  return PostJson("/api/v2/home-news/add", nlohmann::json{{"slug", slug}});
}

Result<nlohmann::json> ApiClient::HomeNewsRemove(const std::string& slug) {
  return PostJson("/api/v2/home-news/remove", nlohmann::json{{"slug", slug}});
}

Result<nlohmann::json> ApiClient::HomeNewsReorder(
    const std::vector<std::string>& picks) {
  return PostJson("/api/v2/home-news/reorder", nlohmann::json{{"picks", picks}});
}

Result<nlohmann::json> ApiClient::SaveNews(const std::string& slug,
                                           const nlohmann::json& data) {
  return PostJson("/api/v2/news/save",
                  nlohmann::json{{"slug", slug}, {"data", data}});
}

Result<nlohmann::json> ApiClient::DeleteNews(const std::string& slug) {
  return PostJson("/api/v2/news/" + UrlEncode(slug) + "/delete",
                  nlohmann::json::object());
}

Result<nlohmann::json> ApiClient::History(const std::string& entity,
                                          const std::string& slug) {
  Query query;
  query.emplace_back("entity", entity);
  query.emplace_back("slug", slug);
  return GetJson("/api/v2/history.json", query);
}

Result<nlohmann::json> ApiClient::RevertHistory(const std::string& id) {
  return PostJson("/api/v2/history/" + UrlEncode(id) + "/revert",
                  nlohmann::json::object());
}

Result<nlohmann::json> ApiClient::Me() { return GetJson("/api/v2/me.json"); }

Result<nlohmann::json> ApiClient::RawRequest(const std::string& method,
                                             const std::string& path,
                                             const nlohmann::json& body) {
  const std::string normalized = ToLower(Trim(method));
  const std::string target = Trim(path);
  if (target.empty()) {
    return Result<nlohmann::json>::Err({0, "invalid_path", "路径不能为空"});
  }
  if (normalized == "get") return GetJson(target);
  if (normalized == "post") {
    return PostJson(target, body.is_null() ? nlohmann::json::object() : body);
  }
  return Result<nlohmann::json>::Err(
      {0, "invalid_method", "仅支持 GET / POST"});
}

Result<DeviceCode> ApiClient::StartDeviceCode(
    const std::vector<std::string>& scopes) {
  nlohmann::json body = nlohmann::json::object();
  if (!scopes.empty()) body["scopes"] = scopes;

  auto r = PostJson("/api/v2/auth/device/code", body);
  if (!r.ok) return Result<DeviceCode>::Err(r.error);
  try {
    return Result<DeviceCode>::Ok(ParseDeviceCode(r.value));
  } catch (const std::exception& e) {
    return Result<DeviceCode>::Err({0, "parse_error", e.what()});
  }
}

Result<DeviceToken> ApiClient::PollDeviceToken(const std::string& deviceCode) {
  auto r = PostJson("/api/v2/auth/device/token",
                    nlohmann::json{{"device_code", deviceCode}});
  if (!r.ok) return Result<DeviceToken>::Err(r.error);
  try {
    return Result<DeviceToken>::Ok(ParseDeviceToken(r.value));
  } catch (const std::exception& e) {
    return Result<DeviceToken>::Err({0, "parse_error", e.what()});
  }
}

Result<nlohmann::json> ApiClient::RevokeToken(const std::string& id) {
  return PostJson("/api/v2/me/tokens/" + UrlEncode(id) + "/revoke",
                  nlohmann::json::object());
}

}  // namespace spark
