#pragma once

#include <adwaita.h>
#include <gtk/gtk.h>

#include <string>
#include <vector>

#include "gtk/page.h"

namespace spark::gtk {

// Home: the site's latest news picks. Mirrors the TUI home page, but rows open
// a full detail page instead of an inline panel.
class HomePage : public Page {
 public:
  explicit HomePage(PageContext ctx);

  GtkWidget* Widget() override;
  std::string Title() const override { return title_; }
  const char* Id() const override { return "home"; }

  void OnShown() override;
  void Refresh() override;

 private:
  void Build();
  void Load();
  void Render();
  void ShowStatus(const std::string& icon, const std::string& title,
                  const std::string& description, GtkWidget* child = nullptr);
  void ShowError(const std::string& message);

  static void OnRowActivated(AdwActionRow* row, gpointer data);
  static void OnRetry(GtkButton* button, gpointer data);

  PageContext ctx_;
  std::string title_ = "Spark ORG";
  GtkWidget* root_ = nullptr;
  GtkWidget* stack_ = nullptr;
  GtkWidget* status_ = nullptr;
  GtkWidget* list_ = nullptr;
  GtkWidget* spinner_ = nullptr;
  std::vector<ListItem> items_;
  int generation_ = 0;
  bool loading_ = false;
  bool loaded_ = false;
};

}  // namespace spark::gtk
