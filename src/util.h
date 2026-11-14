#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace spark {

// UTF-8 aware helpers -------------------------------------------------------

// Decode one UTF-8 code point starting at `i`. Returns the number of bytes
// consumed (>= 1). Invalid sequences yield U+FFFD and consume one byte.
std::size_t Utf8Decode(const std::string& s, std::size_t i, char32_t& cp);

// Display width of a code point (CJK wide = 2, combining = 0, else 1).
std::size_t CodePointWidth(char32_t cp);

// Display width of a UTF-8 string in terminal columns.
int DisplayWidth(const std::string& text);

// Truncate `text` to at most `max_width` display columns. When truncated, an
// ellipsis ("…") is appended. Used to keep rows from colliding in narrow
// terminals.
std::string Ellipsize(const std::string& text, int max_width);

// Byte offset of the previous / next UTF-8 code point boundary.
std::size_t Utf8Prev(const std::string& s, std::size_t i);
std::size_t Utf8Next(const std::string& s, std::size_t i);

// Wrap `text` to at most `width` display columns. Explicit '\n' is honoured.
std::vector<std::string> WrapText(const std::string& text, std::size_t width);

std::vector<std::string> SplitLines(const std::string& text);
std::string Trim(const std::string& s);
std::string ToLower(std::string s);

// HTML-ish content helpers (used by rendering and by the Flarum client).
std::string DecodeHtmlEntities(const std::string& input);
std::string NormalizePlainText(const std::string& input);
std::string StripHtml(const std::string& html);

// Truncate an API date ("YYYY-MM-DD HH:MM:SS") to the precision declared by
// `accuracy` (year / month / day / hour / minute / second). Unknown values and
// empty strings are returned unchanged. This keeps dates readable for users
// instead of always showing a meaningless 00:00:00.
std::string FormatDateByAccuracy(const std::string& start,
                                 const std::string& accuracy);

// Percent-encode a query-string component (RFC 3986 unreserved kept).
std::string UrlEncode(const std::string& s);

// Normalize a user-entered URL or a bare domain. Returns nullopt for blank or
// invalid input. A bare domain gets "https://"; a trailing slash is removed.
// This mirrors the Android client's InstanceConfigRepository.normalizeUrl.
std::optional<std::string> NormalizeUrl(const std::string& raw);

// Filesystem / process ------------------------------------------------------
std::string HomeDir();
std::string ConfigDir();                 // $XDG_CONFIG_HOME/spark-tui
std::string DefaultConfigPath();         // ConfigDir()/config.toml
bool EnsureDirectory(const std::string& path);
bool OpenUrl(const std::string& url);

// Imports an ASCII-armored PGP public key with `gpg --import`. The key is
// written to a private temporary file first, so no shell quoting is involved.
// Returns false when gpg is missing or rejects the key.
bool ImportPgpKey(const std::string& key);

}  // namespace spark
