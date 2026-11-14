#include "api/models.h"

#include <algorithm>

#include "util.h"

namespace spark {

std::string FormatDateField(const nlohmann::json& object) {
  if (!object.is_object() || !object.contains("date")) return {};
  const auto& date = object.at("date");
  if (date.is_string()) return date.get<std::string>();
  if (date.is_object()) {
    return FormatDateByAccuracy(date.value("start", std::string{}),
                                date.value("accuracy", std::string{}));
  }
  return {};
}

SiteInfo ParseSite(const nlohmann::json& j) {
  SiteInfo site;
  site.name = j.value("name", "");
  site.apiVersion = j.value("apiVersion", "");
  site.defaultLanguage = j.value("defaultLanguage", "zh-cn");
  if (j.contains("languages") && j.at("languages").is_array()) {
    for (const auto& lang : j.at("languages")) {
      if (lang.is_string()) site.languages.push_back(lang.get<std::string>());
    }
  }
  if (site.languages.empty()) site.languages.push_back(site.defaultLanguage);
  return site;
}

ListItem ParseListItem(const nlohmann::json& j, const std::string& kind) {
  ListItem item;
  item.kind = kind;
  item.raw = j;
  item.slug = j.value("slug", "");
  item.url = j.value("url", "");
  item.priority = j.value("priority", 999);
  item.active = j.value("active", true);

  if (kind == "news") {
    item.title = j.value("title", item.slug);
    item.subtitle = FormatDateField(j);
    if (!item.active) item.subtitle += "  ·  停用";
  } else if (kind == "contact") {
    item.title = j.value("name", item.slug);
    item.subtitle = j.value("title", "");
  } else if (kind == "platform") {
    item.title = j.value("name", item.slug);
    item.subtitle = j.value("desc", "");
  } else {
    item.title = j.value("title", j.value("name", item.slug));
    item.subtitle = j.value("desc", "");
  }
  return item;
}

ListPage ParseList(const nlohmann::json& j, const std::string& kind) {
  ListPage page;
  page.count = j.value("count", 0);
  page.total = j.value("total", 0);
  page.limit = j.value("limit", 20);
  page.offset = j.value("offset", 0);
  page.hasMore = j.value("hasMore", false);
  if (j.contains("items") && j.at("items").is_array()) {
    for (const auto& entry : j.at("items")) {
      page.items.push_back(ParseListItem(entry, kind));
    }
  }
  return page;
}

SearchResult ParseSearch(const nlohmann::json& j) {
  SearchResult result;
  result.total = j.value("total", 0);
  result.hasMore = j.value("hasMore", false);
  if (!j.contains("groups") || !j.at("groups").is_object()) return result;

  static const char* kOrder[] = {"news", "contact", "platform"};
  const auto& groups = j.at("groups");
  for (const char* kind : kOrder) {
    if (!groups.contains(kind)) continue;
    const auto& group = groups.at(kind);
    if (!group.contains("items") || !group.at("items").is_array()) continue;
    for (const auto& entry : group.at("items")) {
      auto item = ParseListItem(entry, kind);
      const std::string label = KindLabel(kind);
      item.subtitle = item.subtitle.empty() ? label : label + " · " + item.subtitle;
      result.items.push_back(std::move(item));
    }
  }
  return result;
}

DeviceCode ParseDeviceCode(const nlohmann::json& j) {
  DeviceCode code;
  code.deviceCode = j.value("device_code", "");
  code.userCode = j.value("user_code", "");
  code.verificationUri = j.value("verification_uri", "");
  code.verificationUriComplete = j.value("verification_uri_complete", "");
  code.expiresIn = j.value("expires_in", 600);
  code.interval = j.value("interval", 5);
  if (j.contains("scopes") && j.at("scopes").is_array()) {
    for (const auto& s : j.at("scopes")) {
      if (s.is_string()) code.scopes.push_back(s.get<std::string>());
    }
  }
  return code;
}

DeviceToken ParseDeviceToken(const nlohmann::json& j) {
  DeviceToken token;
  token.plaintext = j.value("plaintext", "");
  if (j.contains("token")) token.token = j.at("token");
  return token;
}

std::string KindLabel(const std::string& kind) {
  if (kind == "news") return "新闻";
  if (kind == "contact") return "联系";
  if (kind == "platform") return "平台";
  return kind;
}

}  // namespace spark
