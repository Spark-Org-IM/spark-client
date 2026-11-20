// spark-client: the frontend dispatcher shipped with sparkapp-core.
//
// It inspects the session, then execs either spark-gui (graphical session) or
// spark-tui (terminal / SSH). When the selected frontend is not installed it
// reports which package is missing, using a dialog in a graphical session and
// stderr everywhere else.

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "options.h"
#include "platform/session.h"

namespace {

constexpr const char* kGuiName = "spark-gui";
constexpr const char* kTuiName = "spark-tui";

std::string SelfDir(const char* argv0) {
#ifdef __linux__
  char buffer[PATH_MAX];
  const ssize_t size = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
  if (size > 0) {
    buffer[size] = '\0';
    const std::string path(buffer);
    const std::string::size_type slash = path.rfind('/');
    if (slash != std::string::npos) return path.substr(0, slash);
  }
#endif
  const std::string arg = argv0 != nullptr ? argv0 : "";
  const std::string::size_type slash = arg.rfind('/');
  if (slash != std::string::npos) return arg.substr(0, slash);
  return {};
}

bool IsExecutable(const std::string& path) {
  struct stat info {};
  if (stat(path.c_str(), &info) != 0) return false;
  return S_ISREG(info.st_mode) && access(path.c_str(), X_OK) == 0;
}

std::string FindInPath(const std::string& name) {
  const char* raw = std::getenv("PATH");
  if (raw == nullptr) return {};
  const std::string path(raw);
  std::string::size_type start = 0;
  while (start <= path.size()) {
    const std::string::size_type end = path.find(':', start);
    const std::string dir =
        path.substr(start, end == std::string::npos ? std::string::npos
                                                    : end - start);
    if (!dir.empty()) {
      const std::string candidate = dir + "/" + name;
      if (IsExecutable(candidate)) return candidate;
    }
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return {};
}

// Prefer the binary next to spark-client, so a locally built tree and a Debian
// installation both work without touching PATH.
std::string ResolveFrontend(const std::string& name, const char* argv0) {
  const std::string dir = SelfDir(argv0);
  if (!dir.empty()) {
    const std::string candidate = dir + "/" + name;
    if (IsExecutable(candidate)) return candidate;
  }
  return FindInPath(name);
}

// Runs a dialog helper and waits for it. Returns true when it actually ran.
bool TryDialog(const char* tool, const std::vector<std::string>& arguments) {
  const std::string path = FindInPath(tool);
  if (path.empty()) return false;

  std::vector<char*> argv;
  argv.push_back(const_cast<char*>(path.c_str()));
  for (const std::string& argument : arguments) {
    argv.push_back(const_cast<char*>(argument.c_str()));
  }
  argv.push_back(nullptr);

  const pid_t pid = fork();
  if (pid < 0) return false;
  if (pid == 0) {
    execv(path.c_str(), argv.data());
    _exit(127);
  }
  int status = 0;
  if (waitpid(pid, &status, 0) < 0) return false;
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

void ShowError(const std::string& message, bool graphical) {
  std::fprintf(stderr, "%s\n", message.c_str());
  if (!graphical) return;
  if (TryDialog("zenity",
                {"--error", "--no-wrap", "--title=Spark ORG",
                 "--text=" + message})) {
    return;
  }
  TryDialog("kdialog", {"--title", "Spark ORG", "--error", message});
}

int RunFrontend(const std::string& path, int argc, char** argv) {
  std::vector<char*> args;
  args.push_back(const_cast<char*>(path.c_str()));
  for (int i = 1; i < argc; ++i) args.push_back(argv[i]);
  args.push_back(nullptr);

  execv(path.c_str(), args.data());
  std::fprintf(stderr, "无法启动 %s: %s\n", path.c_str(), std::strerror(errno));
  return 1;
}

void PrintUsage() {
  std::printf(
      "spark-client — Spark ORG 客户端入口\n"
      "\n"
      "检测当前会话并启动对应前端：\n"
      "  桌面会话 / 非终端启动 → spark-gui\n"
      "  终端 / SSH            → spark-tui\n"
      "\n"
      "选项:\n"
      "  --ui MODE          前端: auto | tui | gtk（默认 auto）\n"
      "  -h, --help         显示帮助\n"
      "  -v, --version      显示版本\n"
      "\n"
      "其余选项（--base-url、--lang、--config 等）原样传给所选前端。\n");
}

}  // namespace

int main(int argc, char** argv) {
  const spark::Options opts = spark::ParseOptions(argc, argv);
  if (opts.showHelp) {
    PrintUsage();
    return 0;
  }
  if (opts.showVersion) {
    std::printf("spark-client %s\n", SPARK_TUI_VERSION);
    return 0;
  }

  const bool graphical = spark::HasGraphicalSession();
  const bool terminal = spark::IsTerminalSession();
  const spark::UiMode requested = spark::ParseUiMode(opts.ui);
  const spark::UiMode detected = spark::DetectUiMode(requested);

  const std::string gui = ResolveFrontend(kGuiName, argv[0]);
  const std::string tui = ResolveFrontend(kTuiName, argv[0]);

  // An explicit --ui / SPARK_UI choice is a promise: never fall back silently.
  if (requested == spark::UiMode::Gtk) {
    if (gui.empty()) {
      ShowError(
          "未找到 spark-gui：请安装 sparkapp-gui 软件包（GTK 前端）。",
          graphical);
      return 1;
    }
    return RunFrontend(gui, argc, argv);
  }
  if (requested == spark::UiMode::Tui) {
    if (tui.empty()) {
      ShowError("未找到 spark-tui：请安装 sparkapp-tui 软件包（终端前端）。",
                graphical);
      return 1;
    }
    return RunFrontend(tui, argc, argv);
  }

  if (detected == spark::UiMode::Gtk) {
    if (!gui.empty()) return RunFrontend(gui, argc, argv);
    if (!tui.empty() && terminal) return RunFrontend(tui, argc, argv);
    ShowError(
        "未找到 spark-gui：请安装 sparkapp-gui 软件包（GTK 前端），"
        "或在终端安装 sparkapp-tui。",
        graphical);
    return 1;
  }

  if (!tui.empty()) return RunFrontend(tui, argc, argv);
  if (!gui.empty() && graphical) return RunFrontend(gui, argc, argv);
  if (!gui.empty() && !terminal) return RunFrontend(gui, argc, argv);
  ShowError("未找到任何前端：请安装 sparkapp-tui 或 sparkapp-gui 软件包。",
            graphical);
  return 1;
}
