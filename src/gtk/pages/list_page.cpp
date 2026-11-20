#include "gtk/pages/list_page.h"

#include <string>
#include <utility>
#include <vector>

#include "api/models.h"
#include "gtk/detail.h"
#include "gtk/widgets.h"
#include "util.h"

namespace spark::gtk {
namespace {
constexpr int kPageSize = 20;
}

ListPage::ListPage(std::string id, std::string kind, std::string title,
                   std::string icon, PageContext ctx)
    : id_(std::move(id)),
      kind_(std::move(kind)),
      title_(std::move(title)),
      icon_(std::move(icon)),
      ctx_(std::move(ctx)) {}

GtkWidget* ListPage::Widget() {
  if (root_ == nullptr) Build();
  return root_;
}

void ListPage::Build() {
  root_ = gtk_stack_new();
  gtk_stack_set_transition_type(GTK_STACK(root_),
                                GTK_STACK_TRANSITION_TYPE_CROSSFADE);

  status_ = MakeStatusPage(icon_, title_,
                           T(ctx_, "正在加载…", "Loading…"));
  gtk_stack_add_named(GTK_STACK(root_), status_, "status");

  GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(content, 18);
  gtk_widget_set_margin_bottom(content, 24);
  gtk_widget_set_margin_start(content, 18);
  gtk_widget_set_margin_end(content, 18);

  gtk_box_append(GTK_BOX(content), MakeLabel(title_, "title-1", false));

  GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  count_label_ = MakeDimLabel("");
  gtk_widget_set_hexpand(count_label_, TRUE);
  gtk_box_append(GTK_BOX(header), count_label_);
  spinner_ = MakeSpinner();
  gtk_widget_set_visible(spinner_, FALSE);
  gtk_box_append(GTK_BOX(header), spinner_);
  gtk_box_append(GTK_BOX(content), header);

  list_ = gtk_list_box_new();
  gtk_list_box_set_selection_mode(GTK_LIST_BOX(list_), GTK_SELECTION_NONE);
  gtk_list_box_set_show_separators(GTK_LIST_BOX(list_), FALSE);
  gtk_widget_add_css_class(list_, "boxed-list");
  gtk_box_append(GTK_BOX(content), list_);

  load_more_ = MakePillButton(T(ctx_, "加载更多", "Load more"),
                              "go-down-symbolic", G_CALLBACK(&ListPage::OnLoadMore),
                              this);
  gtk_widget_set_halign(load_more_, GTK_ALIGN_CENTER);
  gtk_widget_set_visible(load_more_, FALSE);
  gtk_box_append(GTK_BOX(content), load_more_);

  gtk_stack_add_named(GTK_STACK(root_), MakeScrolled(MakeClamp(content)),
                      "content");
  gtk_stack_set_visible_child_name(GTK_STACK(root_), "status");
}

void ListPage::OnShown() {
  if (!loaded_ && !loading_) Load(false);
}

void ListPage::Refresh() { Load(false); }

void ListPage::Load(bool append) {
  if (loading_) return;
  const int generation = ++generation_;
  loading_ = true;
  if (!append) {
    items_.clear();
    total_ = 0;
    has_more_ = false;
    loaded_ = false;
    ShowStatus(icon_, T(ctx_, "加载中…", "Loading…"), "", MakeSpinner());
  }
  gtk_widget_set_visible(spinner_, TRUE);
  gtk_spinner_start(GTK_SPINNER(spinner_));

  const std::string kind = kind_;
  const std::string lang = ctx_.lang;
  const int offset = static_cast<int>(items_.size());

  ctx_.worker->Submit([this, generation, kind, lang, offset, append] {
    auto result = ctx_.api->List(kind, lang, kPageSize, offset);
    PostToMainThread([this, generation, append, result] {
      if (generation != generation_) return;
      loading_ = false;
      loaded_ = true;
      gtk_spinner_stop(GTK_SPINNER(spinner_));
      gtk_widget_set_visible(spinner_, FALSE);

      if (!result.ok) {
        if (append && !items_.empty()) {
          if (ctx_.toast) ctx_.toast(result.error.ToString());
        } else {
          ShowError(result.error.ToString());
        }
        return;
      }

      if (!append) items_.clear();
      for (const ListItem& item : result.value.items) {
        items_.push_back(item);
      }
      total_ = result.value.total;
      has_more_ = result.value.hasMore;
      Render();
    });
  });
}

void ListPage::Render() {
  ClearListBox(GTK_LIST_BOX(list_));
  if (items_.empty()) {
    ShowStatus(icon_, T(ctx_, "没有内容", "Nothing here"),
               T(ctx_, "稍后再来看看，或点击刷新。",
                  "Check back later, or refresh."));
    return;
  }

  for (std::size_t i = 0; i < items_.size(); ++i) {
    const ListItem& item = items_[i];
    GtkWidget* row = MakeActionRow(item.title, item.subtitle, icon_.c_str());
    g_object_set_data(G_OBJECT(row), "index",
                      GINT_TO_POINTER(static_cast<int>(i)));
    g_signal_connect(row, "activated", G_CALLBACK(&ListPage::OnRowActivated),
                     this);
    gtk_list_box_append(GTK_LIST_BOX(list_), row);
  }

  gtk_label_set_text(
      GTK_LABEL(count_label_),
      (T(ctx_, "共 ", "") + std::to_string(total_) +
       T(ctx_, " 条", " items"))
          .c_str());
  gtk_widget_set_visible(load_more_, has_more_);
  gtk_stack_set_visible_child_name(GTK_STACK(root_), "content");
}

void ListPage::ShowStatus(const std::string& icon, const std::string& title,
                          const std::string& description, GtkWidget* child) {
  adw_status_page_set_icon_name(ADW_STATUS_PAGE(status_), icon.c_str());
  adw_status_page_set_title(ADW_STATUS_PAGE(status_), title.c_str());
  adw_status_page_set_description(ADW_STATUS_PAGE(status_), description.c_str());
  adw_status_page_set_child(ADW_STATUS_PAGE(status_), child);
  gtk_stack_set_visible_child_name(GTK_STACK(root_), "status");
}

void ListPage::ShowError(const std::string& message) {
  GtkWidget* retry = MakePillButton(T(ctx_, "重试", "Retry"),
                                    "view-refresh-symbolic",
                                    G_CALLBACK(&ListPage::OnRetry), this);
  ShowStatus("dialog-warning-symbolic",
             T(ctx_, "加载失败", "Could not load"), message, retry);
}

void ListPage::OnRowActivated(AdwActionRow* row, gpointer data) {
  auto* self = static_cast<ListPage*>(data);
  const int index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "index"));
  if (index < 0 || index >= static_cast<int>(self->items_.size())) return;
  OpenItemDetail(self->ctx_, self->items_[static_cast<std::size_t>(index)]);
}

void ListPage::OnRetry(GtkButton*, gpointer data) {
  static_cast<ListPage*>(data)->Load(false);
}

void ListPage::OnLoadMore(GtkButton*, gpointer data) {
  static_cast<ListPage*>(data)->Load(true);
}

}  // namespace spark::gtk
