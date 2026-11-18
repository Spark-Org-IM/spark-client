#pragma once

#include <adwaita.h>

#include <string>

#include "api/client.h"
#include "config.h"
#include "forum/client.h"

namespace spark::gtk {

class Application {
 public:
  Application(AdwApplication* app, Config config);
  ~Application() = default;

  void Activate();

 private:
  void BuildWindow();
  void InstallActions();
  void ShowPage(const std::string& page);
  void Toast(const std::string& message);
  void ShowSettings();
  void ShowAbout();

  static void OnRowActivated(GtkListBox* box, GtkListBoxRow* row,
                             gpointer data);
  static void OnSettings(GSimpleAction* action, GVariant* parameter,
                         gpointer data);
  static void OnAbout(GSimpleAction* action, GVariant* parameter,
                      gpointer data);
  static void OnQuit(GSimpleAction* action, GVariant* parameter,
                     gpointer data);
  static void OnRefresh(GSimpleAction* action, GVariant* parameter,
                        gpointer data);

  AdwApplication* app_ = nullptr;
  Config config_;
  ApiClient api_;
  ForumClient forum_;
  GtkWidget* window_ = nullptr;
  GtkWidget* stack_ = nullptr;
  GtkWidget* list_ = nullptr;
  GtkWidget* title_label_ = nullptr;
  GtkWidget* toast_overlay_ = nullptr;
};

}  // namespace spark::gtk
