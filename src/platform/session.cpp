#include "platform/session.h"

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include <cstdlib>
#include <string>

#include "util.h"

namespace spark {
namespace {

const char* Env(const char* name) { return std::getenv(name); }

bool IsTrue(const char* value) {
  return value != nullptr && *value != '\0' && std::string(value) != "0";
}

}  // namespace

UiMode ParseUiMode(const std::string& value) {
  const std::string normalized = ToLower(Trim(value));
  if (normalized == "gtk" || normalized == "gui") return UiMode::Gtk;
  if (normalized == "tui" || normalized == "cli" || normalized == "terminal") {
    return UiMode::Tui;
  }
  return UiMode::Auto;
}

const char* UiModeName(UiMode mode) {
  switch (mode) {
    case UiMode::Tui:
      return "tui";
    case UiMode::Gtk:
      return "gtk";
    default:
      return "auto";
  }
}

bool IsTerminalSession() {
#ifdef _WIN32
  return _isatty(_fileno(stdin)) != 0 || _isatty(_fileno(stdout)) != 0;
#else
  return isatty(STDIN_FILENO) != 0 || isatty(STDOUT_FILENO) != 0;
#endif
}

bool HasGraphicalSession() {
  const char* type = Env("XDG_SESSION_TYPE");
  if (type != nullptr) {
    const std::string normalized = ToLower(type);
    if (normalized == "wayland" || normalized == "x11") return true;
  }
  return IsTrue(Env("WAYLAND_DISPLAY")) || IsTrue(Env("DISPLAY"));
}

UiMode DetectUiMode(UiMode requested) {
  if (requested != UiMode::Auto) return requested;

  if (const char* ui = Env("SPARK_UI")) {
    const UiMode forced = ParseUiMode(ui);
    if (forced != UiMode::Auto) return forced;
  }

  // GIO sets this for applications launched from a .desktop file.
  if (IsTrue(Env("GIO_LAUNCHED_DESKTOP_FILE"))) return UiMode::Gtk;

  if (IsTrue(Env("SSH_CONNECTION")) || IsTrue(Env("SSH_TTY"))) {
    return UiMode::Tui;
  }

  if (IsTerminalSession()) return UiMode::Tui;
  if (HasGraphicalSession()) return UiMode::Gtk;
  return UiMode::Auto;
}

}  // namespace spark
