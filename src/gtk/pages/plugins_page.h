#pragma once

#include <adwaita.h>
#include <gtk/gtk.h>

#include <string>
#include <vector>

#include "gtk/page.h"
#include "plugin/host.h"

namespace spark::gtk {

// Optional plugin actions contributed by installed shared objects. The page is
// only added when the current token can use at least one action, so a normal
// install without plugins keeps the same sidebar.
class PluginsPage : public Page {
 public:
  explicit PluginsPage(PageContext ctx);

  GtkWidget* Widget() override;
  std::string Title() const override { return title_; }
  const char* Id() const override { return "plugins"; }

 private:
  void Build();
  void ShowAction(const PluginAction& action);

  static void OnActionActivated(AdwActionRow* row, gpointer data);

  PageContext ctx_;
  std::string title_;
  std::vector<PluginAction> actions_;
  GtkWidget* root_ = nullptr;
};

// True when at least one action is visible for the token's scopes.
bool HasVisiblePluginActions(const PageContext& ctx);

}  // namespace spark::gtk
