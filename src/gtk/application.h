#pragma once

#include <adwaita.h>
#include <gtk/gtk.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "api/client.h"
#include "config.h"
#include "forum/client.h"
#include "gtk/page.h"
#include "worker.h"

namespace spark::gtk {

class Application {
 public:
  Application(AdwApplication* app, Config config);
  ~Application();

  Application(const Application&) = delete;
  Application& operator=(const Application&) = delete;

  void Activate();

 private:
  void BuildWindow();
  void BuildPages();
  void InstallActions();
  PageContext MakeContext();
  Page* CurrentPage();
  void ShowPage(const std::string& id);
  void PushDetail(const std::string& title, GtkWidget* content);
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
  static void OnToggleSidebar(GtkButton* button, gpointer data);
  static void OnSplitCollapsed(GObject* object, GParamSpec* spec,
                               gpointer data);
  static void OnLanguageChanged(GObject* object, GParamSpec* spec,
                                gpointer data);
  static void OnRawToggled(GObject* object, GParamSpec* spec, gpointer data);

  AdwApplication* app_ = nullptr;
  Config config_;
  ApiClient api_;
  ForumClient forum_;
  std::vector<std::unique_ptr<Page>> pages_;
  std::unique_ptr<Worker> worker_;
  std::atomic<bool> running_{true};

  GtkWidget* window_ = nullptr;
  GtkWidget* navigation_ = nullptr;
  AdwNavigationPage* root_page_ = nullptr;
  GtkWidget* split_ = nullptr;
  GtkWidget* sidebar_button_ = nullptr;
  GtkWidget* stack_ = nullptr;
  GtkWidget* list_ = nullptr;
  GtkWidget* toast_overlay_ = nullptr;
  std::string current_page_ = "home";
};

}  // namespace spark::gtk
