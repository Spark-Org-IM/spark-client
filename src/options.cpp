#include "options.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace spark {
namespace {

std::string Env(const char* name) {
  const char* value = std::getenv(name);
  return value != nullptr ? std::string(value) : std::string();
}

bool EnvBool(const char* name) {
  const std::string value = Env(name);
  return value == "1" || value == "true" || value == "yes" || value == "on";
}

}  // namespace

Options ParseOptions(int argc, char** argv) {
  Options opts;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto next = [&](const char* name) -> std::string {
      if (i + 1 >= argc) {
        std::cerr << "缺少参数: " << name << "\n";
        opts.showHelp = true;
        return {};
      }
      return argv[++i];
    };

    if (arg == "-h" || arg == "--help") {
      opts.showHelp = true;
    } else if (arg == "-v" || arg == "--version") {
      opts.showVersion = true;
    } else if (arg == "-u" || arg == "--base-url" || arg == "--main-url") {
      opts.baseUrl = next("--base-url");
    } else if (arg.rfind("--base-url=", 0) == 0) {
      opts.baseUrl = arg.substr(std::string("--base-url=").size());
    } else if (arg.rfind("--main-url=", 0) == 0) {
      opts.baseUrl = arg.substr(std::string("--main-url=").size());
    } else if (arg == "--sync-url") {
      opts.syncUrl = next("--sync-url");
    } else if (arg.rfind("--sync-url=", 0) == 0) {
      opts.syncUrl = arg.substr(std::string("--sync-url=").size());
    } else if (arg == "--status-url") {
      opts.statusUrl = next("--status-url");
    } else if (arg.rfind("--status-url=", 0) == 0) {
      opts.statusUrl = arg.substr(std::string("--status-url=").size());
    } else if (arg == "--forum-url") {
      opts.forumUrl = next("--forum-url");
    } else if (arg.rfind("--forum-url=", 0) == 0) {
      opts.forumUrl = arg.substr(std::string("--forum-url=").size());
    } else if (arg == "-l" || arg == "--lang") {
      opts.language = next("--lang");
    } else if (arg.rfind("--lang=", 0) == 0) {
      opts.language = arg.substr(std::string("--lang=").size());
    } else if (arg == "--config") {
      opts.configPath = next("--config");
    } else if (arg.rfind("--config=", 0) == 0) {
      opts.configPath = arg.substr(std::string("--config=").size());
    } else if (arg == "--ui") {
      opts.ui = next("--ui");
    } else if (arg.rfind("--ui=", 0) == 0) {
      opts.ui = arg.substr(std::string("--ui=").size());
    } else if (arg == "-k" || arg == "--insecure") {
      opts.insecure = true;
    } else if (arg == "--no-sync") {
      opts.noSync = true;
    } else {
      std::cerr << "未知参数: " << arg << "（使用 --help 查看帮助）\n";
    }
  }

  if (opts.baseUrl.empty()) opts.baseUrl = Env("SPARK_TUI_BASE_URL");
  if (opts.syncUrl.empty()) opts.syncUrl = Env("SPARK_TUI_SYNC_URL");
  if (opts.statusUrl.empty()) opts.statusUrl = Env("SPARK_TUI_STATUS_URL");
  if (opts.forumUrl.empty()) opts.forumUrl = Env("SPARK_TUI_FORUM_URL");
  if (opts.language.empty()) opts.language = Env("SPARK_TUI_LANG");
  if (opts.configPath.empty()) opts.configPath = Env("SPARK_TUI_CONFIG");
  if (opts.ui.empty()) opts.ui = Env("SPARK_UI");
  if (!opts.insecure) opts.insecure = EnvBool("SPARK_TUI_INSECURE");
  if (!opts.noSync) opts.noSync = EnvBool("SPARK_TUI_NO_SYNC");

  return opts;
}

std::string Usage() {
  return R"(spark-tui — Spark ORG API v2 终端客户端

用法:
  spark-tui [选项]

实例地址:
  -u, --base-url URL     手动指定主服务器（等同 --main-url）
      --sync-url URL     服务源 services.json
                         （默认: https://spark-org-im.github.io/api/services.json）
      --status-url URL   手动指定状态服务器
      --forum-url URL    手动指定论坛服务器（Flarum 实例）
      --no-sync          启动时不自动同步服务源
      地址优先级: 手动覆盖 > 上次同步值 > 内置默认

其它选项:
  -l, --lang LANG        语言: system | zh-cn | en-us
  -k, --insecure         跳过 TLS 证书校验（自签名开发环境）
      --config PATH      配置文件（默认: ~/.config/spark-tui/config.toml）
      --ui MODE          前端: auto | tui | gtk（默认 auto）
  -h, --help             显示帮助
  -v, --version          显示版本

环境变量:
  SPARK_TUI_BASE_URL, SPARK_TUI_SYNC_URL, SPARK_TUI_STATUS_URL,
  SPARK_TUI_FORUM_URL, SPARK_TUI_LANG, SPARK_TUI_CONFIG, SPARK_UI,
  SPARK_TUI_INSECURE, SPARK_TUI_NO_SYNC

本机开发:
  spark-tui -u https://127.0.0.1:8096 -k --no-sync

快捷键:
  1-7 切换标签   ↑↓/jk 选择   Enter 详情   n 加载更多   r 刷新
  / 搜索   l 临时切换语言   s 设置   L 登录菜单   O 登出   o 打开链接
  m 管理   a 新建新闻 / 回复   c 发新主题   i 导入 PGP 公钥   q 退出
)";
}

}  // namespace spark
