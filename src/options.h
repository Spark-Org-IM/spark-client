#pragma once

#include <string>

namespace spark {

struct Options {
  std::string baseUrl;   // manual main-server override (--base-url)
  std::string syncUrl;   // service-source document
  std::string statusUrl; // manual status-server override
  std::string forumUrl;  // manual Flarum forum override
  std::string language;
  std::string configPath;
  bool insecure = false;
  bool noSync = false;
  bool showHelp = false;
  bool showVersion = false;
};

Options ParseOptions(int argc, char** argv);
std::string Usage();

}  // namespace spark
