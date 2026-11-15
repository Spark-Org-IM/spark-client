#include "util.h"

#ifdef _WIN32
#define NOMINMAX
#include <fcntl.h>
#include <io.h>
#include <shellapi.h>
#include <sys/stat.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <utility>
#include <vector>

namespace spark {
namespace {

bool IsCombining(char32_t cp) {
  return (cp >= 0x0300 && cp <= 0x036F) ||
         (cp >= 0x1AB0 && cp <= 0x1AFF) ||
         (cp >= 0x1DC0 && cp <= 0x1DFF) ||
         (cp >= 0x20D0 && cp <= 0x20FF) ||
         (cp >= 0xFE20 && cp <= 0xFE2F) ||
         (cp >= 0x200B && cp <= 0x200F) ||
         cp == 0xFEFF;
}

bool IsWide(char32_t cp) {
  return (cp >= 0x1100 && cp <= 0x115F) ||
         (cp >= 0x2329 && cp <= 0x232A) ||
         (cp >= 0x2E80 && cp <= 0x303E) ||
         (cp >= 0x3041 && cp <= 0x33FF) ||
         (cp >= 0x3400 && cp <= 0x4DBF) ||
         (cp >= 0x4E00 && cp <= 0x9FFF) ||
         (cp >= 0xA000 && cp <= 0xA4CF) ||
         (cp >= 0xA960 && cp <= 0xA97F) ||
         (cp >= 0xAC00 && cp <= 0xD7A3) ||
         (cp >= 0xF900 && cp <= 0xFAFF) ||
         (cp >= 0xFE10 && cp <= 0xFE19) ||
         (cp >= 0xFE30 && cp <= 0xFE6F) ||
         (cp >= 0xFF00 && cp <= 0xFF60) ||
         (cp >= 0xFFE0 && cp <= 0xFFE6) ||
         (cp >= 0x1F300 && cp <= 0x1F64F) ||
         (cp >= 0x1F900 && cp <= 0x1F9FF) ||
         (cp >= 0x20000 && cp <= 0x3FFFD);
}

}  // namespace

std::size_t Utf8Decode(const std::string& s, std::size_t i, char32_t& cp) {
  if (i >= s.size()) {
    cp = 0;
    return 0;
  }
  const auto c = static_cast<unsigned char>(s[i]);
  if (c < 0x80) {
    cp = c;
    return 1;
  }
  int n = 0;
  char32_t v = 0;
  if ((c & 0xE0) == 0xC0) {
    n = 2;
    v = c & 0x1F;
  } else if ((c & 0xF0) == 0xE0) {
    n = 3;
    v = c & 0x0F;
  } else if ((c & 0xF8) == 0xF0) {
    n = 4;
    v = c & 0x07;
  } else {
    cp = 0xFFFD;
    return 1;
  }
  if (i + static_cast<std::size_t>(n) > s.size()) {
    cp = 0xFFFD;
    return 1;
  }
  for (int k = 1; k < n; ++k) {
    const auto cc = static_cast<unsigned char>(s[i + k]);
    if ((cc & 0xC0) != 0x80) {
      cp = 0xFFFD;
      return 1;
    }
    v = (v << 6) | (cc & 0x3F);
  }
  cp = v;
  return static_cast<std::size_t>(n);
}

std::size_t CodePointWidth(char32_t cp) {
  if (cp == 0) return 0;
  if (cp < 32 || (cp >= 0x7F && cp < 0xA0)) return 0;
  if (IsCombining(cp)) return 0;
  if (IsWide(cp)) return 2;
  return 1;
}

int DisplayWidth(const std::string& text) {
  int width = 0;
  for (std::size_t i = 0; i < text.size();) {
    char32_t cp = 0;
    const std::size_t consumed = Utf8Decode(text, i, cp);
    if (consumed == 0) break;
    width += static_cast<int>(CodePointWidth(cp));
    i += consumed;
  }
  return width;
}

std::string Ellipsize(const std::string& text, int max_width) {
  if (max_width <= 0) return {};
  if (DisplayWidth(text) <= max_width) return text;

  const std::string ellipsis = "…";
  const int ellipsis_width = std::max(1, DisplayWidth(ellipsis));
  const int limit = std::max(0, max_width - ellipsis_width);

  std::string out;
  int width = 0;
  for (std::size_t i = 0; i < text.size();) {
    char32_t cp = 0;
    const std::size_t consumed = Utf8Decode(text, i, cp);
    if (consumed == 0) break;
    const int code_point_width = static_cast<int>(CodePointWidth(cp));
    if (width + code_point_width > limit) break;
    out.append(text, i, consumed);
    width += code_point_width;
    i += consumed;
  }
  out += ellipsis;
  return out;
}

std::size_t Utf8Prev(const std::string& s, std::size_t i) {
  if (i == 0) return 0;
  --i;
  while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) --i;
  return i;
}

std::size_t Utf8Next(const std::string& s, std::size_t i) {
  if (i >= s.size()) return s.size();
  char32_t cp = 0;
  const std::size_t n = Utf8Decode(s, i, cp);
  return i + (n == 0 ? 1 : n);
}

std::vector<std::string> WrapText(const std::string& text, std::size_t width) {
  if (width < 1) width = 1;
  std::vector<std::string> out;
  std::string line;
  std::size_t line_width = 0;

  for (std::size_t i = 0; i < text.size();) {
    if (text[i] == '\n') {
      out.push_back(line);
      line.clear();
      line_width = 0;
      ++i;
      continue;
    }
    char32_t cp = 0;
    const std::size_t n = Utf8Decode(text, i, cp);
    const std::size_t w = CodePointWidth(cp);
    if (line_width + w > width && !line.empty()) {
      out.push_back(line);
      line.clear();
      line_width = 0;
    }
    line.append(text, i, n);
    line_width += w;
    i += n;
  }
  out.push_back(line);
  return out;
}

std::vector<std::string> SplitLines(const std::string& text) {
  std::vector<std::string> out;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) out.push_back(line);
  if (!text.empty() && text.back() == '\n') out.emplace_back();
  return out;
}

std::string Trim(const std::string& s) {
  const auto begin = s.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) return {};
  const auto end = s.find_last_not_of(" \t\r\n");
  return s.substr(begin, end - begin + 1);
}

std::string ToLower(std::string s) {
  for (char& c : s) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return s;
}

std::string DecodeHtmlEntities(const std::string& input) {
  static const std::pair<const char*, const char*> kEntities[] = {
      {"&amp;", "&"},   {"&lt;", "<"},    {"&gt;", ">"},
      {"&quot;", "\""}, {"&#39;", "'"},   {"&#x27;", "'"},
      {"&apos;", "'"},  {"&nbsp;", " "},  {"&mdash;", "\xE2\x80\x94"},
      {"&ndash;", "\xE2\x80\x93"},        {"&hellip;", "\xE2\x80\xA6"},
  };

  std::string out = input;
  for (const auto& [from, to] : kEntities) {
    std::size_t position = 0;
    while ((position = out.find(from, position)) != std::string::npos) {
      out.replace(position, std::char_traits<char>::length(from), to);
      position += std::char_traits<char>::length(to);
    }
  }
  return out;
}

std::string NormalizePlainText(const std::string& input) {
  const auto lines = SplitLines(input);
  std::string out;
  bool pending_blank = false;
  for (const auto& line : lines) {
    const std::string trimmed = Trim(line);
    if (trimmed.empty()) {
      if (!out.empty()) pending_blank = true;
      continue;
    }
    if (!out.empty()) out += pending_blank ? "\n\n" : "\n";
    out += trimmed;
    pending_blank = false;
  }
  return out;
}

std::string StripHtml(const std::string& html) {
  std::string stripped;
  stripped.reserve(html.size());

  for (std::size_t i = 0; i < html.size();) {
    if (html[i] != '<') {
      stripped.push_back(html[i++]);
      continue;
    }

    const std::size_t end = html.find('>', i);
    if (end == std::string::npos) {
      stripped.append(html, i, std::string::npos);
      break;
    }

    std::string tag = ToLower(Trim(html.substr(i + 1, end - i - 1)));
    const bool closing = !tag.empty() && tag.front() == '/';
    if (closing) tag.erase(0, 1);
    const std::size_t attributes = tag.find_first_of(" \t\r\n/");
    if (attributes != std::string::npos) tag.erase(attributes);

    if (tag == "br" || tag == "p" || tag == "div" || tag == "li" ||
        tag == "tr" || tag == "blockquote" || tag == "pre" || tag == "h1" ||
        tag == "h2" || tag == "h3" || tag == "h4" || tag == "h5" ||
        tag == "h6") {
      stripped.push_back('\n');
    }

    i = end + 1;
  }

  return NormalizePlainText(DecodeHtmlEntities(stripped));
}

std::string FormatDateByAccuracy(const std::string& start,
                                 const std::string& accuracy) {
  std::string value = Trim(start);
  if (value.empty()) return value;

  // Normalize an ISO "T" separator so the truncated forms look consistent.
  if (value.size() > 10 && value[10] == 'T') value[10] = ' ';

  std::size_t length = value.size();
  const std::string unit = ToLower(Trim(accuracy));
  if (unit == "year") {
    length = 4;
  } else if (unit == "month") {
    length = 7;
  } else if (unit == "day") {
    length = 10;
  } else if (unit == "hour") {
    length = 13;
  } else if (unit == "minute") {
    length = 16;
  } else if (unit == "second") {
    length = 19;
  }
  if (length < value.size()) value.resize(length);
  return value;
}

std::string UrlEncode(const std::string& s) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  out.reserve(s.size());
  for (unsigned char c : s) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('%');
      out.push_back(hex[c >> 4]);
      out.push_back(hex[c & 0x0F]);
    }
  }
  return out;
}

std::optional<std::string> NormalizeUrl(const std::string& raw) {
  std::string value = Trim(raw);
  if (value.empty()) return std::nullopt;
  if (value.find_first_of(" \t\r\n") != std::string::npos) return std::nullopt;

  const std::string lower = ToLower(value);
  if (lower.rfind("http://", 0) == 0) {
    value.replace(0, 4, "http");
  } else if (lower.rfind("https://", 0) == 0) {
    value.replace(0, 5, "https");
  } else {
    value = "https://" + value;
  }

  // scheme://host[:port][/path][?query][#fragment]; IPv6 hosts in brackets.
  static const std::regex kUrl(
      R"(^https?://(\[[0-9A-Fa-f:.]+\]|[^/?#:]+)(:[0-9]+)?([/?#].*)?$)",
      std::regex::icase);
  if (!std::regex_match(value, kUrl)) return std::nullopt;

  while (!value.empty() && value.back() == '/') value.pop_back();
  return value;
}

std::string HomeDir() {
#ifdef _WIN32
  const char* profile = std::getenv("USERPROFILE");
  if (profile != nullptr && *profile != '\0') return std::string(profile);
  const char* drive = std::getenv("HOMEDRIVE");
  const char* path = std::getenv("HOMEPATH");
  if (drive != nullptr && path != nullptr) {
    return std::string(drive) + path;
  }
  const char* temp = std::getenv("TEMP");
  return temp != nullptr ? std::string(temp) : std::string(".");
#else
  const char* home = std::getenv("HOME");
  return home != nullptr ? std::string(home) : std::string("/tmp");
#endif
}

std::string ConfigDir() {
#ifdef _WIN32
  const char* appdata = std::getenv("APPDATA");
  if (appdata != nullptr && *appdata != '\0') {
    return (std::filesystem::path(appdata) / "spark-tui").string();
  }
  return (std::filesystem::path(HomeDir()) / ".config" / "spark-tui").string();
#else
  const char* xdg = std::getenv("XDG_CONFIG_HOME");
  if (xdg != nullptr && *xdg != '\0') {
    return (std::filesystem::path(xdg) / "spark-tui").string();
  }
  return (std::filesystem::path(HomeDir()) / ".config" / "spark-tui").string();
#endif
}

std::string DefaultConfigPath() {
  return (std::filesystem::path(ConfigDir()) / "config.toml").string();
}

bool EnsureDirectory(const std::string& path) {
  std::error_code ec;
  std::filesystem::create_directories(path, ec);
  return !ec;
}

std::optional<std::string> WriteTempFile(const std::string& prefix,
                                         const std::string& content) {
  const std::filesystem::path directory =
      std::filesystem::temp_directory_path();
  std::string pattern = (directory / (prefix + "-XXXXXX")).string();
  std::vector<char> path(pattern.begin(), pattern.end());
  path.push_back('\0');

#ifdef _WIN32
  if (_mktemp_s(path.data(), path.size()) != 0) return std::nullopt;
  const int fd = _open(path.data(),
                       _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY,
                       _S_IREAD | _S_IWRITE);
#else
  const int fd = mkstemp(path.data());
  if (fd >= 0) chmod(path.data(), S_IRUSR | S_IWUSR);
#endif
  if (fd < 0) return std::nullopt;

  std::size_t offset = 0;
  bool ok = true;
  while (offset < content.size()) {
    const std::size_t remaining = content.size() - offset;
    const std::size_t chunk = std::min<std::size_t>(remaining, 1 << 20);
#ifdef _WIN32
    const int written = _write(fd, content.data() + offset,
                               static_cast<unsigned int>(chunk));
#else
    const ssize_t written = write(fd, content.data() + offset, chunk);
#endif
    if (written <= 0) {
      ok = false;
      break;
    }
    offset += static_cast<std::size_t>(written);
  }
#ifdef _WIN32
  _close(fd);
#else
  close(fd);
#endif
  if (!ok) {
#ifdef _WIN32
    _unlink(path.data());
#else
    unlink(path.data());
#endif
    return std::nullopt;
  }
  return std::string(path.data());
}

std::string ReadTextFile(const std::string& path) {
  std::ifstream in(path);
  if (!in) return {};
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

bool EditFile(const std::string& path) {
  const char* visual = std::getenv("VISUAL");
  const char* editor = std::getenv("EDITOR");
#ifdef _WIN32
  std::string command = visual != nullptr && *visual != '\0'
                            ? visual
                        : editor != nullptr && *editor != '\0' ? editor
                                                               : "notepad";
  std::string quoted = "\"";
  for (char c : path) {
    if (c == '\"') {
      quoted += "\\\"";
    } else {
      quoted += c;
    }
  }
  quoted += "\"";
  command += " " + quoted;
  return std::system(command.c_str()) == 0;
#else
  std::string command = visual != nullptr && *visual != '\0'
                            ? visual
                        : editor != nullptr && *editor != '\0' ? editor
                                                               : "vi";

  auto quote = [](const std::string& value) {
    std::string out = "'";
    for (char c : value) {
      if (c == '\'') {
        out += "'\\''";
      } else {
        out += c;
      }
    }
    out += "'";
    return out;
  };
  command += " " + quote(path);

  const pid_t pid = fork();
  if (pid < 0) return false;
  if (pid == 0) {
    execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
    _exit(127);
  }
  int status = 0;
  waitpid(pid, &status, 0);
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
}

bool OpenUrl(const std::string& url) {
#ifdef _WIN32
  const auto result = ShellExecuteA(nullptr, "open", url.c_str(), nullptr,
                                    nullptr, SW_SHOWNORMAL);
  return reinterpret_cast<std::intptr_t>(result) > 32;
#else
  const pid_t pid = fork();
  if (pid < 0) return false;
  if (pid == 0) {
    execlp("xdg-open", "xdg-open", url.c_str(), static_cast<char*>(nullptr));
    _exit(127);
  }
  int status = 0;
  waitpid(pid, &status, WNOHANG);
  return true;
#endif
}

bool ImportPgpKey(const std::string& key) {
  if (Trim(key).empty()) return false;

  std::string data = key;
  if (data.back() != '\n') data.push_back('\n');
  const auto path = WriteTempFile("spark-tui-pgp", data);
  if (!path) return false;

#ifdef _WIN32
  std::string quoted = "\"";
  for (char c : *path) {
    if (c == '\"') {
      quoted += "\\\"";
    } else {
      quoted += c;
    }
  }
  quoted += "\"";
  const std::string command =
      "gpg --import " + quoted + " >NUL 2>&1";
#else
  std::string quoted = "'";
  for (char c : *path) {
    if (c == '\'') {
      quoted += "'\\''";
    } else {
      quoted += c;
    }
  }
  quoted += "'";
  const std::string command =
      "gpg --import " + quoted + " >/dev/null 2>&1";
#endif
  const int status = std::system(command.c_str());
  std::error_code ec;
  std::filesystem::remove(*path, ec);
  return status == 0;
}

}  // namespace spark
