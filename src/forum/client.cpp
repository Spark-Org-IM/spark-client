#include "forum/client.h"

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
  e.message = "无法连接论坛服务器（" +
              std::to_string(static_cast<int>(error)) + "）";
  return e;
}

void FillDiscussionUrls(ForumPage& page, const std::string& origin) {
  for (ForumDiscussion& item : page.items) {
    const std::string slug = item.slug.empty() ? item.id : item.slug;
    if (!origin.empty() && !slug.empty()) {
      item.url = origin + "/d/" + slug;
    }
  }
}

}  // namespace

ForumClient::ForumClient(std::string baseUrl, bool insecure, std::string token)
    : baseUrl_(std::move(baseUrl)),
      insecure_(insecure),
      token_(std::move(token)) {
  ApplyBaseUrlLocked();
}

void ForumClient::ApplyBaseUrlLocked() {
  if (baseUrl_.empty()) baseUrl_ = kDefaultForumUrl;
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

void ForumClient::SetBaseUrl(std::string baseUrl) {
  std::lock_guard<std::mutex> lock(mutex_);
  baseUrl_ = std::move(baseUrl);
  ApplyBaseUrlLocked();
}

std::string ForumClient::BaseUrl() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return baseUrl_;
}

std::string ForumClient::Origin() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return schemeHostPort_;
}

void ForumClient::SetToken(std::string token) {
  std::lock_guard<std::mutex> lock(mutex_);
  token_ = std::move(token);
}

std::string ForumClient::Token() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return token_;
}

std::string ForumClient::PathFor(const std::string& next) const {
  if (next.empty()) return "/api/discussions";
  if (next.front() == '/') return next;

  const std::size_t scheme = next.find("://");
  if (scheme == std::string::npos) return next;
  const std::size_t slash = next.find('/', scheme + 3);
  if (slash == std::string::npos) return "/";
  return next.substr(slash);
}

Result<nlohmann::json> ForumClient::HandleResponse(int status,
                                                   const std::string& body) {
  nlohmann::json j = nlohmann::json::parse(body, nullptr, false);
  if (status >= 400) {
    ApiError e;
    e.status = status;
    if (!j.is_discarded() && j.is_object() && j.contains("errors") &&
        j.at("errors").is_array() && !j.at("errors").empty() &&
        j.at("errors").front().is_object()) {
      const auto& first = j.at("errors").front();
      e.code = first.value("code", "");
      e.message = first.value("detail", "");
      if (e.message.empty()) e.message = first.value("title", "");
    }
    if (e.code.empty() && e.message.empty()) {
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
    e.message = "论坛返回了无法解析的 JSON";
    return Result<nlohmann::json>::Err(std::move(e));
  }
  return Result<nlohmann::json>::Ok(std::move(j));
}

Result<nlohmann::json> ForumClient::GetJson(const std::string& path,
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
      {"Accept", "application/vnd.api+json"},
      {"User-Agent", "spark-tui/" SPARK_TUI_VERSION},
  };
  if (!token.empty()) {
    headers.emplace("Authorization", "Token " + token);
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

Result<nlohmann::json> ForumClient::PostJson(const std::string& path,
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
      {"Accept", "application/vnd.api+json"},
      {"User-Agent", "spark-tui/" SPARK_TUI_VERSION},
  };
  if (!token.empty()) {
    headers.emplace("Authorization", "Token " + token);
  }

  const std::string full = prefix + path;
  auto res = cli.Post(full.c_str(), headers, body.dump(), "application/json");
  if (!res) {
    return Result<nlohmann::json>::Err(NetworkError(res.error()));
  }
  return HandleResponse(res->status, res->body);
}

Result<nlohmann::json> ForumClient::PatchJson(const std::string& path,
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
      {"Accept", "application/vnd.api+json"},
      {"User-Agent", "spark-tui/" SPARK_TUI_VERSION},
  };
  if (!token.empty()) {
    headers.emplace("Authorization", "Token " + token);
  }

  const std::string full = prefix + path;
  auto res = cli.Patch(full.c_str(), headers, body.dump(),
                       "application/vnd.api+json");
  if (!res) {
    return Result<nlohmann::json>::Err(NetworkError(res.error()));
  }
  return HandleResponse(res->status, res->body);
}

Result<nlohmann::json> ForumClient::DeleteJson(const std::string& path) {
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
      {"Accept", "application/vnd.api+json"},
      {"User-Agent", "spark-tui/" SPARK_TUI_VERSION},
  };
  if (!token.empty()) {
    headers.emplace("Authorization", "Token " + token);
  }

  const std::string full = prefix + path;
  auto res = cli.Delete(full.c_str(), headers);
  if (!res) {
    return Result<nlohmann::json>::Err(NetworkError(res.error()));
  }
  return HandleResponse(res->status, res->body);
}

Result<ForumPage> ForumClient::Discussions(int limit, const std::string& next) {
  Query query;
  const std::string path = PathFor(next);
  if (next.empty()) {
    query.emplace_back("page[limit]", std::to_string(limit));
    query.emplace_back("page[offset]", "0");
    query.emplace_back("sort", "-lastPostedAt");
  }

  auto r = GetJson(path, query);
  if (!r.ok) return Result<ForumPage>::Err(r.error);
  try {
    ForumPage page = ParseForumDiscussions(r.value);
    FillDiscussionUrls(page, Origin());
    return Result<ForumPage>::Ok(std::move(page));
  } catch (const std::exception& e) {
    return Result<ForumPage>::Err({0, "parse_error", e.what()});
  }
}

Result<ForumThread> ForumClient::Discussion(const std::string& id) {
  auto r = GetJson("/api/discussions/" + UrlEncode(id));
  if (!r.ok) return Result<ForumThread>::Err(r.error);
  try {
    ForumThread thread = ParseForumThread(r.value);
    const std::string slug =
        thread.discussion.slug.empty() ? thread.discussion.id
                                       : thread.discussion.slug;
    if (!slug.empty()) thread.discussion.url = Origin() + "/d/" + slug;
    return Result<ForumThread>::Ok(std::move(thread));
  } catch (const std::exception& e) {
    return Result<ForumThread>::Err({0, "parse_error", e.what()});
  }
}

Result<ForumUser> ForumClient::User(const std::string& id) {
  auto r = GetJson("/api/users/" + UrlEncode(id));
  if (!r.ok) return Result<ForumUser>::Err(r.error);
  try {
    return Result<ForumUser>::Ok(ParseForumUser(r.value));
  } catch (const std::exception& e) {
    return Result<ForumUser>::Err({0, "parse_error", e.what()});
  }
}

Result<nlohmann::json> ForumClient::UpdateDiscussion(
    const std::string& id, const nlohmann::json& attributes) {
  nlohmann::json body = {
      {"data",
       {
           {"type", "discussions"},
           {"id", id},
           {"attributes", attributes},
       }},
  };
  return PatchJson("/api/discussions/" + UrlEncode(id), body);
}

Result<nlohmann::json> ForumClient::DeleteDiscussion(const std::string& id) {
  return DeleteJson("/api/discussions/" + UrlEncode(id));
}

Result<std::vector<ForumTag>> ForumClient::Tags() {
  auto r = GetJson("/api/tags");
  if (!r.ok) return Result<std::vector<ForumTag>>::Err(r.error);
  try {
    std::vector<ForumTag> tags;
    if (r.value.is_object() && r.value.contains("data") &&
        r.value.at("data").is_array()) {
      for (const auto& entry : r.value.at("data")) {
        if (entry.is_object()) tags.push_back(ParseForumTag(entry));
      }
    }
    return Result<std::vector<ForumTag>>::Ok(std::move(tags));
  } catch (const std::exception& e) {
    return Result<std::vector<ForumTag>>::Err({0, "parse_error", e.what()});
  }
}

Result<nlohmann::json> ForumClient::SetDiscussionTags(
    const std::string& id, const std::vector<std::string>& tagIds) {
  nlohmann::json tags = nlohmann::json::array();
  for (const auto& tag_id : tagIds) {
    tags.push_back({{"type", "tags"}, {"id", tag_id}});
  }
  nlohmann::json body = {
      {"data",
       {
           {"type", "discussions"},
           {"id", id},
           {"relationships", {{"tags", {{"data", tags}}}}},
       }},
  };
  return PatchJson("/api/discussions/" + UrlEncode(id), body);
}

Result<nlohmann::json> ForumClient::Reply(const std::string& discussionId,
                                          const std::string& content) {
  nlohmann::json body = {
      {"data",
       {
           {"type", "posts"},
           {"attributes", {{"content", content}}},
           {"relationships",
            {{"discussion",
              {{"data", {{"type", "discussions"}, {"id", discussionId}}}}}}},
       }},
  };
  return PostJson("/api/posts", body);
}

Result<nlohmann::json> ForumClient::CreateDiscussion(
    const std::string& title, const std::string& content,
    const std::vector<std::string>& tagIds) {
  nlohmann::json tags = nlohmann::json::array();
  for (const auto& tag_id : tagIds) {
    tags.push_back({{"type", "tags"}, {"id", tag_id}});
  }
  nlohmann::json body = {
      {"data",
       {
           {"type", "discussions"},
           {"attributes", {{"title", title}, {"content", content}}},
           {"relationships", {{"tags", {{"data", tags}}}}},
       }},
  };
  return PostJson("/api/discussions", body);
}

Result<nlohmann::json> ForumClient::RawRequest(const std::string& method,
                                               const std::string& path,
                                               const nlohmann::json& body) {
  std::string normalized = ToLower(Trim(method));
  std::string target = Trim(path);
  if (target.empty()) {
    return Result<nlohmann::json>::Err({0, "invalid_path", "路径不能为空"});
  }
  target = PathFor(target);
  if (!target.empty() && target.front() != '/') target = "/" + target;

  if (normalized == "get") return GetJson(target);
  if (normalized == "post") return PostJson(target, body);
  if (normalized == "patch") return PatchJson(target, body);
  if (normalized == "delete") return DeleteJson(target);
  return Result<nlohmann::json>::Err(
      {0, "invalid_method", "仅支持 GET / POST / PATCH / DELETE"});
}

Result<ForumAuth> ForumClient::Login(const std::string& identification,
                                     const std::string& password) {
  nlohmann::json body = {
      {"identification", identification},
      {"password", password},
      {"remember", true},
  };
  auto r = PostJson("/api/token", body);
  if (!r.ok) return Result<ForumAuth>::Err(r.error);
  try {
    return Result<ForumAuth>::Ok(ParseForumAuth(r.value));
  } catch (const std::exception& e) {
    return Result<ForumAuth>::Err({0, "parse_error", e.what()});
  }
}

}  // namespace spark
