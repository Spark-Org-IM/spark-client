#pragma once

#include <adwaita.h>
#include <gtk/gtk.h>

#include <string>
#include <vector>

#include "gtk/page.h"

namespace spark::gtk {

// Generic paged list for news, contacts and platform entries. Rows open the
// shared item detail page.
class ListPage : public Page {
 public:
  ListPage(std::string id, std::string kind, std::string title,
           std::string icon, PageContext ctx);

  GtkWidget* Widget() override;
  std::string Title() const override { return title_; }
  const char* Id() const override { return id_.c_str(); }

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

  std::string id_;
  std::string kind_;
  std::string title_;
  std::string icon_;
  PageContext ctx_;

  GtkWidget* root_ = nullptr;
  GtkWidget* status_ = nullptr;
  GtkWidget* list_ = nullptr;
  GtkWidget* spinner_ = nullptr;
  GtkWidget* count_label_ = nullptr;
  GtkWidget* load_more_ = nullptr;
  std::vector<ListItem> items_;
  int generation_ = 0;
  int total_ = 0;
  bool loading_ = false;
  bool loaded_ = false;
  bool has_more_ = false;
};

}  // namespace spark::gtk
