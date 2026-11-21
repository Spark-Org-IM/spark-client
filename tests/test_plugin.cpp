#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "api/client.h"
#include "config.h"
#include "forum/client.h"
#include "plugin/host.h"

namespace {

int Fail(const char* message) {
  std::fprintf(stderr, "plugin test failed: %s\n", message);
  return 1;
}

struct TempDir {
  std::filesystem::path path;

  TempDir() {
    path = std::filesystem::temp_directory_path() /
           ("spark-plugin-test-" + std::to_string(::getpid()));
    std::error_code error;
    std::filesystem::remove_all(path, error);
    std::filesystem::create_directories(path, error);
  }

  ~TempDir() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

}  // namespace

// The host only loads plugins; this test therefore never assumes that one was
// built together with it. Real plugin modules are tested in their own project.
int main() {
  spark::Config config;
  config.token.plaintext = "secret-account-token";
  config.forum.token = "secret-forum-token";

  spark::ApiClient api(config.EffectiveMain(), false, config.token.plaintext);
  spark::ForumClient forum(config.EffectiveForum(), false, config.forum.token);
  spark::PluginHost host(&api, &forum, &config);

  // A missing or empty directory is a normal installation without plugins.
  if (host.LoadDirectory("/nonexistent/spark-plugin-directory") != 0) {
    return Fail("a missing plugin directory must not load anything");
  }
  if (!host.Plugins().empty() || !host.Actions().empty()) {
    return Fail("missing plugin directory produced entries");
  }

  // Broken modules are skipped instead of aborting startup.
  {
    const TempDir tmp;
    {
      std::ofstream broken(tmp.path / "broken.so");
      broken << "this is not a shared object";
    }
    {
      std::ofstream ignored(tmp.path / "readme.txt");
      ignored << "not a plugin";
    }
    if (host.LoadDirectory(tmp.path.string()) != 0) {
      return Fail("a broken plugin was loaded");
    }
    if (!host.Plugins().empty()) {
      return Fail("a broken plugin was registered");
    }
  }

  // Credentials must never be part of the settings handed to a plugin.
  const std::string settings = spark::SanitizedSettings(config).dump();
  if (settings.find("secret") != std::string::npos ||
      settings.find("token") != std::string::npos) {
    return Fail("sanitized settings leaked credentials");
  }

  // Action scopes gate the UI. An empty token scope list means the token
  // predates scope storage, so the server makes the final decision.
  {
    spark::PluginAction action;
    action.scopes = {"news.edit"};
    const std::vector<std::string> none;
    const std::vector<std::string> wildcard = {"*"};
    const std::vector<std::string> allowed = {"news.edit", "profile.read"};
    const std::vector<std::string> denied = {"news.read"};
    if (!spark::PluginScopesAllow(action, none)) {
      return Fail("empty token scopes must not hide actions");
    }
    if (!spark::PluginScopesAllow(action, wildcard)) {
      return Fail("wildcard token scope must allow actions");
    }
    if (!spark::PluginScopesAllow(action, allowed)) {
      return Fail("matching token scope must allow actions");
    }
    if (spark::PluginScopesAllow(action, denied)) {
      return Fail("missing token scope must hide actions");
    }
  }

  // $SPARK_PLUGIN_PATH is a path list; duplicates must collapse.
  setenv("SPARK_PLUGIN_PATH", "/tmp/spark-a:/tmp/spark-a:/tmp/spark-b", 1);
  const auto paths = spark::PluginSearchPaths();
  if (paths.size() < 2 || paths[0] != "/tmp/spark-a" ||
      paths[1] != "/tmp/spark-b") {
    return Fail("SPARK_PLUGIN_PATH was not parsed correctly");
  }

  std::printf("plugin tests passed\n");
  return 0;
}
