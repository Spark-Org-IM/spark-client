#include "gtk/application.h"

#include <gdk/gdk.h>
#include <gtk/gtk.h>

#include <array>
#include <string>
#include <utility>
#include <vector>

#include "i18n.h"
#include "util.h"

namespace spark::gtk {
namespace {

struct PageSpec {
  const char* id;
  const char* title;
  const char* icon;
  const char* description;
};

constexpr std::array<PageSpec, 7> kPages{{
    {"home", "主页", "go-home-symbolic", "Spark ORG 主页与置顶内容将在这里显示。"},
    {"news", "新闻", "newspaper-symbolic", "新闻列表和详情页面将在这里接入。"},
    {"contact", "联系", "system-users-symbolic", "联系人详情与 PGP 导入将在这里接入。"},
    {"platform", "平台", "network-workgroup-symbolic", "平台列表将在这里接入。"},
    {"forum", "论坛", "dialog-question-symbolic", "Flarum 论坛列表和详情将在这里接入。"},
    {"me", "我的", "avatar-default-symbolic", "账号、令牌和设置入口将在这里接入。"},
    {"settings", "设置", "preferences-system-symbolic", "服务源、外观和语言设置将在这里接入。"},
}};

void AddCss() {
  GtkCssProvider* provider = gtk_css_provider_new();
  gtk_css_provider_load_from_string(
      provider,
      ".sidebar-footer { padding: 12px 16px; }"
      ".sidebar-footer label { font-size: 0.85em; }"
      ".placeholder-icon { opacity: 0.7; }");
  gtk_style_context_add_provider_for_display(
      gdk_display_get_default(), GTK_STYLE_PROVIDER(provider),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(provider);
}

GtkWidget* MakeSidebarRow(const PageSpec& page) {
  GtkWidget* row = gtk_list_box_row_new();
  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_widget_set_margin_top(box, 8);
  gtk_widget_set_margin_bottom(box, 8);
  gtk_widget_set_margin_start(box, 12);
  gtk_widget_set_margin_end(box, 12);

  GtkWidget* icon = gtk_image_new_from_icon_name(page.icon);
  GtkWidget* label = gtk_label_new(page.title);
  gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
  gtk_widget_set_hexpand(label, TRUE);
  gtk_box_append(GTK_BOX(box), icon);
  gtk_box_append(GTK_BOX(box), label);
  gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
  g_object_set_data_full(G_OBJECT(row), "page", g_strdup(page.id), g_free);
  g_object_set_data_full(G_OBJECT(row), "page-title", g_strdup(page.title),
                         g_free);
  return row;
}

GtkWidget* MakePlaceholder(const PageSpec& page) {
  GtkWidget* status = adw_status_page_new();
  adw_status_page_set_icon_name(ADW_STATUS_PAGE(status), page.icon);
  adw_status_page_set_title(ADW_STATUS_PAGE(status), page.title);
  adw_status_page_set_description(ADW_STATUS_PAGE(status), page.description);
  return status;
}

}  // namespace

Application::Application(AdwApplication* app, Config config)
    : app_(app),
      config_(std::move(config)),
      api_(config_.EffectiveMain(), config_.insecure, config_.token.plaintext),
      forum_(config_.EffectiveForum(), config_.insecure,
             config_.forum.token) {}

void Application::Activate() {
  if (window_ == nullptr) BuildWindow();
  gtk_window_present(GTK_WINDOW(window_));
}

void Application::BuildWindow() {
  AdwStyleManager* style = adw_style_manager_get_default();
  adw_style_manager_set_color_scheme(style, ADW_COLOR_SCHEME_DEFAULT);
  AddCss();

  window_ = adw_application_window_new(GTK_APPLICATION(app_));
  gtk_window_set_title(GTK_WINDOW(window_), "Spark ORG");
  gtk_window_set_icon_name(GTK_WINDOW(window_), "spark-tui");
  gtk_window_set_default_size(GTK_WINDOW(window_), 1100, 760);

  GtkWidget* header = adw_header_bar_new();
  title_label_ = gtk_label_new("主页");
  gtk_widget_add_css_class(title_label_, "title-4");
  adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), title_label_);

  GMenu* menu = g_menu_new();
  g_menu_append(menu, "设置", "app.settings");
  g_menu_append(menu, "关于 Spark ORG", "app.about");
  g_menu_append(menu, "退出", "app.quit");
  GtkWidget* menu_button = gtk_menu_button_new();
  gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menu_button),
                                "open-menu-symbolic");
  gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menu_button),
                                 G_MENU_MODEL(menu));
  adw_header_bar_pack_end(ADW_HEADER_BAR(header), menu_button);
  g_object_unref(menu);

  GtkWidget* sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_add_css_class(sidebar, "navigation-sidebar");
  list_ = gtk_list_box_new();
  gtk_list_box_set_selection_mode(GTK_LIST_BOX(list_),
                                  GTK_SELECTION_SINGLE);
  gtk_list_box_set_show_separators(GTK_LIST_BOX(list_), FALSE);
  gtk_widget_set_vexpand(list_, TRUE);
  for (const PageSpec& page : kPages) {
    gtk_list_box_append(GTK_LIST_BOX(list_), MakeSidebarRow(page));
  }
  g_signal_connect(list_, "row-activated",
                   G_CALLBACK(&Application::OnRowActivated), this);
  gtk_box_append(GTK_BOX(sidebar), list_);

  GtkWidget* footer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_add_css_class(footer, "sidebar-footer");
  GtkWidget* footer_label = gtk_label_new("Spark ORG " SPARK_TUI_VERSION);
  gtk_label_set_xalign(GTK_LABEL(footer_label), 0.0f);
  gtk_widget_add_css_class(footer_label, "dim-label");
  gtk_box_append(GTK_BOX(footer), footer_label);
  gtk_box_append(GTK_BOX(sidebar), footer);

  stack_ = gtk_stack_new();
  gtk_stack_set_transition_type(GTK_STACK(stack_),
                                GTK_STACK_TRANSITION_TYPE_CROSSFADE);
  for (const PageSpec& page : kPages) {
    gtk_stack_add_titled(GTK_STACK(stack_), MakePlaceholder(page), page.id,
                         page.title);
  }

  GtkWidget* split = adw_navigation_split_view_new();
  adw_navigation_split_view_set_sidebar(
      ADW_NAVIGATION_SPLIT_VIEW(split),
      adw_navigation_page_new(sidebar, "Spark ORG"));
  adw_navigation_split_view_set_content(
      ADW_NAVIGATION_SPLIT_VIEW(split),
      adw_navigation_page_new(stack_, "Spark ORG"));

  GtkWidget* toolbar = adw_toolbar_view_new();
  adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), header);
  adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), split);

  toast_overlay_ = adw_toast_overlay_new();
  adw_toast_overlay_set_child(ADW_TOAST_OVERLAY(toast_overlay_), toolbar);
  adw_application_window_set_content(ADW_APPLICATION_WINDOW(window_),
                                     toast_overlay_);

  GtkListBoxRow* first = gtk_list_box_get_row_at_index(GTK_LIST_BOX(list_), 0);
  if (first != nullptr) {
    gtk_list_box_select_row(GTK_LIST_BOX(list_), first);
  }
  ShowPage("home");
  InstallActions();
}

void Application::InstallActions() {
  static const GActionEntry entries[] = {
      {"settings", &Application::OnSettings, nullptr, nullptr, nullptr},
      {"about", &Application::OnAbout, nullptr, nullptr, nullptr},
      {"quit", &Application::OnQuit, nullptr, nullptr, nullptr},
      {"refresh", &Application::OnRefresh, nullptr, nullptr, nullptr},
  };
  g_action_map_add_action_entries(G_ACTION_MAP(app_), entries,
                                  G_N_ELEMENTS(entries), this);
}

void Application::ShowPage(const std::string& page) {
  if (stack_ == nullptr) return;
  gtk_stack_set_visible_child_name(GTK_STACK(stack_), page.c_str());
}

void Application::Toast(const std::string& message) {
  if (toast_overlay_ == nullptr) return;
  AdwToast* toast = adw_toast_new(message.c_str());
  adw_toast_overlay_add_toast(ADW_TOAST_OVERLAY(toast_overlay_), toast);
}

void Application::ShowSettings() {
  GtkWidget* preferences = adw_preferences_window_new();
  gtk_window_set_title(GTK_WINDOW(preferences), "设置");
  gtk_window_set_transient_for(GTK_WINDOW(preferences), GTK_WINDOW(window_));
  gtk_window_set_default_size(GTK_WINDOW(preferences), 640, 520);

  GtkWidget* page = adw_preferences_page_new();
  GtkWidget* group = adw_preferences_group_new();
  adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(group), "外观");
  adw_preferences_group_set_description(
      ADW_PREFERENCES_GROUP(group),
      "GTK 前端会跟随 GNOME 的深色模式、强调色和高对比度设置。");

  GtkWidget* row = adw_action_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), "主题");
  adw_action_row_set_subtitle(ADW_ACTION_ROW(row),
                              "跟随系统（libadwaita 默认）");
  adw_action_row_add_suffix(ADW_ACTION_ROW(row),
                            gtk_label_new("自动"));
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), row);
  adw_preferences_page_add(ADW_PREFERENCES_PAGE(page),
                           ADW_PREFERENCES_GROUP(group));
  adw_preferences_window_add(ADW_PREFERENCES_WINDOW(preferences),
                             ADW_PREFERENCES_PAGE(page));
  gtk_window_present(GTK_WINDOW(preferences));
}

void Application::ShowAbout() {
  GtkWidget* about = adw_about_window_new();
  adw_about_window_set_application_name(ADW_ABOUT_WINDOW(about), "Spark ORG");
  adw_about_window_set_application_icon(ADW_ABOUT_WINDOW(about), "spark-tui");
  adw_about_window_set_developer_name(ADW_ABOUT_WINDOW(about), "Spark ORG");
  adw_about_window_set_version(ADW_ABOUT_WINDOW(about), SPARK_TUI_VERSION);
  adw_about_window_set_website(ADW_ABOUT_WINDOW(about),
                               "https://sparkorg.dpdns.org");
  adw_about_window_set_issue_url(
      ADW_ABOUT_WINDOW(about),
      "https://github.com/Spark-Org-IM/spark-client/issues");
  adw_about_window_set_copyright(ADW_ABOUT_WINDOW(about),
                                 "© 2026 Spark ORG");
  adw_about_window_set_license_type(ADW_ABOUT_WINDOW(about),
                                    GTK_LICENSE_MIT_X11);
  static const char* developers[] = {"Jianyin Li", nullptr};
  adw_about_window_set_developers(ADW_ABOUT_WINDOW(about), developers);
  gtk_window_set_transient_for(GTK_WINDOW(about), GTK_WINDOW(window_));
  gtk_window_present(GTK_WINDOW(about));
}

void Application::OnRowActivated(GtkListBox*, GtkListBoxRow* row,
                                 gpointer data) {
  if (row == nullptr) return;
  auto* self = static_cast<Application*>(data);
  const auto* page =
      static_cast<const char*>(g_object_get_data(G_OBJECT(row), "page"));
  const auto* title =
      static_cast<const char*>(g_object_get_data(G_OBJECT(row), "page-title"));
  if (page != nullptr) self->ShowPage(page);
  if (title != nullptr && self->title_label_ != nullptr) {
    gtk_label_set_text(GTK_LABEL(self->title_label_), title);
  }
}

void Application::OnSettings(GSimpleAction*, GVariant*, gpointer data) {
  static_cast<Application*>(data)->ShowSettings();
}

void Application::OnAbout(GSimpleAction*, GVariant*, gpointer data) {
  static_cast<Application*>(data)->ShowAbout();
}

void Application::OnQuit(GSimpleAction*, GVariant*, gpointer data) {
  auto* self = static_cast<Application*>(data);
  g_application_quit(G_APPLICATION(self->app_));
}

void Application::OnRefresh(GSimpleAction*, GVariant*, gpointer data) {
  auto* self = static_cast<Application*>(data);
  self->Toast("GTK 框架阶段：数据刷新接口将在下一步接入");
}

}  // namespace spark::gtk
