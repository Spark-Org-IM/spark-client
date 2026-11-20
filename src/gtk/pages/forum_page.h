#pragma once

#include <adwaita.h>
#include <gtk/gtk.h>

#include <string>
#include <vector>

#include "forum/models.h"
#include "gtk/page.h"

namespace spark::gtk {

// Read-only Flarum discussion list. Signing in still happens in the TUI; the
// stored forum token is shared through the config file.
class ForumPage : public Page {
 public:
  explicit ForumPage(PageContext ctx);

  GtkWidget* Widget() override;
  std::string Title() const override;
  const char* Id() const override { return "forum"; }

  void OnShown() override;
  void Refresh() override;

 private:
  void Build();
  void Load(bool append);
  void Render();
  void ShowStatus(const std::string& icon, const std::string& title,
                  const std::string& description, GtkWidget* child = nullptr);
  void ShowError(const std::string& message);

  static void OnRowActivated(AdwActionRow* row, gpointer data);
  static void OnRetry(GtkButton* button, gpointer data);
  static void OnLoadMore(GtkButton* button, gpointer data);

  PageContext ctx_;
  GtkWidget* root_ = nullptr;
  GtkWidget* status_ = nullptr;
  GtkWidget* list_ = nullptr;
  GtkWidget* spinner_ = nullptr;
  GtkWidget* count_label_ = nullptr;
  GtkWidget* load_more_ = nullptr;
  std::vector<ForumDiscussion> items_;
  std::string next_;
  int generation_ = 0;
  bool loading_ = false;
  bool loaded_ = false;
  bool has_more_ = false;
};

}  // namespace spark::gtk
