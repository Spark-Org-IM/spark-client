#include "i18n.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

namespace spark {
namespace {

std::string LowerTrim(std::string value) {
  std::size_t begin = 0;
  while (begin < value.size() &&
         (value[begin] == ' ' || value[begin] == '\t' ||
          value[begin] == '\r' || value[begin] == '\n')) {
    ++begin;
  }
  std::size_t end = value.size();
  while (end > begin && (value[end - 1] == ' ' || value[end - 1] == '\t' ||
                         value[end - 1] == '\r' || value[end - 1] == '\n')) {
    --end;
  }
  std::string out = value.substr(begin, end - begin);
  for (char& c : out) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (c == '_') c = '-';
  }
  return out;
}

std::string EnvValue(const char* name) {
  const char* value = std::getenv(name);
  return value == nullptr ? std::string{} : std::string(value);
}

bool ContainsTag(const std::vector<std::string>& available,
                 const std::string& tag) {
  for (const auto& entry : available) {
    if (LowerTrim(entry) == tag) return true;
  }
  return false;
}

std::string FirstWithPrefix(const std::vector<std::string>& available,
                            const std::string& prefix) {
  for (const auto& entry : available) {
    if (LowerTrim(entry).rfind(prefix, 0) == 0) return entry;
  }
  return {};
}

}  // namespace

bool IsEnglish(const std::string& lang) {
  const std::string tag = LowerTrim(lang);
  return tag.rfind("en", 0) == 0;
}

std::string Tr(const std::string& zh, const std::string& en,
               const std::string& lang) {
  return IsEnglish(lang) ? en : zh;
}

std::string SystemLanguageTag() {
  const char* names[] = {"LC_ALL", "LC_MESSAGES", "LANG", "LANGUAGE"};
  for (const char* name : names) {
    std::string value = LowerTrim(EnvValue(name));
    if (value.empty()) continue;
    // LANGUAGE may be a colon-separated priority list.
    const std::size_t colon = value.find(':');
    if (colon != std::string::npos) value = value.substr(0, colon);
    if (value.find("zh") != std::string::npos) return "zh-cn";
    if (value.find("en") != std::string::npos) return "en-us";
  }
  // C / POSIX locale: let the caller fall back to the site default.
  return {};
}

std::string ResolveLanguageTag(const std::string& preference,
                               const std::vector<std::string>& available,
                               const std::string& fallback) {
  std::string candidate = LowerTrim(preference);
  if (candidate.empty() || candidate == "system" || candidate == "auto" ||
      candidate == "follow") {
    candidate = SystemLanguageTag();
  }
  if (candidate.empty()) candidate = LowerTrim(fallback);

  if (available.empty()) return candidate.empty() ? "zh-cn" : candidate;

  if (ContainsTag(available, candidate)) {
    for (const auto& entry : available) {
      if (LowerTrim(entry) == candidate) return entry;
    }
  }

  if (candidate.rfind("zh", 0) == 0) {
    const std::string match = FirstWithPrefix(available, "zh");
    if (!match.empty()) return match;
  }
  if (candidate.rfind("en", 0) == 0) {
    const std::string match = FirstWithPrefix(available, "en");
    if (!match.empty()) return match;
  }

  const std::string normalized_fallback = LowerTrim(fallback);
  if (ContainsTag(available, normalized_fallback)) {
    for (const auto& entry : available) {
      if (LowerTrim(entry) == normalized_fallback) return entry;
    }
  }

  return available.front();
}

std::string LanguageDisplayName(const std::string& tag,
                                const std::string& uiLang) {
  const std::string normalized = LowerTrim(tag);
  if (normalized.empty() || normalized == "system" || normalized == "auto") {
    return Tr("跟随系统", "System", uiLang);
  }
  if (normalized.rfind("zh", 0) == 0) return "中文";
  if (normalized.rfind("en", 0) == 0) return "English";
  return tag;
}

}  // namespace spark
