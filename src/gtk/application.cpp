#include "gtk/application.h"

#include <gdk/gdk.h>

#include <algorithm>
#include <string>
#include <utility>

#include "gtk/detail.h"
#include "gtk/pages/forum_page.h"
#include "gtk/pages/home_page.h"
#include "gtk/pages/list_page.h"
#include "gtk/pages/me_page.h"
#include "gtk/pages/plugins_page.h"
#include "gtk/widgets.h"
#include "i18n.h"

namespace spark::gtk {
namespace {

void AddCss() {
  GtkCssProvider* provider = gtk_css_provider_new();
  gtk_css_provider_load_from_string(
      provider,
      ".sidebar-footer { padding: 12px 16px; }"
      ".sidebar-footer label { font-size: 0.85em; }"
      ".detail-icon { opacity: 0.7; }");
  gtk_style_context_add_provider_for_display(
      gdk_display_get_default(), GTK_STYLE_PROVIDER(provider),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(provider);
}

const char* IconFor(const std::string& id) {
  if (id == "home") return "go-home-symbolic";
  if (id == "news") return "x-office-document-symbolic";
  if (id == "contact") return "system-users-symbolic";
  if (id == "platform") return "network-workgroup-symbolic";
  if (id == "forum") return "chat-message-new-symbolic";
  if (id == "me") return "avatar-default-symbolic";
  if (id == "plugins") return "application-x-addon-symbolic";
  return "application-x-executable-symbolic";
}

GtkWidget* MakeSidebarRow(const std::string& title, const char* icon) {
  GtkWidget* row = gtk_list_box_row_new();
  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_widget_set_margin_top(box, 8);
  gtk_widget_set_margin_bottom(box, 8);
  gtk_widget_set_margin_start(box, 12);
  gtk_widget_set_margin_end(box, 12);

  GtkWidget* image = gtk_image_new_from_icon_name(icon);
  GtkWidget* label = gtk_label_new(title.c_str());
  gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
  gtk_widget_set_hexpand(label, TRUE);
  gtk_box_append(GTK_BOX(box), image);
  gtk_box_append(GTK_BOX(box), label);
  gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
  return row;
}

void AddInfoRow(GtkWidget* group, const std::string& title,
                const std::string& value) {
  GtkWidget* row = adw_action_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title.c_str());
  adw_action_row_set_subtitle(ADW_ACTION_ROW(row), value.c_str());
  adw_action_row_set_subtitle_selectable(ADW_ACTION_ROW(row), TRUE);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), row);
}

}  // namespace

Application::Application(AdwApplication* app, Config config)
    : app_(app),
      config_(std::move(config)),
      api_(config_.EffectiveMain(), config_.insecure, config_.token.plaintext),
      forum_(config_.EffectiveForum(), config_.insecure,
             config_.forum.token),
      plugins_(&api_, &forum_, &config_) {
  plugins_.LoadAll();
  worker_ = std::make_unique<Worker>([this] {
    if (!running_.load()) return;
    PostToMainThread([this] {
      if (running_.load() && worker_) worker_->Drain();
    });
  });
}

Application::~Application() {
  running_.store(false);
  if (worker_) worker_->Stop();
}

void Application::Activate() {
  if (window_ == nullptr) BuildWindow();
  gtk_window_present(GTK_WINDOW(window_));
}

PageContext Application::MakeContext() {
  PageContext ctx;
  ctx.api = &api_;
  ctx.forum = &forum_;
  ctx.config = &config_;
  ctx.worker = worker_.get();
  ctx.plugins = &plugins_;
  ctx.lang = UiLanguage(config_);
  ctx.toast = [this](const std::string& message) { Toast(message); };
  ctx.push_detail = [this](const std::string& title, GtkWidget* content) {
    PushDetail(title, content);
  };
  ctx.refresh = [this](const std::string& target) {
    for (const auto& page : pages_) {
      const std::string id = page->Id();
      if (id == target || (target == "news" && id == "home")) {
        page->Refresh();
      }
    }
  };
  return ctx;
}

void Application::BuildPages() {
  const PageContext ctx = MakeContext();
  pages_.push_back(std::make_unique<HomePage>(ctx));
  pages_.push_back(std::make_unique<ListPage>(
      "news", "news", T(ctx, "新闻", "News"), "x-office-document-symbolic", ctx));
  pages_.push_back(std::make_unique<ListPage>(
      "contact", "contact", T(ctx, "联系", "Contact"),
      "system-users-symbolic", ctx));
  pages_.push_back(std::make_unique<ListPage>(
      "platform", "platform", T(ctx, "平台", "Platform"),
      "network-workgroup-symbolic", ctx));
  pages_.push_back(std::make_unique<ForumPage>(ctx));
  pages_.push_back(std::make_unique<MePage>(ctx));
  if (HasVisiblePluginActions(ctx)) {
    pages_.push_back(std::make_unique<PluginsPage>(ctx));
  }

  pluginActions_.clear();
  for (const PluginAction& action : plugins_.Actions()) {
    if (PluginScopesAllow(action, config_.token.scopes)) {
      pluginActions_.push_back(action);
    }
  }
  RebuildTopBarItems();
}

void Application::BuildWindow() {
  const PageContext ctx = MakeContext();
  AdwStyleManager* style = adw_style_manager_get_default();
  adw_style_manager_set_color_scheme(style, ADW_COLOR_SCHEME_DEFAULT);
  AddCss();

  window_ = adw_application_window_new(GTK_APPLICATION(app_));
  gtk_window_set_title(GTK_WINDOW(window_), "Spark ORG");
  gtk_window_set_default_size(GTK_WINDOW(window_), 1100, 760);

  BuildPages();

  GtkWidget* header = adw_header_bar_new();
  sidebar_button_ = MakeToolbarButton(
      "sidebar-show-symbolic",
      T(ctx, "显示/隐藏侧边栏", "Toggle sidebar").c_str());
  g_signal_connect(sidebar_button_, "clicked",
                   G_CALLBACK(&Application::OnToggleSidebar), this);
  adw_header_bar_pack_start(ADW_HEADER_BAR(header), sidebar_button_);

  GtkWidget* refresh_button = MakeToolbarButton(
      "view-refresh-symbolic", T(ctx, "刷新", "Refresh").c_str());
  g_signal_connect(refresh_button, "clicked", G_CALLBACK(+[](GtkButton*,
                                                             gpointer data) {
    Application::OnRefresh(nullptr, nullptr, data);
  }),
                   this);
  adw_header_bar_pack_end(ADW_HEADER_BAR(header), refresh_button);

  GMenu* menu = g_menu_new();
  g_menu_append(menu, T(ctx, "设置", "Settings").c_str(), "app.settings");
  g_menu_append(menu, T(ctx, "关于 Spark ORG", "About Spark ORG").c_str(),
                "app.about");
  g_menu_append(menu, T(ctx, "刷新", "Refresh").c_str(), "app.refresh");
  g_menu_append(menu, T(ctx, "退出", "Quit").c_str(), "app.quit");
  GtkWidget* menu_button = gtk_menu_button_new();
  gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menu_button),
                                "open-menu-symbolic");
  gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menu_button),
                                 G_MENU_MODEL(menu));
  adw_header_bar_pack_end(ADW_HEADER_BAR(header), menu_button);
  g_object_unref(menu);

  GtkWidget* sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_add_css_class(sidebar, "navigation-sidebar");

  sidebar_stack_ = gtk_stack_new();
  gtk_stack_set_transition_type(
      GTK_STACK(sidebar_stack_),
      GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
  gtk_stack_set_transition_duration(GTK_STACK(sidebar_stack_), 250);
  gtk_widget_set_vexpand(sidebar_stack_, TRUE);

  sidebar_main_list_ = gtk_list_box_new();
  gtk_list_box_set_selection_mode(GTK_LIST_BOX(sidebar_main_list_),
                                  GTK_SELECTION_SINGLE);
  gtk_list_box_set_show_separators(GTK_LIST_BOX(sidebar_main_list_), FALSE);
  g_signal_connect(sidebar_main_list_, "row-activated",
                   G_CALLBACK(&Application::OnRowActivated), this);
  gtk_stack_add_named(GTK_STACK(sidebar_stack_),
                      MakeScrolled(sidebar_main_list_), "main");

  sidebar_collapsed_page_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  GtkWidget* back_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_margin_top(back_row, 8);
  gtk_widget_set_margin_bottom(back_row, 4);
  gtk_widget_set_margin_start(back_row, 8);
  gtk_widget_set_margin_end(back_row, 8);
  GtkWidget* back = gtk_button_new_with_label(
      ("‹ " + T(ctx, "返回", "Back")).c_str());
  gtk_widget_add_css_class(back, "flat");
  gtk_widget_set_halign(back, GTK_ALIGN_START);
  g_signal_connect(back, "clicked", G_CALLBACK(&Application::OnSidebarBack),
                   this);
  gtk_box_append(GTK_BOX(back_row), back);
  gtk_box_append(GTK_BOX(sidebar_collapsed_page_), back_row);
  sidebar_collapsed_list_ = gtk_list_box_new();
  gtk_list_box_set_selection_mode(GTK_LIST_BOX(sidebar_collapsed_list_),
                                  GTK_SELECTION_SINGLE);
  gtk_list_box_set_show_separators(GTK_LIST_BOX(sidebar_collapsed_list_),
                                   FALSE);
  g_signal_connect(sidebar_collapsed_list_, "row-activated",
                   G_CALLBACK(&Application::OnCollapsedRowActivated), this);
  gtk_box_append(GTK_BOX(sidebar_collapsed_page_),
                 MakeScrolled(sidebar_collapsed_list_));
  gtk_stack_add_named(GTK_STACK(sidebar_stack_), sidebar_collapsed_page_,
                      "collapsed");

  gtk_box_append(GTK_BOX(sidebar), sidebar_stack_);

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
  for (const auto& page : pages_) {
    gtk_stack_add_named(GTK_STACK(stack_), page->Widget(), page->Id());
  }

  split_ = adw_navigation_split_view_new();
  adw_navigation_split_view_set_sidebar(
      ADW_NAVIGATION_SPLIT_VIEW(split_),
      adw_navigation_page_new(sidebar, "Spark ORG"));
  adw_navigation_split_view_set_content(
      ADW_NAVIGATION_SPLIT_VIEW(split_),
      adw_navigation_page_new(stack_, "Spark ORG"));
  adw_navigation_split_view_set_show_content(
      ADW_NAVIGATION_SPLIT_VIEW(split_), TRUE);
  // The toggle only makes sense in the collapsed (narrow window) layout;
  // the expanded layout always shows the sidebar.
  gtk_widget_set_visible(
      sidebar_button_,
      adw_navigation_split_view_get_collapsed(ADW_NAVIGATION_SPLIT_VIEW(split_)));
  g_signal_connect(split_, "notify::collapsed",
                   G_CALLBACK(&Application::OnSplitCollapsed), this);

  // AdwNavigationSplitView does not collapse on its own. Collapse it below
  // 700sp (roughly a phone in portrait) and only then show the sidebar
  // toggle button.
  AdwBreakpoint* narrow = adw_breakpoint_new(
      adw_breakpoint_condition_parse("max-width: 700sp"));
  const auto add_bool_setter = [](AdwBreakpoint* breakpoint, GObject* object,
                                  const char* property) {
    GValue value = G_VALUE_INIT;
    g_value_init(&value, G_TYPE_BOOLEAN);
    g_value_set_boolean(&value, TRUE);
    adw_breakpoint_add_setter(breakpoint, object, property, &value);
    g_value_unset(&value);
  };
  add_bool_setter(narrow, G_OBJECT(split_), "collapsed");
  add_bool_setter(narrow, G_OBJECT(sidebar_button_), "visible");
  adw_application_window_add_breakpoint(ADW_APPLICATION_WINDOW(window_), narrow);

  GtkWidget* toolbar = adw_toolbar_view_new();
  adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), header);
  adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), split_);

  navigation_ = adw_navigation_view_new();
  root_page_ = adw_navigation_page_new(toolbar, "Spark ORG");
  adw_navigation_view_add(ADW_NAVIGATION_VIEW(navigation_), root_page_);

  toast_overlay_ = adw_toast_overlay_new();
  adw_toast_overlay_set_child(ADW_TOAST_OVERLAY(toast_overlay_), navigation_);
  adw_application_window_set_content(ADW_APPLICATION_WINDOW(window_),
                                     toast_overlay_);

  RebuildSidebarLists();
  ShowPage(FirstVisiblePage());
  InstallActions();
}

void Application::InstallActions() {
  static const GActionEntry entries[] = {
      {"settings", &Application::OnSettings, nullptr, nullptr, nullptr,
       {0, 0, 0}},
      {"about", &Application::OnAbout, nullptr, nullptr, nullptr, {0, 0, 0}},
      {"quit", &Application::OnQuit, nullptr, nullptr, nullptr, {0, 0, 0}},
      {"refresh", &Application::OnRefresh, nullptr, nullptr, nullptr,
       {0, 0, 0}},
  };
  g_action_map_add_action_entries(G_ACTION_MAP(app_), entries,
                                  G_N_ELEMENTS(entries), this);
}

Page* Application::CurrentPage() {
  for (const auto& page : pages_) {
    if (current_page_ == page->Id()) return page.get();
  }
  return nullptr;
}

void Application::ShowPage(const std::string& id) {
  if (stack_ == nullptr) return;
  Page* target = nullptr;
  for (const auto& page : pages_) {
    if (id == page->Id()) {
      target = page.get();
      break;
    }
  }
  if (target == nullptr) return;

  gtk_stack_set_visible_child_name(GTK_STACK(stack_), id.c_str());
  current_page_ = id;
  for (const TopBarItem& item : topbar_) {
    if (item.id == "page:" + id && item.visible && item.collapsed) {
      last_collapsed_id_ = item.id;
      UpdateCollapseLabel();
      break;
    }
  }
  if (root_page_ != nullptr) {
    adw_navigation_page_set_title(root_page_, target->Title().c_str());
  }

  SelectSidebarPage(id);

  if (split_ != nullptr &&
      adw_navigation_split_view_get_collapsed(
          ADW_NAVIGATION_SPLIT_VIEW(split_))) {
    adw_navigation_split_view_set_show_content(
        ADW_NAVIGATION_SPLIT_VIEW(split_), TRUE);
  }

  target->OnShown();
}

void Application::RebuildTopBarItems() {
  std::vector<TopBarSpec> specs;
  std::vector<TopBarItem> items;
  specs.reserve(pages_.size() + pluginActions_.size());
  items.reserve(specs.capacity());

  for (const auto& page : pages_) {
    TopBarSpec spec;
    spec.id = std::string("page:") + page->Id();
    specs.push_back(spec);

    TopBarItem item;
    item.id = spec.id;
    item.title = page->Title();
    item.kind = TopBarItem::Kind::Page;
    item.pageId = page->Id();
    items.push_back(std::move(item));
  }

  for (int i = 0; i < static_cast<int>(pluginActions_.size()); ++i) {
    const PluginAction& action = pluginActions_[static_cast<std::size_t>(i)];
    TopBarSpec spec;
    spec.id = "plugin:" + action.pluginId + ":" + action.id;
    spec.visible = false;
    spec.collapsed = true;
    specs.push_back(spec);

    TopBarItem item;
    item.id = spec.id;
    item.title = action.title.empty() ? action.id : action.title;
    item.subtitle = action.pluginId;
    for (const PluginInfo& info : plugins_.Plugins()) {
      if (info.id == action.pluginId) {
        item.subtitle = info.name;
        break;
      }
    }
    item.kind = TopBarItem::Kind::PluginAction;
    item.action = i;
    items.push_back(std::move(item));
  }

  const std::vector<TopBarState> states =
      MergeTopBarStates(specs, config_.topBar);
  topbar_.clear();
  topbar_.reserve(states.size());
  for (const TopBarState& state : states) {
    const auto it = std::find_if(
        items.begin(), items.end(),
        [&](const TopBarItem& item) { return item.id == state.id; });
    if (it == items.end()) continue;
    TopBarItem item = *it;
    item.visible = state.visible;
    item.collapsed = state.collapsed;
    topbar_.push_back(std::move(item));
  }
}

bool Application::PageIsVisible(const std::string& pageId) const {
  const std::string id = "page:" + pageId;
  for (const TopBarItem& item : topbar_) {
    if (item.id == id) return item.visible;
  }
  return false;
}

std::string Application::FirstVisiblePage() const {
  for (const TopBarItem& item : topbar_) {
    if (item.kind == TopBarItem::Kind::Page && item.visible) {
      return item.pageId;
    }
  }
  return "home";
}

GtkWidget* Application::MakeTopBarRow(const TopBarItem& item) {
  const char* icon = item.kind == TopBarItem::Kind::Page
                         ? IconFor(item.pageId)
                         : "application-x-addon-symbolic";
  GtkWidget* row = MakeSidebarRow(item.title, icon);
  g_object_set_data_full(G_OBJECT(row), "topbar-id",
                         g_strdup(item.id.c_str()), g_free);
  return row;
}

GtkWidget* Application::MakeCollapseRow() {
  GtkWidget* row = gtk_list_box_row_new();
  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_widget_set_margin_top(box, 8);
  gtk_widget_set_margin_bottom(box, 8);
  gtk_widget_set_margin_start(box, 12);
  gtk_widget_set_margin_end(box, 12);

  GtkWidget* image =
      gtk_image_new_from_icon_name("view-more-horizontal-symbolic");
  GtkWidget* label = gtk_label_new("");
  gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
  gtk_widget_set_hexpand(label, TRUE);
  GtkWidget* triangle = gtk_label_new("▾");
  gtk_widget_add_css_class(triangle, "dim-label");

  gtk_box_append(GTK_BOX(box), image);
  gtk_box_append(GTK_BOX(box), label);
  gtk_box_append(GTK_BOX(box), triangle);
  gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
  g_object_set_data_full(G_OBJECT(row), "topbar-id", g_strdup("collapse"),
                         g_free);

  sidebar_collapse_row_ = row;
  sidebar_collapse_label_ = label;
  UpdateCollapseLabel();
  return row;
}

void Application::UpdateCollapseLabel() {
  if (sidebar_collapse_label_ == nullptr) return;
  const std::string lang = UiLanguage(config_);
  std::string label = Tr("已折叠", "Collapsed", lang);
  for (const TopBarItem& item : topbar_) {
    if (item.id == last_collapsed_id_ && item.visible && item.collapsed) {
      label = item.title;
      break;
    }
  }
  gtk_label_set_text(GTK_LABEL(sidebar_collapse_label_), label.c_str());
}

void Application::RebuildSidebarLists() {
  if (sidebar_main_list_ == nullptr || sidebar_collapsed_list_ == nullptr) {
    return;
  }
  ClearListBox(GTK_LIST_BOX(sidebar_main_list_));
  ClearListBox(GTK_LIST_BOX(sidebar_collapsed_list_));
  sidebar_collapse_row_ = nullptr;
  sidebar_collapse_label_ = nullptr;

  bool has_collapsed = false;
  for (const TopBarItem& item : topbar_) {
    if (!item.visible) continue;
    if (item.collapsed) {
      has_collapsed = true;
      gtk_list_box_append(GTK_LIST_BOX(sidebar_collapsed_list_),
                          MakeTopBarRow(item));
    } else {
      gtk_list_box_append(GTK_LIST_BOX(sidebar_main_list_),
                          MakeTopBarRow(item));
    }
  }
  if (has_collapsed) {
    gtk_list_box_append(GTK_LIST_BOX(sidebar_main_list_),
                        MakeCollapseRow());
  }
  if (!has_collapsed && sidebar_collapsed_) {
    ShowSidebarCollapsed(false);
  }
  UpdateCollapseLabel();

  if (!PageIsVisible(current_page_)) {
    ShowPage(FirstVisiblePage());
  } else {
    SelectSidebarPage(current_page_);
  }
}

void Application::SelectSidebarPage(const std::string& pageId) {
  if (sidebar_main_list_ == nullptr) return;
  const std::string target = "page:" + pageId;
  GtkWidget* child = gtk_widget_get_first_child(sidebar_main_list_);
  bool collapsed_match = false;
  while (child != nullptr) {
    const auto* id =
        static_cast<const char*>(g_object_get_data(G_OBJECT(child), "topbar-id"));
    if (id != nullptr && target == id) {
      gtk_list_box_select_row(GTK_LIST_BOX(sidebar_main_list_),
                              GTK_LIST_BOX_ROW(child));
      return;
    }
    if (id != nullptr && std::string(id) == "collapse") {
      collapsed_match = true;
    }
    child = gtk_widget_get_next_sibling(child);
  }
  if (collapsed_match) {
    for (const TopBarItem& item : topbar_) {
      if (item.id == target && item.visible && item.collapsed &&
          sidebar_collapse_row_ != nullptr) {
        gtk_list_box_select_row(GTK_LIST_BOX(sidebar_main_list_),
                                GTK_LIST_BOX_ROW(sidebar_collapse_row_));
        return;
      }
    }
  }
  gtk_list_box_unselect_all(GTK_LIST_BOX(sidebar_main_list_));
}

void Application::ShowSidebarCollapsed(bool collapsed) {
  if (sidebar_stack_ == nullptr) return;
  sidebar_collapsed_ = collapsed;
  gtk_stack_set_transition_type(
      GTK_STACK(sidebar_stack_),
      collapsed ? GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT
                : GTK_STACK_TRANSITION_TYPE_SLIDE_RIGHT);
  gtk_stack_set_visible_child_name(GTK_STACK(sidebar_stack_),
                                   collapsed ? "collapsed" : "main");

  if (collapsed && sidebar_collapsed_list_ != nullptr &&
      !last_collapsed_id_.empty()) {
    GtkWidget* child = gtk_widget_get_first_child(sidebar_collapsed_list_);
    while (child != nullptr) {
      const auto* id = static_cast<const char*>(
          g_object_get_data(G_OBJECT(child), "topbar-id"));
      if (id != nullptr && last_collapsed_id_ == id) {
        gtk_list_box_select_row(GTK_LIST_BOX(sidebar_collapsed_list_),
                                GTK_LIST_BOX_ROW(child));
        break;
      }
      child = gtk_widget_get_next_sibling(child);
    }
  }
}

void Application::ActivateTopBarId(const std::string& id) {
  if (id == "collapse") {
    ShowSidebarCollapsed(true);
    return;
  }
  if (id.rfind("page:", 0) == 0) {
    ShowPage(id.substr(5));
    return;
  }
  if (id.rfind("plugin:", 0) != 0) return;
  for (const TopBarItem& item : topbar_) {
    if (item.id != id) continue;
    if (item.action >= 0 &&
        item.action < static_cast<int>(pluginActions_.size())) {
      ShowPluginActionDialog(MakeContext(),
                             pluginActions_[static_cast<std::size_t>(item.action)],
                             window_);
    }
    return;
  }
}

void Application::SaveTopBarPrefs() {
  std::vector<TopBarState> states;
  states.reserve(topbar_.size());
  for (const TopBarItem& item : topbar_) {
    states.push_back({item.id, item.visible, item.collapsed});
  }
  config_.topBar = TopBarPrefsFromStates(states);
  config_.Save();
}

void Application::OnCollapsedRowActivated(GtkListBox*, GtkListBoxRow* row,
                                          gpointer data) {
  if (row == nullptr) return;
  auto* self = static_cast<Application*>(data);
  const auto* id = static_cast<const char*>(
      g_object_get_data(G_OBJECT(row), "topbar-id"));
  if (id == nullptr) return;
  self->last_collapsed_id_ = id;
  self->ShowSidebarCollapsed(false);
  self->ActivateTopBarId(id);
  self->UpdateCollapseLabel();
}

void Application::OnCollapseRowActivated(GtkListBox*, GtkListBoxRow*,
                                         gpointer data) {
  static_cast<Application*>(data)->ShowSidebarCollapsed(true);
}

void Application::OnSidebarBack(GtkButton*, gpointer data) {
  static_cast<Application*>(data)->ShowSidebarCollapsed(false);
}

void Application::PushDetail(const std::string& title, GtkWidget* content) {
  if (content == nullptr) return;
  if (navigation_ == nullptr) {
    g_object_ref_sink(content);
    g_object_unref(content);
    return;
  }
  GtkWidget* toolbar = adw_toolbar_view_new();
  GtkWidget* header = adw_header_bar_new();
  adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), header);
  adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), content);
  AdwNavigationPage* page = adw_navigation_page_new(toolbar, title.c_str());
  adw_navigation_view_push(ADW_NAVIGATION_VIEW(navigation_), page);
}

void Application::Toast(const std::string& message) {
  if (toast_overlay_ == nullptr) return;
  AdwToast* toast = adw_toast_new(message.c_str());
  adw_toast_overlay_add_toast(ADW_TOAST_OVERLAY(toast_overlay_), toast);
}

void Application::ShowSettings() {
  if (settings_window_ != nullptr) {
    gtk_window_present(GTK_WINDOW(settings_window_));
    return;
  }
  const PageContext ctx = MakeContext();
  GtkWidget* preferences = adw_preferences_window_new();
  settings_window_ = preferences;
  g_signal_connect(
      preferences, "destroy",
      G_CALLBACK(+[](GtkWidget*, gpointer data) {
        static_cast<Application*>(data)->settings_window_ = nullptr;
      }),
      this);
  gtk_window_set_title(GTK_WINDOW(preferences),
                       T(ctx, "设置", "Settings").c_str());
  gtk_window_set_transient_for(GTK_WINDOW(preferences), GTK_WINDOW(window_));
  gtk_window_set_default_size(GTK_WINDOW(preferences), 680, 560);

  GtkWidget* page = adw_preferences_page_new();

  GtkWidget* instances = adw_preferences_group_new();
  adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(instances),
                                  T(ctx, "实例", "Instances").c_str());
  AddInfoRow(instances, T(ctx, "主服务器", "Main server"),
             config_.EffectiveMain());
  AddInfoRow(instances, T(ctx, "状态服务器", "Status server"),
             config_.EffectiveStatus());
  AddInfoRow(instances, T(ctx, "论坛", "Forum"),
             config_.EffectiveForum());
  adw_preferences_page_add(ADW_PREFERENCES_PAGE(page),
                           ADW_PREFERENCES_GROUP(instances));

  GtkWidget* appearance = adw_preferences_group_new();
  adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(appearance),
                                  T(ctx, "外观", "Appearance").c_str());
  adw_preferences_group_set_description(
      ADW_PREFERENCES_GROUP(appearance),
      T(ctx,
        "GTK 前端会跟随 GNOME 的深色模式、强调色和高对比度设置。",
        "The GTK frontend follows GNOME's dark mode, accent color and high "
        "contrast settings.")
          .c_str());

  GtkWidget* theme = adw_action_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(theme),
                                T(ctx, "主题", "Theme").c_str());
  adw_action_row_set_subtitle(
      ADW_ACTION_ROW(theme),
      T(ctx, "跟随系统（libadwaita 默认）",
        "Follow the system (libadwaita default)")
          .c_str());
  adw_action_row_add_suffix(
      ADW_ACTION_ROW(theme),
      gtk_label_new(T(ctx, "自动", "Automatic").c_str()));
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(appearance), theme);

  GtkWidget* language = adw_combo_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(language),
                                T(ctx, "语言", "Language").c_str());
  const char* languages[] = {"跟随系统 / System", "中文", "English", nullptr};
  GtkStringList* language_model = gtk_string_list_new(languages);
  adw_combo_row_set_model(ADW_COMBO_ROW(language),
                          G_LIST_MODEL(language_model));
  g_object_unref(language_model);
  guint selected = 0;
  if (config_.language.rfind("en", 0) == 0) {
    selected = 2;
  } else if (!config_.language.empty() && config_.language != "system") {
    selected = 1;
  }
  adw_combo_row_set_selected(ADW_COMBO_ROW(language), selected);
  g_signal_connect(language, "notify::selected",
                   G_CALLBACK(&Application::OnLanguageChanged), this);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(appearance), language);

  GtkWidget* raw = adw_switch_row_new();
  adw_preferences_row_set_title(
      ADW_PREFERENCES_ROW(raw),
      T(ctx, "详情显示底层数据", "Show raw detail data").c_str());
  adw_switch_row_set_active(ADW_SWITCH_ROW(raw), config_.showRawDetails);
  g_signal_connect(raw, "notify::active",
                   G_CALLBACK(&Application::OnRawToggled), this);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(appearance), raw);

  adw_preferences_page_add(ADW_PREFERENCES_PAGE(page),
                           ADW_PREFERENCES_GROUP(appearance));

  GtkWidget* navigation = adw_preferences_group_new();
  adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(navigation),
                                  T(ctx, "顶栏", "Top bar").c_str());
  GtkWidget* topbar_row = adw_action_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(topbar_row),
                                T(ctx, "顶栏元素", "Top bar entries").c_str());
  adw_action_row_set_subtitle(
      ADW_ACTION_ROW(topbar_row),
      T(ctx, "可见性、折叠与顺序",
        "Visibility, collapsed group and order")
          .c_str());
  adw_action_row_add_suffix(ADW_ACTION_ROW(topbar_row),
                            gtk_image_new_from_icon_name("go-next-symbolic"));
  gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(topbar_row), TRUE);
  g_signal_connect(topbar_row, "activated",
                   G_CALLBACK(&Application::OnTopBarSettings), this);
  adw_preferences_group_add(ADW_PREFERENCES_GROUP(navigation), topbar_row);
  adw_preferences_page_add(ADW_PREFERENCES_PAGE(page),
                           ADW_PREFERENCES_GROUP(navigation));

  GtkWidget* accounts = adw_preferences_group_new();
  adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(accounts),
                                  T(ctx, "账号", "Accounts").c_str());
  AddInfoRow(accounts, "Spark ORG",
             config_.token.Empty()
                 ? T(ctx, "未登录", "Not signed in")
                 : (config_.token.username.empty()
                        ? T(ctx, "已登录", "Signed in")
                        : config_.token.username));
  AddInfoRow(accounts, T(ctx, "论坛账号", "Forum account"),
             config_.forum.Empty()
                 ? T(ctx, "未登录", "Not signed in")
                 : (config_.forum.displayName.empty()
                        ? config_.forum.username
                        : config_.forum.displayName));
  adw_preferences_page_add(ADW_PREFERENCES_PAGE(page),
                           ADW_PREFERENCES_GROUP(accounts));

  adw_preferences_window_add(ADW_PREFERENCES_WINDOW(preferences),
                             ADW_PREFERENCES_PAGE(page));
  gtk_window_present(GTK_WINDOW(preferences));
}

void Application::ShowTopBarSettings() {
  const std::string lang = UiLanguage(config_);
  AdwDialog* dialog = ADW_DIALOG(adw_dialog_new());
  adw_dialog_set_title(
      dialog, Tr("顶栏元素", "Top bar entries", lang).c_str());
  adw_dialog_set_content_width(dialog, 640);
  adw_dialog_set_content_height(dialog, 620);

  AdwToolbarView* toolbar = ADW_TOOLBAR_VIEW(adw_toolbar_view_new());
  AdwHeaderBar* header = ADW_HEADER_BAR(adw_header_bar_new());
  adw_toolbar_view_add_top_bar(toolbar, GTK_WIDGET(header));

  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(box, 18);
  gtk_widget_set_margin_bottom(box, 18);
  gtk_widget_set_margin_start(box, 18);
  gtk_widget_set_margin_end(box, 18);
  gtk_box_append(
      GTK_BOX(box),
      MakeDimLabel(
          Tr("列表顺序就是顶栏顺序；折叠的元素收进“已折叠”。",
             "List order is top bar order; collapsed entries go into the "
             "collapsed group.",
             lang),
          true));

  topbar_settings_list_ = gtk_list_box_new();
  gtk_list_box_set_selection_mode(GTK_LIST_BOX(topbar_settings_list_),
                                  GTK_SELECTION_NONE);
  gtk_widget_add_css_class(topbar_settings_list_, "boxed-list");
  FillTopBarSettingsList();
  gtk_box_append(GTK_BOX(box), MakeScrolled(topbar_settings_list_));
  adw_toolbar_view_set_content(toolbar, box);
  adw_dialog_set_child(dialog, GTK_WIDGET(toolbar));

  g_signal_connect(
      dialog, "closed",
      G_CALLBACK(+[](AdwDialog*, gpointer data) {
        static_cast<Application*>(data)->topbar_settings_list_ = nullptr;
      }),
      this);
  adw_dialog_present(dialog, settings_window_ != nullptr ? settings_window_
                                                          : window_);
}

void Application::FillTopBarSettingsList() {
  if (topbar_settings_list_ == nullptr) return;
  ClearListBox(GTK_LIST_BOX(topbar_settings_list_));
  const std::string lang = UiLanguage(config_);

  for (int i = 0; i < static_cast<int>(topbar_.size()); ++i) {
    const TopBarItem& item = topbar_[static_cast<std::size_t>(i)];
    GtkWidget* row = gtk_list_box_row_new();
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);

    GtkWidget* outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(outer, 12);
    gtk_widget_set_margin_bottom(outer, 12);
    gtk_widget_set_margin_start(outer, 12);
    gtk_widget_set_margin_end(outer, 12);

    GtkWidget* head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* title = MakeLabel(item.title, "heading", false);
    gtk_widget_set_hexpand(title, TRUE);
    std::string kind = Tr("页面", "Page", lang);
    if (item.kind == TopBarItem::Kind::PluginAction) {
      kind = Tr("插件", "Plugin", lang);
      if (!item.subtitle.empty()) kind += " · " + item.subtitle;
    }
    gtk_box_append(GTK_BOX(head), title);
    gtk_box_append(GTK_BOX(head), MakeDimLabel(kind, false));

    GtkWidget* controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* shown = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(shown), item.visible);
    g_object_set_data(G_OBJECT(shown), "topbar-index", GINT_TO_POINTER(i + 1));
    g_signal_connect(shown, "notify::active",
                     G_CALLBACK(&Application::OnTopBarVisible), this);

    GtkWidget* collapsed = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(collapsed), item.collapsed);
    g_object_set_data(G_OBJECT(collapsed), "topbar-index",
                      GINT_TO_POINTER(i + 1));
    g_signal_connect(collapsed, "notify::active",
                     G_CALLBACK(&Application::OnTopBarCollapsed), this);

    GtkWidget* spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);

    GtkWidget* up = gtk_button_new_from_icon_name("go-up-symbolic");
    gtk_widget_add_css_class(up, "flat");
    gtk_widget_set_sensitive(up, i > 0);
    g_object_set_data(G_OBJECT(up), "topbar-index", GINT_TO_POINTER(i + 1));
    g_object_set_data(G_OBJECT(up), "topbar-delta", GINT_TO_POINTER(-1));
    g_signal_connect(up, "clicked", G_CALLBACK(&Application::OnTopBarMove),
                     this);

    GtkWidget* down = gtk_button_new_from_icon_name("go-down-symbolic");
    gtk_widget_add_css_class(down, "flat");
    gtk_widget_set_sensitive(down, i + 1 < static_cast<int>(topbar_.size()));
    g_object_set_data(G_OBJECT(down), "topbar-index", GINT_TO_POINTER(i + 1));
    g_object_set_data(G_OBJECT(down), "topbar-delta", GINT_TO_POINTER(1));
    g_signal_connect(down, "clicked", G_CALLBACK(&Application::OnTopBarMove),
                     this);

    gtk_box_append(GTK_BOX(controls),
                   MakeDimLabel(Tr("显示", "Shown", lang), false));
    gtk_box_append(GTK_BOX(controls), shown);
    gtk_box_append(GTK_BOX(controls),
                   MakeDimLabel(Tr("折叠", "Collapsed", lang), false));
    gtk_box_append(GTK_BOX(controls), collapsed);
    gtk_box_append(GTK_BOX(controls), spacer);
    gtk_box_append(GTK_BOX(controls), up);
    gtk_box_append(GTK_BOX(controls), down);

    gtk_box_append(GTK_BOX(outer), head);
    gtk_box_append(GTK_BOX(outer), controls);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), outer);
    gtk_list_box_append(GTK_LIST_BOX(topbar_settings_list_), row);
  }
}

void Application::OnTopBarSettings(GtkWidget*, gpointer data) {
  static_cast<Application*>(data)->ShowTopBarSettings();
}

void Application::OnTopBarVisible(GObject* object, GParamSpec*, gpointer data) {
  auto* self = static_cast<Application*>(data);
  const int index =
      GPOINTER_TO_INT(g_object_get_data(object, "topbar-index")) - 1;
  if (index < 0 || index >= static_cast<int>(self->topbar_.size())) return;
  TopBarItem& item = self->topbar_[static_cast<std::size_t>(index)];
  item.visible = gtk_switch_get_active(GTK_SWITCH(object));
  if (!item.visible && item.id == self->last_collapsed_id_) {
    self->last_collapsed_id_.clear();
  }
  self->SaveTopBarPrefs();
  self->RebuildSidebarLists();
}

void Application::OnTopBarCollapsed(GObject* object, GParamSpec*,
                                    gpointer data) {
  auto* self = static_cast<Application*>(data);
  const int index =
      GPOINTER_TO_INT(g_object_get_data(object, "topbar-index")) - 1;
  if (index < 0 || index >= static_cast<int>(self->topbar_.size())) return;
  TopBarItem& item = self->topbar_[static_cast<std::size_t>(index)];
  item.collapsed = gtk_switch_get_active(GTK_SWITCH(object));
  if (!item.collapsed && item.id == self->last_collapsed_id_) {
    self->last_collapsed_id_.clear();
  }
  self->SaveTopBarPrefs();
  self->RebuildSidebarLists();
}

void Application::OnTopBarMove(GtkButton* button, gpointer data) {
  auto* self = static_cast<Application*>(data);
  const int index =
      GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "topbar-index")) - 1;
  const int delta =
      GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "topbar-delta"));
  const int target = index + delta;
  if (index < 0 || index >= static_cast<int>(self->topbar_.size()) ||
      target < 0 || target >= static_cast<int>(self->topbar_.size())) {
    return;
  }
  std::swap(self->topbar_[static_cast<std::size_t>(index)],
            self->topbar_[static_cast<std::size_t>(target)]);
  self->SaveTopBarPrefs();
  self->FillTopBarSettingsList();
  self->RebuildSidebarLists();
}

void Application::ShowAbout() {
  static const char* developers[] = {"Jianyin Li", nullptr};
#if ADW_CHECK_VERSION(1, 5, 0)
  AdwDialog* about = ADW_DIALOG(adw_about_dialog_new());
  adw_about_dialog_set_application_name(ADW_ABOUT_DIALOG(about), "Spark ORG");
  adw_about_dialog_set_application_icon(ADW_ABOUT_DIALOG(about), "spark-tui");
  adw_about_dialog_set_developer_name(ADW_ABOUT_DIALOG(about), "Spark ORG");
  adw_about_dialog_set_version(ADW_ABOUT_DIALOG(about), SPARK_TUI_VERSION);
  adw_about_dialog_set_website(ADW_ABOUT_DIALOG(about),
                               "https://sparkorg.dpdns.org");
  adw_about_dialog_set_issue_url(
      ADW_ABOUT_DIALOG(about),
      "https://github.com/Spark-Org-IM/spark-client/issues");
  adw_about_dialog_set_copyright(ADW_ABOUT_DIALOG(about),
                                 "© 2026 Spark ORG");
  adw_about_dialog_set_license_type(ADW_ABOUT_DIALOG(about),
                                    GTK_LICENSE_MIT_X11);
  adw_about_dialog_set_developers(ADW_ABOUT_DIALOG(about), developers);
  adw_dialog_present(about, window_);
#else
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
  adw_about_window_set_developers(ADW_ABOUT_WINDOW(about), developers);
  gtk_window_set_transient_for(GTK_WINDOW(about), GTK_WINDOW(window_));
  gtk_window_present(GTK_WINDOW(about));
#endif
}

void Application::OnRowActivated(GtkListBox*, GtkListBoxRow* row,
                                 gpointer data) {
  if (row == nullptr) return;
  auto* self = static_cast<Application*>(data);
  const auto* id = static_cast<const char*>(
      g_object_get_data(G_OBJECT(row), "topbar-id"));
  if (id != nullptr) self->ActivateTopBarId(id);
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
  Page* page = self->CurrentPage();
  if (page == nullptr) return;
  page->Refresh();
  self->Toast(T(self->MakeContext(), "正在刷新…", "Refreshing…"));
}

void Application::OnSplitCollapsed(GObject* object, GParamSpec*, gpointer data) {
  auto* self = static_cast<Application*>(data);
  if (self->sidebar_button_ == nullptr) return;
  const gboolean collapsed =
      adw_navigation_split_view_get_collapsed(ADW_NAVIGATION_SPLIT_VIEW(object));
  gtk_widget_set_visible(self->sidebar_button_, collapsed);
  if (!collapsed) {
    adw_navigation_split_view_set_show_content(
        ADW_NAVIGATION_SPLIT_VIEW(object), TRUE);
  }
}

void Application::OnToggleSidebar(GtkButton*, gpointer data) {
  auto* self = static_cast<Application*>(data);
  if (self->split_ == nullptr) return;
  const gboolean show_content = adw_navigation_split_view_get_show_content(
      ADW_NAVIGATION_SPLIT_VIEW(self->split_));
  adw_navigation_split_view_set_show_content(
      ADW_NAVIGATION_SPLIT_VIEW(self->split_), !show_content);
}

void Application::OnLanguageChanged(GObject* object, GParamSpec*, gpointer data) {
  auto* self = static_cast<Application*>(data);
  const guint selected = adw_combo_row_get_selected(ADW_COMBO_ROW(object));
  if (selected == 0) {
    self->config_.language = "system";
  } else if (selected == 2) {
    self->config_.language = "en-us";
  } else {
    self->config_.language = "zh-cn";
  }
  self->config_.Save();
  self->Toast(Tr("语言将在重启后生效", "Language changes take effect after restart",
                 UiLanguage(self->config_)));
}

void Application::OnRawToggled(GObject* object, GParamSpec*, gpointer data) {
  auto* self = static_cast<Application*>(data);
  self->config_.showRawDetails = adw_switch_row_get_active(ADW_SWITCH_ROW(object));
  self->config_.Save();
}

}  // namespace spark::gtk
