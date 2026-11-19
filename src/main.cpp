#include <iostream>
#include <optional>
#include <string>
#include <utility>

#include "config.h"
#include "options.h"
#include "platform/session.h"
#include "ui/app.h"
#include "util.h"

namespace {

bool ApplyUrl(const std::string& raw, std::optional<std::string>& target,
              const char* label) {
  const auto normalized = spark::NormalizeUrl(raw);
  if (!normalized) {
    std::cerr << "无效的" << label << ": " << raw << "\n";
    return false;
  }
  target = *normalized;
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  spark::Options opts = spark::ParseOptions(argc, argv);

  if (opts.showHelp) {
    std::cout << spark::Usage();
    return 0;
  }
  if (opts.showVersion) {
    std::cout << "spark-tui " << SPARK_TUI_VERSION << "\n";
    return 0;
  }

  const std::string path =
      opts.configPath.empty() ? spark::DefaultConfigPath() : opts.configPath;

  spark::Config cfg = spark::Config::Load(path);

  if (!opts.baseUrl.empty() &&
      !ApplyUrl(opts.baseUrl, cfg.instance.mainOverride, "主服务器地址")) {
    return 2;
  }
  if (!opts.statusUrl.empty() &&
      !ApplyUrl(opts.statusUrl, cfg.instance.statusOverride, "状态服务器地址")) {
    return 2;
  }
  if (!opts.forumUrl.empty() &&
      !ApplyUrl(opts.forumUrl, cfg.instance.forumOverride, "论坛服务器地址")) {
    return 2;
  }
  if (!opts.syncUrl.empty()) {
    const auto sync = spark::NormalizeUrl(opts.syncUrl);
    if (!sync) {
      std::cerr << "无效的服务源地址: " << opts.syncUrl << "\n";
      return 2;
    }
    cfg.instance.syncUrl = *sync;
  }
  if (!opts.language.empty()) cfg.language = opts.language;
  if (opts.insecure) cfg.insecure = true;

  if (spark::ParseUiMode(opts.ui) == spark::UiMode::Gtk) {
    std::cerr << "GTK 前端请运行 spark-gui 或 spark-client\n";
    return 2;
  }

  spark::App app(std::move(opts), std::move(cfg));
  return app.Run();
}
