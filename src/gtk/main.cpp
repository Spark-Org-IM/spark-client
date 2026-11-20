#include <adwaita.h>
#include <gtk/gtk.h>

#include <iostream>
#include <optional>
#include <string>
#include <utility>

#include "config.h"
#include "gtk/application.h"
#include "options.h"
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
  int status = 0;
  {
    // The Application owns the worker pool and every page. Destroy it before
    // dropping the GApplication so widgets are torn down in a known order.
    spark::gtk::Application application(app, std::move(config));
    g_signal_connect(app, "activate", G_CALLBACK(OnActivate), &application);
    // Our options are already applied above; GTK would reject them as
    // unknown, so hand the application only the program name.
    status = g_application_run(G_APPLICATION(app), 1, argv);
    g_signal_handlers_disconnect_by_data(app, &application);
  }
  g_object_unref(app);
  return status;
}
