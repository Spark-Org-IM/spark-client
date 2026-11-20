#include "gtk/pages/forum_page.h"

#include <string>
#include <utility>
#include <vector>

#include "gtk/detail.h"
#include "gtk/widgets.h"
#include "util.h"

namespace spark::gtk {
namespace {
constexpr int kPageSize = 20;
}

ForumPage::ForumPage(PageContext ctx) : ctx_(std::move(ctx)) {}

std::string ForumPage::Title() const {
  return T(ctx_, "论坛", "Forum");
}

GtkWidget* ForumPage::Widget() {
  if (root_ == nullptr) Build();
  return root_;
}

void ForumPage::Build() {
  root_ = gtk_stack_new();
  gtk_stack_set_transition_type(GTK_STACK(root_),
                                GTK_STACK_TRANSITION_TYPE_CROSSFADE);

  status_ = MakeStatusPage("chat-message-new-symbolic", Title(),
                           T(ctx_, "正在加载讨论…", "Loading discussions…"));
  gtk_stack_add_named(GTK_STACK(root_), status_, "status");

  GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(content, 18);
  gtk_widget_set_margin_bottom(content, 24);
  gtk_widget_set_margin_start(content, 18);
  gtk_widget_set_margin_end(content, 18);

  gtk_box_append(GTK_BOX(content), MakeLabel(Title(), "title-1", false));

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

  load_more_ = MakePillButton(
      T(ctx_, "加载更多", "Load more"), "go-down-symbolic",
      G_CALLBACK(&ForumPage::OnLoadMore), this);
  gtk_widget_set_halign(load_more_, GTK_ALIGN_CENTER);
  gtk_widget_set_visible(load_more_, FALSE);
  gtk_box_append(GTK_BOX(content), load_more_);

  gtk_stack_add_named(GTK_STACK(root_), MakeScrolled(MakeClamp(content)),
                      "content");
  gtk_stack_set_visible_child_name(GTK_STACK(root_), "status");
}

void ForumPage::OnShown() {
  if (!loaded_ && !loading_) Load(false);
}

void ForumPage::Refresh() { Load(false); }

void ForumPage::Load(bool append) {
  if (loading_) return;
  const int generation = ++generation_;
  loading_ = true;
  if (!append) {
    items_.clear();
    next_.clear();
    has_more_ = false;
    loaded_ = false;
    ShowStatus("chat-message-new-symbolic", T(ctx_, "加载中…", "Loading…"), "",
               MakeSpinner());
  }
  gtk_widget_set_visible(spinner_, TRUE);
  gtk_spinner_start(GTK_SPINNER(spinner_));

  const std::string next = append ? next_ : std::string();
  ctx_.worker->Submit([this, generation, next, append] {
    auto result = ctx_.forum->Discussions(kPageSize, next);
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
      for (const ForumDiscussion& item : result.value.items) {
        items_.push_back(item);
      }
      next_ = result.value.next;
      has_more_ = result.value.hasMore;
      Render();
    });
  });
}

void ForumPage::Render() {
  ClearListBox(GTK_LIST_BOX(list_));
  if (items_.empty()) {
    ShowStatus("chat-message-new-symbolic",
               T(ctx_, "没有讨论", "No discussions yet"),
               T(ctx_, "论坛可能还没有内容，或服务暂时不可用。",
                  "The forum may be empty, or the service is unavailable."));
    return;
  }

  for (std::size_t i = 0; i < items_.size(); ++i) {
    const ForumDiscussion& item = items_[i];
    std::vector<std::string> meta;
    meta.push_back(item.author);
    if (item.commentCount > 0) {
      meta.push_back(std::to_string(item.commentCount) +
                     T(ctx_, " 条回复", " replies"));
    }
    if (item.sticky) meta.push_back(T(ctx_, "置顶", "Pinned"));
    if (item.locked) meta.push_back(T(ctx_, "锁定", "Locked"));

    std::string subtitle;
    for (const std::string& part : meta) {
      if (part.empty()) continue;
      if (!subtitle.empty()) subtitle += " · ";
      subtitle += part;
    }

    GtkWidget* row = MakeActionRow(item.title, subtitle, "chat-message-new-symbolic");
    g_object_set_data(G_OBJECT(row), "index",
                      GINT_TO_POINTER(static_cast<int>(i)));
    g_signal_connect(row, "activated", G_CALLBACK(&ForumPage::OnRowActivated),
                     this);
    gtk_list_box_append(GTK_LIST_BOX(list_), row);
  }

  gtk_label_set_text(
      GTK_LABEL(count_label_),
      (T(ctx_, "已加载 ", "Loaded ") + std::to_string(items_.size()) +
       T(ctx_, " 条讨论", " discussions"))
          .c_str());
  gtk_widget_set_visible(load_more_, has_more_);
  gtk_stack_set_visible_child_name(GTK_STACK(root_), "content");
}

void ForumPage::ShowStatus(const std::string& icon, const std::string& title,
                           const std::string& description, GtkWidget* child) {
  adw_status_page_set_icon_name(ADW_STATUS_PAGE(status_), icon.c_str());
  adw_status_page_set_title(ADW_STATUS_PAGE(status_), title.c_str());
  adw_status_page_set_description(ADW_STATUS_PAGE(status_), description.c_str());
  adw_status_page_set_child(ADW_STATUS_PAGE(status_), child);
  gtk_stack_set_visible_child_name(GTK_STACK(root_), "status");
}

void ForumPage::ShowError(const std::string& message) {
  GtkWidget* retry = MakePillButton(T(ctx_, "重试", "Retry"),
                                    "view-refresh-symbolic",
                                    G_CALLBACK(&ForumPage::OnRetry), this);
  ShowStatus("dialog-warning-symbolic",
             T(ctx_, "无法加载论坛", "Could not load the forum"), message,
             retry);
}

void ForumPage::OnRowActivated(AdwActionRow* row, gpointer data) {
  auto* self = static_cast<ForumPage*>(data);
  const int index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "index"));
  if (index < 0 || index >= static_cast<int>(self->items_.size())) return;
  OpenForumThread(self->ctx_,
                  self->items_[static_cast<std::size_t>(index)]);
}

void ForumPage::OnRetry(GtkButton*, gpointer data) {
  static_cast<ForumPage*>(data)->Load(false);
}

void ForumPage::OnLoadMore(GtkButton*, gpointer data) {
  static_cast<ForumPage*>(data)->Load(true);
}

}  // namespace spark::gtk
