#include "gtk/pages/me_page.h"

#include <string>
#include <utility>

#include "gtk/detail.h"
#include "gtk/widgets.h"

namespace spark::gtk {

MePage::MePage(PageContext ctx) : ctx_(std::move(ctx)) {}

GtkWidget* MePage::Widget() {
  if (root_ == nullptr) Build();
  return root_;
}

void MePage::Build() {
  root_ = gtk_stack_new();
  gtk_stack_set_transition_type(GTK_STACK(root_),
                                GTK_STACK_TRANSITION_TYPE_CROSSFADE);
  status_ = MakeStatusPage("avatar-default-symbolic", Title(), "");
  gtk_stack_add_named(GTK_STACK(root_), status_, "status");
  gtk_stack_set_visible_child_name(GTK_STACK(root_), "status");
}

void MePage::OnShown() {
  if (!loaded_ && !loading_) Load();
}

void MePage::Refresh() { Load(); }

void MePage::Load() {
  if (loading_) return;
  const int generation = ++generation_;
  loading_ = true;
  loaded_ = false;

  if (ctx_.config == nullptr || ctx_.config->token.Empty()) {
    loading_ = false;
    loaded_ = true;
    ShowStatus("avatar-default-symbolic", T(ctx_, "未登录", "Not signed in"),
               T(ctx_,
                  "请先在终端运行 spark-tui 并登录；GTK 登录界面将在后续版本"
                  "接入。",
                  "Sign in with spark-tui in a terminal first; the GTK sign-in "
                  "flow is coming in a later version."));
    return;
  }

  ShowStatus("avatar-default-symbolic", T(ctx_, "加载中…", "Loading…"), "",
             MakeSpinner());

  ctx_.worker->Submit([this, generation] {
    auto result = ctx_.api->Me();
    PostToMainThread([this, generation, result] {
      if (generation != generation_) return;
      loading_ = false;
      loaded_ = true;

      if (!result.ok) {
        if (result.error.status == 401) {
          ctx_.api->SetToken("");
          if (ctx_.config != nullptr) {
            ctx_.config->token = {};
            ctx_.config->Save();
          }
          ShowStatus("dialog-password-symbolic",
                     T(ctx_, "登录已过期", "Session expired"),
                     T(ctx_, "请重新运行 spark-tui 登录。",
                        "Sign in again with spark-tui."));
          return;
        }
        ShowError(result.error.ToString());
        return;
      }

      GtkWidget* old =
          gtk_stack_get_child_by_name(GTK_STACK(root_), "content");
      if (old != nullptr) gtk_stack_remove(GTK_STACK(root_), old);
      gtk_stack_add_named(GTK_STACK(root_),
                          BuildMeDetail(ctx_, result.value), "content");
      gtk_stack_set_visible_child_name(GTK_STACK(root_), "content");
    });
  });
}

void MePage::ShowStatus(const std::string& icon, const std::string& title,
                        const std::string& description, GtkWidget* child) {
  adw_status_page_set_icon_name(ADW_STATUS_PAGE(status_), icon.c_str());
  adw_status_page_set_title(ADW_STATUS_PAGE(status_), title.c_str());
  adw_status_page_set_description(ADW_STATUS_PAGE(status_), description.c_str());
  adw_status_page_set_child(ADW_STATUS_PAGE(status_), child);
  gtk_stack_set_visible_child_name(GTK_STACK(root_), "status");
}

void MePage::ShowError(const std::string& message) {
  GtkWidget* retry = MakePillButton(T(ctx_, "重试", "Retry"),
                                    "view-refresh-symbolic",
                                    G_CALLBACK(&MePage::OnRetry), this);
  ShowStatus("dialog-warning-symbolic",
             T(ctx_, "读取账号失败", "Could not load the account"), message,
             retry);
}

void MePage::OnRetry(GtkButton*, gpointer data) {
  static_cast<MePage*>(data)->Load();
}

}  // namespace spark::gtk
