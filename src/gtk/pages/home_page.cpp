#include "gtk/pages/home_page.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "api/models.h"
#include "gtk/detail.h"
#include "gtk/widgets.h"
#include "util.h"

namespace spark::gtk {

HomePage::HomePage(PageContext ctx) : ctx_(std::move(ctx)) {}

GtkWidget* HomePage::Widget() {
  if (root_ == nullptr) Build();
  return root_;
}

void HomePage::Build() {
  root_ = gtk_stack_new();
  gtk_stack_set_transition_type(GTK_STACK(root_),
                                GTK_STACK_TRANSITION_TYPE_CROSSFADE);

  status_ = MakeStatusPage("go-home-symbolic", title_,
                           T(ctx_, "正在加载…", "Loading…"));
  gtk_stack_add_named(GTK_STACK(root_), status_, "status");

  GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
  gtk_widget_set_margin_top(content, 24);
  gtk_widget_set_margin_bottom(content, 32);
  gtk_widget_set_margin_start(content, 18);
  gtk_widget_set_margin_end(content, 18);

  gtk_box_append(GTK_BOX(content), MakeLabel(title_, "title-1", false));
  gtk_box_append(
      GTK_BOX(content),
      MakeDimLabel(T(ctx_, "资讯、联系人与平台入口",
                     "News, contacts and platform links")));

  GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget* heading = MakeHeading(T(ctx_, "最新新闻", "Latest news"));
  gtk_widget_set_hexpand(heading, TRUE);
  gtk_box_append(GTK_BOX(header), heading);
  spinner_ = MakeSpinner();
  gtk_widget_set_visible(spinner_, FALSE);
  gtk_box_append(GTK_BOX(header), spinner_);
  gtk_box_append(GTK_BOX(content), header);

  list_ = gtk_list_box_new();
  gtk_list_box_set_selection_mode(GTK_LIST_BOX(list_), GTK_SELECTION_NONE);
  gtk_list_box_set_show_separators(GTK_LIST_BOX(list_), FALSE);
  gtk_widget_add_css_class(list_, "boxed-list");
  SetListBoxPlaceholder(GTK_LIST_BOX(list_), "x-office-document-symbolic",
                        T(ctx_, "暂无新闻", "No news yet"), "");
  gtk_box_append(GTK_BOX(content), list_);

  gtk_stack_add_named(GTK_STACK(root_), MakeScrolled(MakeClamp(content)),
                      "content");
  gtk_stack_set_visible_child_name(GTK_STACK(root_), "status");
}

void HomePage::OnShown() {
  if (!loaded_ && !loading_) Load();
}

void HomePage::Refresh() { Load(); }

void HomePage::Load() {
  if (loading_) return;
  const int generation = ++generation_;
  loading_ = true;
  loaded_ = false;
  gtk_widget_set_visible(spinner_, TRUE);
  gtk_spinner_start(GTK_SPINNER(spinner_));
  ShowStatus("go-home-symbolic", T(ctx_, "加载中…", "Loading…"), "",
             MakeSpinner());

  const std::string lang = ctx_.lang;
  ctx_.worker->Submit([this, generation, lang] {
    auto result = ctx_.api->HomeNews();
    std::vector<ListItem> items;
    std::string error;

    if (!result.ok) {
      error = result.error.ToString();
    } else if (result.value.is_object()) {
      std::vector<std::string> picks;
      if (result.value.contains("picks") &&
          result.value.at("picks").is_array()) {
        for (const auto& pick : result.value.at("picks")) {
          if (pick.is_string()) picks.push_back(pick.get<std::string>());
        }
      }

      // Anonymous titles are not localized; keep them as a fallback for picks
      // whose detail request fails.
      std::map<std::string, std::string> fallback;
      if (result.value.contains("items") &&
          result.value.at("items").is_array()) {
        for (const auto& entry : result.value.at("items")) {
          if (entry.is_object() && entry.contains("slug") &&
              entry.at("slug").is_string()) {
            fallback[entry.at("slug").get<std::string>()] =
                entry.value("title", std::string{});
          }
        }
      }

      for (const std::string& slug : picks) {
        auto detail = ctx_.api->Item("news", lang, slug);
        if (detail.ok && detail.value.is_object()) {
          items.push_back(ParseListItem(detail.value, "news"));
        } else {
          ListItem item;
          item.kind = "news";
          item.slug = slug;
          const auto found = fallback.find(slug);
          item.title = found == fallback.end() ? slug : found->second;
          item.raw = nlohmann::json::object();
          item.raw["slug"] = slug;
          item.raw["title"] = item.title;
          items.push_back(item);
        }
      }
    }

    PostToMainThread([this, generation, items, error] {
      if (generation != generation_) return;
      loading_ = false;
      loaded_ = true;
      items_ = items;
      if (!error.empty()) {
        ShowError(error);
        return;
      }
      Render();
    });
  });
}

void HomePage::Render() {
  gtk_spinner_stop(GTK_SPINNER(spinner_));
  gtk_widget_set_visible(spinner_, FALSE);
  ClearListBox(GTK_LIST_BOX(list_));
  if (items_.empty()) {
    ShowStatus("x-office-document-symbolic", T(ctx_, "暂无新闻", "No news yet"),
               T(ctx_, "稍后再来看看，或点击刷新。",
                  "Check back later, or refresh."));
    return;
  }

  for (std::size_t i = 0; i < items_.size(); ++i) {
    const ListItem& item = items_[i];
    const std::string subtitle = FormatDateField(item.raw);
    GtkWidget* row = MakeActionRow(item.title, subtitle, "x-office-document-symbolic");
    g_object_set_data(G_OBJECT(row), "index",
                      GINT_TO_POINTER(static_cast<int>(i)));
    g_signal_connect(row, "activated", G_CALLBACK(&HomePage::OnRowActivated),
                     this);
    gtk_list_box_append(GTK_LIST_BOX(list_), row);
  }
  gtk_stack_set_visible_child_name(GTK_STACK(root_), "content");
}

void HomePage::ShowStatus(const std::string& icon, const std::string& title,
                          const std::string& description, GtkWidget* child) {
  adw_status_page_set_icon_name(ADW_STATUS_PAGE(status_), icon.c_str());
  adw_status_page_set_title(ADW_STATUS_PAGE(status_), title.c_str());
  adw_status_page_set_description(ADW_STATUS_PAGE(status_), description.c_str());
  adw_status_page_set_child(ADW_STATUS_PAGE(status_), child);
  gtk_stack_set_visible_child_name(GTK_STACK(root_), "status");
}

void HomePage::ShowError(const std::string& message) {
  gtk_spinner_stop(GTK_SPINNER(spinner_));
  gtk_widget_set_visible(spinner_, FALSE);
  GtkWidget* retry = MakePillButton(T(ctx_, "重试", "Retry"),
                                    "view-refresh-symbolic",
                                    G_CALLBACK(&HomePage::OnRetry), this);
  ShowStatus("dialog-warning-symbolic",
             T(ctx_, "暂时无法获取新闻", "News is temporarily unavailable"),
             message, retry);
}

void HomePage::OnRowActivated(AdwActionRow* row, gpointer data) {
  auto* self = static_cast<HomePage*>(data);
  const int index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "index"));
  if (index < 0 || index >= static_cast<int>(self->items_.size())) return;
  OpenItemDetail(self->ctx_, self->items_[static_cast<std::size_t>(index)]);
}

void HomePage::OnRetry(GtkButton*, gpointer data) {
  static_cast<HomePage*>(data)->Load();
}

}  // namespace spark::gtk
