#pragma once

#include <string>
#include <vector>

namespace spark {

// True for English tags such as "en", "en-us", "en-US", "en-gb".
bool IsEnglish(const std::string& lang);

// Picks the Chinese or English text for `lang`.
std::string Tr(const std::string& zh, const std::string& en,
               const std::string& lang);

// Best-effort system UI language from LC_ALL / LC_MESSAGES / LANG / LANGUAGE.
// Returns "en-us" or "zh-cn".
std::string SystemLanguageTag();

// Resolves a stored preference ("system", "en-us", "zh-cn", ...) against the
// languages the server offers. Unknown tags fall back to `fallback`, then to
// the first available language, then "zh-cn".
std::string ResolveLanguageTag(const std::string& preference,
                               const std::vector<std::string>& available,
                               const std::string& fallback);

// Native display name for a language tag ("English", "中文", or the tag).
std::string LanguageDisplayName(const std::string& tag,
                                const std::string& uiLang);

}  // namespace spark
