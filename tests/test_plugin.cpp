#include <cstdio>
#include <cstdlib>
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

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <plugin-directory>\n", argv[0]);
    return 2;
  }

  spark::Config config;
  config.token.plaintext = "secret-account-token";
  config.forum.token = "secret-forum-token";

  spark::ApiClient api(config.EffectiveMain(), false, config.token.plaintext);
  spark::ForumClient forum(config.EffectiveForum(), false, config.forum.token);
  spark::PluginHost host(&api, &forum, &config);

  if (host.LoadDirectory(argv[1]) != 1) {
    return Fail("expected exactly one plugin to load");
  }
  if (host.Plugins().size() != 1 ||
      host.Plugins().front().id != "news-admin") {
    return Fail("unexpected plugin metadata");
  }

  const auto actions = host.Actions();
  if (actions.size() != 4) return Fail("expected four actions");
  bool hasSave = false;
  for (const auto& action : actions) {
    if (action.id == "save-news") {
      hasSave = true;
      if (action.pluginId != "news-admin") return Fail("wrong action owner");
      if (action.fields.size() != 2) return Fail("wrong save-news fields");
    }
  }
  if (!hasSave) return Fail("save-news action missing");

  nlohmann::json result;
  std::string error;
  if (host.Run("news-admin", "no-such-action", nlohmann::json::object(),
               result, error)) {
    return Fail("unknown action unexpectedly succeeded");
  }
  if (host.Run("missing-plugin", "save-news", nlohmann::json::object(),
               result, error)) {
    return Fail("unknown plugin unexpectedly succeeded");
  }

  // Credentials must never be part of the settings handed to a plugin.
  const std::string settings = spark::SanitizedSettings(config).dump();
  if (settings.find("secret") != std::string::npos ||
      settings.find("token") != std::string::npos) {
    return Fail("sanitized settings leaked credentials");
  }

#ifndef _WIN32
  // $SPARK_PLUGIN_PATH is a path list; duplicates must collapse.
  setenv("SPARK_PLUGIN_PATH", "/tmp/spark-a:/tmp/spark-a:/tmp/spark-b", 1);
  const auto paths = spark::PluginSearchPaths();
  if (paths.size() < 2 || paths[0] != "/tmp/spark-a" ||
      paths[1] != "/tmp/spark-b") {
    return Fail("SPARK_PLUGIN_PATH was not parsed correctly");
  }
#endif

  std::printf("plugin tests passed\n");
  return 0;
}
