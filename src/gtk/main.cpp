#include <adwaita.h>
#include <gtk/gtk.h>

#include <iostream>
#include <optional>
#include <string>
#include <utility>

#include "config.h"
#include "gtk/application.h"
#include "options.h"
#include "platform/session.h"
#include "util.h"

namespace {

bool ApplyUrl(const std::string& raw, std::optional<std::string>& target,
              std::string& error) {
  const auto normalized = spark::NormalizeUrl(raw);
  if (!normalized) {
    error = "无效地址: " + raw;
    return false;
  }
  target = *normalized;
  return true;
}

bool ApplyOptions(const spark::Options& opts, spark::Config& config,
                  std::string& error) {
  if (!opts.baseUrl.empty() &&
      !ApplyUrl(opts.baseUrl, config.instance.mainOverride, error)) {
    return false;
  }
  if (!opts.statusUrl.empty() &&
      !ApplyUrl(opts.statusUrl, config.instance.statusOverride, error)) {
    return false;
  }
  if (!opts.forumUrl.empty() &&
      !ApplyUrl(opts.forumUrl, config.instance.forumOverride, error)) {
    return false;
  }
  if (!opts.syncUrl.empty()) {
    const auto sync = spark::NormalizeUrl(opts.syncUrl);
    if (!sync) {
      error = "无效同步源地址: " + opts.syncUrl;
      return false;
    }
    config.instance.syncUrl = *sync;
  }
  if (!opts.language.empty()) config.language = opts.language;
  if (opts.insecure) config.insecure = true;
  return true;
}

void OnActivate(GtkApplication*, gpointer data) {
  static_cast<spark::gtk::Application*>(data)->Activate();
}

}  // namespace

int main(int argc, char** argv) {
  spark::Options opts = spark::ParseOptions(argc, argv);
  if (opts.showHelp) {
    std::cout << spark::Usage();
    return 0;
  }
  if (opts.showVersion) {
    std::cout << "spark-gui " SPARK_TUI_VERSION << "\n";
    return 0;
  }

  const spark::UiMode requested = spark::ParseUiMode(opts.ui);
  if (requested == spark::UiMode::Tui) {
    std::cerr << "spark-gui 是 GTK 前端；终端界面请运行 spark-tui\n";
    return 2;
  }
  const spark::UiMode mode =
      spark::DetectUiMode(requested == spark::UiMode::Auto
                              ? spark::UiMode::Gtk
                              : requested);
  if (mode != spark::UiMode::Gtk || !spark::HasGraphicalSession()) {
    std::cerr << "没有可用的图形会话；请在桌面环境运行 spark-gui，"
                 "或在终端运行 spark-tui\n";
    return 1;
  }

  const std::string config_path =
      opts.configPath.empty() ? spark::DefaultConfigPath() : opts.configPath;
  spark::Config config = spark::Config::Load(config_path);
  std::string error;
  if (!ApplyOptions(opts, config, error)) {
    std::cerr << error << "\n";
    return 2;
  }

  AdwApplication* app = adw_application_new("org.sparkorg.Spark",
                                            G_APPLICATION_DEFAULT_FLAGS);
  spark::gtk::Application application(app, std::move(config));
  g_signal_connect(app, "activate", G_CALLBACK(OnActivate), &application);
  const int status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  return status;
}
