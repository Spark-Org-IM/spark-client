#pragma once

#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace spark {

struct SiteInfo {
  std::string name;
  std::string apiVersion;
  std::string defaultLanguage;
  std::vector<std::string> languages;
};

struct ListItem {
  std::string kind;  // news / contact / platform
  std::string slug;
  std::string title;
  std::string subtitle;
  std::string url;
  int priority = 999;
  bool active = true;
  nlohmann::json raw;
};

struct ListPage {
  int count = 0;
  int total = 0;
  int limit = 20;
  int offset = 0;
  bool hasMore = false;
  std::vector<ListItem> items;
};

struct SearchResult {
  int total = 0;
  bool hasMore = false;
  std::vector<ListItem> items;
};

// Formats the `date` field of a news item using its declared accuracy, e.g.
// {"start": "2026-09-25 00:00:00", "accuracy": "day"} -> "2026-09-25".
std::string FormatDateField(const nlohmann::json& object);

struct DeviceCode {
  std::string deviceCode;
  std::string userCode;
  std::string verificationUri;
  std::string verificationUriComplete;
  int expiresIn = 600;
  int interval = 5;
  std::vector<std::string> scopes;
};

struct DeviceToken {
  std::string plaintext;
  nlohmann::json token;
};

struct ApiError {
  int status = 0;
  std::string code;
  std::string message;

  std::string ToString() const {
    std::string out;
    if (status > 0) out += "HTTP " + std::to_string(status) + ": ";
    if (!code.empty()) out += code;
    if (!message.empty()) {
      if (!code.empty()) out += " — ";
      out += message;
    }
    if (out.empty()) out = "未知错误";
    return out;
  }
};

template <class T>
struct Result {
  bool ok = false;
  T value{};
  ApiError error{};

  static Result Ok(T v) {
    Result r;
    r.ok = true;
    r.value = std::move(v);
    return r;
  }
  static Result Err(ApiError e) {
    Result r;
    r.ok = false;
    r.error = std::move(e);
    return r;
  }
};

// Parsers (throw nlohmann::json exceptions only on structurally invalid input;
// callers wrap them).
SiteInfo ParseSite(const nlohmann::json& j);
ListItem ParseListItem(const nlohmann::json& j, const std::string& kind);
ListPage ParseList(const nlohmann::json& j, const std::string& kind);
SearchResult ParseSearch(const nlohmann::json& j);
DeviceCode ParseDeviceCode(const nlohmann::json& j);
DeviceToken ParseDeviceToken(const nlohmann::json& j);

std::string KindLabel(const std::string& kind);

}  // namespace spark
