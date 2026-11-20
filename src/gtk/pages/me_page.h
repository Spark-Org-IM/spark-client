#pragma once

#include <adwaita.h>
#include <gtk/gtk.h>

#include <string>

#include "gtk/page.h"

namespace spark::gtk {

// Account page: shows /api/v2/me.json for the stored token and the linked
// forum account. Signing in is still done through the TUI for now.
class MePage : public Page {
 public:
  explicit MePage(PageContext ctx);

  GtkWidget* Widget() override;
  std::string Title() const override { return T(ctx_, "我的", "Me"); }
  const char* Id() const override { return "me"; }

  void OnShown() override;
  void Refresh() override;

 private:
  void Build();
  void Load();
  void ShowStatus(const std::string& icon, const std::string& title,
                  const std::string& description, GtkWidget* child = nullptr);
  void ShowError(const std::string& message);

  static void OnRetry(GtkButton* button, gpointer data);

  PageContext ctx_;
  GtkWidget* root_ = nullptr;
  GtkWidget* status_ = nullptr;
  int generation_ = 0;
  bool loading_ = false;
  bool loaded_ = false;
};

}  // namespace spark::gtk
