#pragma once

#include <gtk/gtk.h>

#include <functional>
#include <string>

#include "api/client.h"
#include "config.h"
#include "forum/client.h"
#include "gtk/async.h"
#include "i18n.h"
#include "worker.h"

namespace spark {
class PluginHost;
}

namespace spark::gtk {

using ToastFn = std::function<void(const std::string& message)>;
// Pushes a detail page onto the window-level navigation stack. The callee
// takes ownership of `content`.
using PushDetailFn =
    std::function<void(const std::string& title, GtkWidget* content)>;
// Refreshes the pages affected by a plugin action ("news", "forum", "me").
using RefreshFn = std::function<void(const std::string& target)>;

// Everything a page needs to talk to the shared core and to the window. The
// objects pointed to are owned by Application and outlive every page.
struct PageContext {
  ApiClient* api = nullptr;
  ForumClient* forum = nullptr;
  Config* config = nullptr;
  Worker* worker = nullptr;
  PluginHost* plugins = nullptr;
  std::string lang = "zh-cn";
  ToastFn toast;
  PushDetailFn push_detail;
  RefreshFn refresh;
};

inline std::string UiLanguage(const Config& config) {
  if (config.language.empty() || config.language == "system") {
    return SystemLanguageTag();
  }
  return config.language;
}

inline std::string T(const PageContext& ctx, const char* zh, const char* en) {
  return Tr(zh, en, ctx.lang);
}

// A top-level page shown in the content stack. Pages build their widget lazily
// and fetch data the first time they are shown.
class Page {
 public:
  virtual ~Page() = default;

  virtual GtkWidget* Widget() = 0;
  virtual std::string Title() const = 0;
  virtual const char* Id() const = 0;

  // Called when the page becomes visible. Implementations load data here at
  // most once and then keep it cached.
  virtual void OnShown() {}
  // Explicit user refresh; always reloads.
  virtual void Refresh() { OnShown(); }
};

}  // namespace spark::gtk
