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
#include "plugin/host.h"
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
  void ShowTopBarSettings();

  struct TopBarItem {
    enum class Kind { Page, PluginAction };
    std::string id;
    std::string title;
    std::string subtitle;
    Kind kind = Kind::Page;
    std::string pageId;
    int action = -1;  // index into pluginActions_
    bool visible = true;
    bool collapsed = false;
  };

  void RebuildTopBarItems();
  void SaveTopBarPrefs();
  void RebuildSidebarLists();
  void FillTopBarSettingsList();
  void ShowSidebarCollapsed(bool collapsed);
  void UpdateCollapseLabel();
  void SelectSidebarPage(const std::string& pageId);
  void ActivateTopBarId(const std::string& id);
  bool PageIsVisible(const std::string& pageId) const;
  std::string FirstVisiblePage() const;
  GtkWidget* MakeTopBarRow(const TopBarItem& item);
  GtkWidget* MakeCollapseRow();

  static void OnRowActivated(GtkListBox* box, GtkListBoxRow* row,
                             gpointer data);
  static void OnCollapsedRowActivated(GtkListBox* box, GtkListBoxRow* row,
                                      gpointer data);
  static void OnCollapseRowActivated(GtkListBox* box, GtkListBoxRow* row,
                                     gpointer data);
  static void OnSidebarBack(GtkButton* button, gpointer data);
  static void OnTopBarSettings(GtkWidget* row, gpointer data);
  static void OnTopBarVisible(GObject* object, GParamSpec* spec,
                              gpointer data);
  static void OnTopBarCollapsed(GObject* object, GParamSpec* spec,
                                gpointer data);
  static void OnTopBarMove(GtkButton* button, gpointer data);
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
  PluginHost plugins_;
  std::vector<std::unique_ptr<Page>> pages_;
  std::vector<PluginAction> pluginActions_;
  std::vector<TopBarItem> topbar_;
  std::unique_ptr<Worker> worker_;
  std::atomic<bool> running_{true};

  GtkWidget* window_ = nullptr;
  GtkWidget* navigation_ = nullptr;
  AdwNavigationPage* root_page_ = nullptr;
  GtkWidget* split_ = nullptr;
  GtkWidget* sidebar_button_ = nullptr;
  GtkWidget* stack_ = nullptr;
  GtkWidget* sidebar_stack_ = nullptr;
  GtkWidget* sidebar_main_list_ = nullptr;
  GtkWidget* sidebar_collapsed_list_ = nullptr;
  GtkWidget* sidebar_collapsed_page_ = nullptr;
  GtkWidget* sidebar_collapse_row_ = nullptr;
  GtkWidget* sidebar_collapse_label_ = nullptr;
  GtkWidget* topbar_settings_list_ = nullptr;
  GtkWidget* settings_window_ = nullptr;
  GtkWidget* toast_overlay_ = nullptr;
  bool sidebar_collapsed_ = false;
  std::string last_collapsed_id_;
  std::string current_page_ = "home";
};

}  // namespace spark::gtk
