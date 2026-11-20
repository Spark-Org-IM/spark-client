#include "gtk/pages/plugins_page.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "gtk/async.h"
#include "gtk/widgets.h"
#include "util.h"

namespace spark::gtk {
namespace {

struct FormState {
  PageContext ctx;
  PluginAction action;
  std::vector<GtkWidget*> inputs;
  GtkWidget* run = nullptr;
  GtkWidget* status = nullptr;
  bool alive = true;
};

using StatePtr = std::shared_ptr<FormState>;

StatePtr* MakeHolder(const StatePtr& state) { return new StatePtr(state); }

std::string ReadInput(GtkWidget* widget) {
  if (GTK_IS_TEXT_VIEW(widget)) {
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(widget));
    GtkTextIter start;
    GtkTextIter end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    char* text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    std::string value = text == nullptr ? std::string{} : text;
    g_free(text);
    return Trim(value);
  }
  const char* text = gtk_editable_get_text(GTK_EDITABLE(widget));
  return text == nullptr ? std::string{} : Trim(text);
}

StatePtr StateFor(GtkWidget* widget) {
  GtkWidget* ancestor =
      widget == nullptr ? nullptr
                        : gtk_widget_get_ancestor(widget, ADW_TYPE_DIALOG);
  if (ancestor == nullptr) return {};
  auto* holder = static_cast<StatePtr*>(
      g_object_get_data(G_OBJECT(ancestor), "plugin-form-state"));
  return holder == nullptr ? StatePtr{} : *holder;
}

void OnRunClicked(GtkButton* button, gpointer) {
  StatePtr state = StateFor(GTK_WIDGET(button));
  if (!state || !state->alive || state->run == nullptr) return;

  nlohmann::json input = nlohmann::json::object();
  for (std::size_t i = 0; i < state->action.fields.size(); ++i) {
    const PluginField& field = state->action.fields[i];
    const std::string value = ReadInput(state->inputs[i]);
    if (field.required && value.empty()) {
      gtk_label_set_text(GTK_LABEL(state->status),
                         (T(state->ctx, "请填写: ", "Required: ") + field.label)
                             .c_str());
      gtk_widget_grab_focus(state->inputs[i]);
      return;
    }
    input[field.key] = value;
  }

  PluginHost* plugins = state->ctx.plugins;
  Worker* worker = state->ctx.worker;
  if (plugins == nullptr || worker == nullptr) return;

  gtk_widget_set_sensitive(state->run, FALSE);
  gtk_label_set_text(GTK_LABEL(state->status),
                     T(state->ctx, "正在执行…", "Running…").c_str());

  const std::string pluginId = state->action.pluginId;
  const std::string actionId = state->action.id;
  worker->Submit([state, plugins, pluginId, actionId, input] {
    nlohmann::json result;
    std::string error;
    const bool ok = plugins->Run(pluginId, actionId, input, result, error);
    PostToMainThread([state, ok, result, error] {
      if (!state->alive) return;
      if (ok) {
        const std::string message = result.value("message", std::string{});
        const std::string refresh = result.value("refresh", std::string{});
        if (state->ctx.toast) {
          state->ctx.toast(message.empty()
                               ? T(state->ctx, "完成", "Done")
                               : message);
        }
        if (state->ctx.refresh && !refresh.empty()) {
          state->ctx.refresh(refresh);
        }
        GtkWidget* dialog =
            state->run == nullptr
                ? nullptr
                : gtk_widget_get_ancestor(state->run, ADW_TYPE_DIALOG);
        if (dialog != nullptr) adw_dialog_close(ADW_DIALOG(dialog));
        return;
      }
      if (state->run != nullptr) {
        gtk_widget_set_sensitive(state->run, TRUE);
      }
      if (state->status != nullptr) {
        gtk_label_set_text(GTK_LABEL(state->status),
                           (T(state->ctx, "执行失败: ", "Failed: ") + error)
                               .c_str());
      }
    });
  });
}

GtkWidget* MakeFieldInput(const PluginField& field, bool* multiline) {
  *multiline = field.type == "multiline";
  if (*multiline) {
    GtkWidget* view = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
    gtk_widget_add_css_class(view, "monospace");
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)),
                             field.initial.c_str(), -1);
    GtkWidget* scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scrolled, -1, 120);
    gtk_widget_add_css_class(scrolled, "card");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), view);
    return scrolled;
  }
  GtkWidget* entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(entry), field.label.c_str());
  gtk_editable_set_text(GTK_EDITABLE(entry), field.initial.c_str());
  return entry;
}

}  // namespace

bool HasVisiblePluginActions(const PageContext& ctx) {
  if (ctx.plugins == nullptr) return false;
  const std::vector<std::string> scopes =
      ctx.config == nullptr ? std::vector<std::string>{}
                            : ctx.config->token.scopes;
  for (const PluginAction& action : ctx.plugins->Actions()) {
    if (PluginScopesAllow(action, scopes)) return true;
  }
  return false;
}

PluginsPage::PluginsPage(PageContext ctx)
    : ctx_(std::move(ctx)),
      title_(T(ctx_, "插件", "Plugins")),
      actions_(ctx_.plugins == nullptr ? std::vector<PluginAction>{}
                                      : ctx_.plugins->Actions()) {
  const std::vector<std::string> scopes =
      ctx_.config == nullptr ? std::vector<std::string>{}
                             : ctx_.config->token.scopes;
  actions_.erase(
      std::remove_if(actions_.begin(), actions_.end(),
                     [&](const PluginAction& action) {
                       return !PluginScopesAllow(action, scopes);
                     }),
      actions_.end());
}

GtkWidget* PluginsPage::Widget() {
  if (root_ == nullptr) Build();
  return root_;
}

void PluginsPage::Build() {
  GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
  gtk_widget_set_margin_top(content, 24);
  gtk_widget_set_margin_bottom(content, 32);
  gtk_widget_set_margin_start(content, 18);
  gtk_widget_set_margin_end(content, 18);

  gtk_box_append(GTK_BOX(content), MakeLabel(title_, "title-1", false));
  gtk_box_append(
      GTK_BOX(content),
      MakeDimLabel(T(ctx_,
                     "这些动作由已安装的可选插件提供；请求由客户端代为发送。",
                     "These actions come from installed optional plugins; the "
                     "client proxies their requests."),
                   true));

  GtkWidget* group = adw_preferences_group_new();
  adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(group),
                                  T(ctx_, "可用动作", "Available actions").c_str());
  for (int i = 0; i < static_cast<int>(actions_.size()); ++i) {
    const PluginAction& action = actions_[i];
    std::string pluginName = action.pluginId;
    if (ctx_.plugins != nullptr) {
      for (const PluginInfo& info : ctx_.plugins->Plugins()) {
        if (info.id == action.pluginId) {
          pluginName = info.name;
          break;
        }
      }
    }
    std::string subtitle = action.description;
    if (!subtitle.empty()) subtitle += " · ";
    subtitle += pluginName;

    GtkWidget* row = MakeActionRow(action.title, subtitle,
                                   "application-x-addon-symbolic");
    g_object_set_data(G_OBJECT(row), "action-index", GINT_TO_POINTER(i + 1));
    g_signal_connect(row, "activated",
                     G_CALLBACK(&PluginsPage::OnActionActivated), this);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), row);
  }
  gtk_box_append(GTK_BOX(content), group);

  root_ = MakeScrolled(MakeClamp(content));
}

void PluginsPage::OnActionActivated(AdwActionRow* row, gpointer data) {
  auto* self = static_cast<PluginsPage*>(data);
  if (self == nullptr) return;
  const int index =
      GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "action-index")) - 1;
  if (index < 0 || index >= static_cast<int>(self->actions_.size())) return;
  self->ShowAction(self->actions_[index]);
}

void PluginsPage::ShowAction(const PluginAction& action) {
  StatePtr state = std::make_shared<FormState>();
  state->ctx = ctx_;
  state->action = action;

  AdwDialog* dialog = ADW_DIALOG(adw_dialog_new());
  adw_dialog_set_title(dialog, action.title.c_str());
  adw_dialog_set_content_width(dialog, 520);
  adw_dialog_set_content_height(dialog, 420);

  AdwToolbarView* toolbar = ADW_TOOLBAR_VIEW(adw_toolbar_view_new());
  AdwHeaderBar* header = ADW_HEADER_BAR(adw_header_bar_new());
  state->run = gtk_button_new_with_label(T(ctx_, "执行", "Run").c_str());
  gtk_widget_add_css_class(state->run, "suggested-action");
  adw_header_bar_pack_end(header, state->run);
  adw_toolbar_view_add_top_bar(toolbar, GTK_WIDGET(header));

  GtkWidget* body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(body, 18);
  gtk_widget_set_margin_bottom(body, 18);
  gtk_widget_set_margin_start(body, 18);
  gtk_widget_set_margin_end(body, 18);

  gtk_box_append(GTK_BOX(body), MakeDimLabel(action.description, true));

  for (const PluginField& field : action.fields) {
    GtkWidget* label = MakeLabel(
        field.label + (field.required ? " *" : ""), "heading", false);
    gtk_box_append(GTK_BOX(body), label);
    bool multiline = false;
    GtkWidget* input = MakeFieldInput(field, &multiline);
    state->inputs.push_back(multiline ? gtk_scrolled_window_get_child(
                                            GTK_SCROLLED_WINDOW(input))
                                      : input);
    gtk_box_append(GTK_BOX(body), input);
  }

  state->status = MakeDimLabel("", true);
  gtk_box_append(GTK_BOX(body), state->status);

  adw_toolbar_view_set_content(toolbar, MakeScrolled(body));
  adw_dialog_set_child(dialog, GTK_WIDGET(toolbar));

  g_object_set_data_full(
      G_OBJECT(dialog), "plugin-form-state", MakeHolder(state),
      +[](gpointer data) {
        auto* holder = static_cast<StatePtr*>(data);
        if (holder != nullptr) {
          if (*holder) {
            (*holder)->alive = false;
            (*holder)->run = nullptr;
            (*holder)->status = nullptr;
            (*holder)->inputs.clear();
          }
          delete holder;
        }
      });
  g_signal_connect(state->run, "clicked", G_CALLBACK(OnRunClicked), nullptr);

  adw_dialog_present(dialog, GTK_WIDGET(gtk_widget_get_root(root_)));
}

}  // namespace spark::gtk
