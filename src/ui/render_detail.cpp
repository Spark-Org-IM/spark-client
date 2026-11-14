#include "ui/render_detail.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "api/models.h"
#include "i18n.h"
#include "util.h"

namespace spark {
namespace {

void AppendJsonValue(std::vector<std::string>& out, const std::string& key,
                     const nlohmann::json& value, int width, int indent) {
  const std::string pad(static_cast<std::size_t>(indent), ' ');

  if (value.is_string()) {
    const std::string text_value = value.get<std::string>();
    const auto wrapped = WrapText(
        text_value, static_cast<std::size_t>(std::max(10, width - indent - 2)));
    if (wrapped.size() <= 1) {
      out.push_back(pad + key + ": " + (wrapped.empty() ? "" : wrapped.front()));
    } else {
      out.push_back(pad + key + ":");
      for (const auto& line : wrapped) out.push_back(pad + "  " + line);
    }
    return;
  }

  if (value.is_object()) {
    out.push_back(pad + key + ":");
    for (const auto& [sub_key, sub_value] : value.items()) {
      AppendJsonValue(out, sub_key, sub_value, width, indent + 2);
    }
    return;
  }

  if (value.is_array()) {
    if (value.empty()) {
      out.push_back(pad + key + ": []");
      return;
    }
    out.push_back(pad + key + ":");
    for (const auto& entry : value) {
      if (entry.is_string()) {
        const auto wrapped = WrapText(
            entry.get<std::string>(),
            static_cast<std::size_t>(std::max(10, width - indent - 4)));
        for (std::size_t i = 0; i < wrapped.size(); ++i) {
          out.push_back(pad + (i == 0 ? "  - " : "    ") + wrapped[i]);
        }
      } else {
        out.push_back(pad + "  - " + entry.dump());
      }
    }
    return;
  }

  out.push_back(pad + key + ": " + value.dump());
}

void AddBlank(std::vector<std::string>& out) {
  if (!out.empty() && !out.back().empty()) out.emplace_back();
}

std::string JsonString(const nlohmann::json& object, const char* key) {
  if (!object.is_object() || !object.contains(key) ||
      !object.at(key).is_string()) {
    return {};
  }
  return object.at(key).get<std::string>();
}

std::string DateStart(const nlohmann::json& object) {
  return FormatDateField(object);
}

void AddField(std::vector<std::string>& out, const std::string& label,
              const std::string& value, int width) {
  if (value.empty()) return;
  const std::string prefix = label + ": ";
  const int prefix_width = DisplayWidth(prefix);
  const int available = std::max(10, width - prefix_width);
  const auto wrapped = WrapText(value, static_cast<std::size_t>(available));
  if (wrapped.empty()) return;
  out.push_back(prefix + wrapped.front());
  for (std::size_t i = 1; i < wrapped.size(); ++i) {
    out.push_back(std::string(static_cast<std::size_t>(prefix_width), ' ') +
                  wrapped[i]);
  }
}

void AddBlock(std::vector<std::string>& out, const std::string& label,
              const std::string& value, int width) {
  if (value.empty()) return;
  out.push_back(label + ":");
  const auto wrapped =
      WrapText(value, static_cast<std::size_t>(std::max(10, width - 2)));
  for (const auto& line : wrapped) out.push_back("  " + line);
}

void AddStringList(std::vector<std::string>& out, const std::string& label,
                   const nlohmann::json& array, int width) {
  if (!array.is_array() || array.empty()) return;
  out.push_back(label + ":");
  for (const auto& entry : array) {
    if (!entry.is_string()) continue;
    const auto wrapped =
        WrapText(entry.get<std::string>(),
                 static_cast<std::size_t>(std::max(10, width - 4)));
    for (std::size_t i = 0; i < wrapped.size(); ++i) {
      out.push_back(std::string(i == 0 ? "  - " : "    ") + wrapped[i]);
    }
  }
}

void AddObjectList(std::vector<std::string>& out, const std::string& label,
                   const nlohmann::json& array, const char* name_key,
                   int width) {
  if (!array.is_array() || array.empty()) return;
  out.push_back(label + ":");
  for (const auto& entry : array) {
    if (!entry.is_object()) continue;
    const std::string name = JsonString(entry, name_key);
    const std::string url = JsonString(entry, "url");
    std::string text = url;
    if (!name.empty()) text = name + ": " + url;
    if (text.empty()) continue;
    const auto wrapped = WrapText(
        text, static_cast<std::size_t>(std::max(10, width - 4)));
    for (std::size_t i = 0; i < wrapped.size(); ++i) {
      out.push_back(std::string(i == 0 ? "  - " : "    ") + wrapped[i]);
    }
  }
}

void AddImages(std::vector<std::string>& out, const nlohmann::json& data,
               int width, const std::string& lang) {
  if (!data.is_object() || !data.contains("images") ||
      !data.at("images").is_array()) {
    return;
  }
  AddObjectList(out, Tr("图片", "Images", lang), data.at("images"), "alt", width);
}

void AddLinks(std::vector<std::string>& out, const nlohmann::json& data,
              int width, const std::string& lang) {
  if (!data.is_object() || !data.contains("links") ||
      !data.at("links").is_array()) {
    return;
  }
  AddObjectList(out, Tr("链接", "Links", lang), data.at("links"), "name", width);
}

void RenderNews(std::vector<std::string>& out, const nlohmann::json& data,
                int width, const std::string& lang) {
  AddField(out, Tr("日期", "Date", lang), DateStart(data), width);

  if (data.contains("place") && data.at("place").is_object()) {
    const auto& place = data.at("place");
    AddField(out, Tr("地点", "Place", lang), JsonString(place, "desc"), width);
    AddField(out, Tr("坐标", "Coordinates", lang), JsonString(place, "geo"),
             width);
  }

  AddBlank(out);
  const std::string desc = StripHtml(JsonString(data, "desc"));
  AddBlock(out, Tr("正文", "Body", lang), desc, width);

  AddBlank(out);
  AddImages(out, data, width, lang);
  AddLinks(out, data, width, lang);
  AddField(out, Tr("原文", "Source", lang), JsonString(data, "url"), width);
}

void RenderContact(std::vector<std::string>& out, const nlohmann::json& data,
                   int width, const std::string& lang) {
  AddField(out, Tr("姓名", "Name", lang), JsonString(data, "name"), width);
  AddField(out, Tr("职务", "Title", lang), JsonString(data, "title"), width);
  AddBlank(out);

  const nlohmann::json empty = nlohmann::json::array();
  AddStringList(out, Tr("电话", "Phone", lang), data.value("tel", empty),
                width);
  AddStringList(out, Tr("备用电话", "Alt phone", lang),
                data.value("telExtra", empty), width);
  AddStringList(out, Tr("邮箱", "Email", lang), data.value("email", empty),
                width);
  AddStringList(out, Tr("备用邮箱", "Alt email", lang),
                data.value("emailExtra", empty), width);
  AddObjectList(out, Tr("主页", "Homepage", lang),
                data.value("homepage", empty), "name", width);
  AddObjectList(out, Tr("备用主页", "Alt homepage", lang),
                data.value("homepageExtra", empty), "name", width);
  AddObjectList(out, Tr("社交", "Social", lang), data.value("social", empty),
                "name", width);

  AddBlank(out);
  AddField(out, Tr("PGP 指纹", "PGP fingerprint", lang),
           JsonString(data, "pgpFingerprint"), width);
  if (!JsonString(data, "pgpKey").empty()) {
    out.push_back(Tr("PGP 公钥: 已省略（按 x 查看底层数据）",
                     "PGP public key: hidden (press x for raw data)", lang));
  }
  AddField(out, Tr("原文", "Source", lang), JsonString(data, "url"), width);
}

void RenderPlatform(std::vector<std::string>& out, const nlohmann::json& data,
                    int width, const std::string& lang) {
  AddField(out, Tr("名称", "Name", lang), JsonString(data, "name"), width);
  AddBlank(out);
  AddBlock(out, Tr("简介", "Description", lang),
           StripHtml(JsonString(data, "desc")), width);
  AddBlank(out);
  AddField(out, Tr("按钮", "Button", lang), JsonString(data, "button"), width);
  AddField(out, Tr("地址", "URL", lang), JsonString(data, "url"), width);
  AddField(out, Tr("站内页", "Site page", lang), JsonString(data, "pageUrl"),
           width);
}

}  // namespace

std::vector<std::string> BuildJsonLines(const nlohmann::json& json, int width) {
  std::vector<std::string> out;
  if (!json.is_object()) {
    return WrapText(json.dump(2),
                    static_cast<std::size_t>(std::max(10, width)));
  }
  for (const auto& [key, value] : json.items()) {
    AppendJsonValue(out, key, value, width, 0);
  }
  if (out.empty()) out.emplace_back("(空)");
  return out;
}

std::vector<std::string> RenderDetailLines(const std::string& kind,
                                           const nlohmann::json& data,
                                           int width, bool raw,
                                           const std::string& lang) {
  if (raw || !data.is_object()) return BuildJsonLines(data, width);

  std::vector<std::string> out;
  const std::string normalized = ToLower(kind);
  if (normalized == "news") {
    RenderNews(out, data, width, lang);
  } else if (normalized == "contact") {
    RenderContact(out, data, width, lang);
  } else if (normalized == "platform") {
    RenderPlatform(out, data, width, lang);
  } else {
    return BuildJsonLines(data, width);
  }

  while (!out.empty() && out.back().empty()) out.pop_back();
  if (out.empty()) out.emplace_back(Tr("(无内容)", "(no content)", lang));
  return out;
}

std::vector<std::string> RenderMeLines(const nlohmann::json& data, int width,
                                       bool raw, const std::string& lang) {
  if (raw) return BuildJsonLines(data, width);

  const nlohmann::json* user = &data;
  if (data.is_object() && data.contains("user") &&
      data.at("user").is_object()) {
    user = &data.at("user");
  }
  if (!user->is_object()) return BuildJsonLines(data, width);

  std::vector<std::string> out;
  AddField(out, Tr("用户名", "Username", lang), JsonString(*user, "username"),
           width);
  std::string display_name = JsonString(*user, "fullName");
  if (display_name.empty()) display_name = JsonString(*user, "display_name");
  if (display_name.empty()) display_name = JsonString(*user, "displayName");
  AddField(out, Tr("显示名", "Display name", lang), display_name, width);
  AddField(out, Tr("角色", "Role", lang), JsonString(*user, "role"), width);
  AddField(out, Tr("最近在线", "Last seen", lang),
           JsonString(*user, "lastSeenAt"), width);
  AddField(out, Tr("显示邮箱", "Display email", lang),
           JsonString(*user, "displayEmail"), width);

  const std::string avatar = JsonString(*user, "avatar");
  out.push_back(Tr("头像", "Avatar", lang) + ": " +
                (avatar.empty() ? Tr("无", "none", lang) : avatar));

  const nlohmann::json empty = nlohmann::json::array();
  AddStringList(out, Tr("声明邮箱", "Declared emails", lang),
                user->value("emails", empty), width);

  AddBlank(out);
  AddField(out, Tr("PGP 指纹", "PGP fingerprint", lang),
           JsonString(*user, "pgpFingerprint"), width);
  AddField(out, Tr("PGP 算法", "PGP algorithm", lang),
           JsonString(*user, "pgpAlgorithm"), width);
  AddField(out, Tr("PGP 创建时间", "PGP created", lang),
           JsonString(*user, "pgpCreatedAt"), width);

  if (user->contains("pgpSelfSignatures") &&
      user->at("pgpSelfSignatures").is_number_integer()) {
    out.push_back(Tr("PGP 自签名数", "PGP self-signatures", lang) + ": " +
                  std::to_string(user->at("pgpSelfSignatures").get<long long>()));
  }
  if (user->contains("pgpThirdPartySignatures") &&
      user->at("pgpThirdPartySignatures").is_number_integer()) {
    out.push_back(Tr("PGP 第三方签名数", "PGP third-party signatures", lang) +
                  ": " +
                  std::to_string(
                      user->at("pgpThirdPartySignatures").get<long long>()));
  }
  if (user->contains("pgpRevoked") && user->at("pgpRevoked").is_boolean() &&
      user->at("pgpRevoked").get<bool>()) {
    out.push_back(Tr("PGP 状态: 已撤销", "PGP status: revoked", lang));
  }
  if (!JsonString(*user, "pgpPublicKey").empty()) {
    out.push_back(Tr("PGP 公钥: 已省略（按 x 查看底层数据）",
                     "PGP public key: hidden (press x for raw data)", lang));
  }

  while (!out.empty() && out.back().empty()) out.pop_back();
  if (out.empty()) out.emplace_back(Tr("(无内容)", "(no content)", lang));
  return out;
}

}  // namespace spark
