#include "gtk/widgets.h"

#include <adwaita.h>

#include <string>
#include <utility>

#include "util.h"

namespace spark::gtk {
namespace {

void OnLinkClicked(GtkButton* button, gpointer) {
  const auto* url =
      static_cast<const char*>(g_object_get_data(G_OBJECT(button), "url"));
  if (url != nullptr) OpenUrl(url);
}

}  // namespace

GtkWidget* MakeLabel(const std::string& text, const char* css, bool wrap) {
  GtkWidget* label = gtk_label_new(text.c_str());
  gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
  if (wrap) {
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
  }
  if (css != nullptr) gtk_widget_add_css_class(label, css);
  return label;
}

GtkWidget* MakeDimLabel(const std::string& text, bool wrap) {
  return MakeLabel(text, "dim-label", wrap);
}

GtkWidget* MakeHeading(const std::string& text) {
  return MakeLabel(text, "title-4", false);
}

GtkWidget* MakeBodyLabel(const std::string& text) {
  GtkWidget* label = MakeLabel(text, "body", true);
  gtk_label_set_selectable(GTK_LABEL(label), TRUE);
  return label;
}

GtkWidget* MakeSpinner() {
  GtkWidget* spinner = gtk_spinner_new();
  gtk_widget_set_size_request(spinner, 20, 20);
  gtk_spinner_start(GTK_SPINNER(spinner));
  return spinner;
}

GtkWidget* MakeScrolled(GtkWidget* child) {
  GtkWidget* scrolled = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_hexpand(scrolled, TRUE);
  gtk_widget_set_vexpand(scrolled, TRUE);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), child);
  return scrolled;
}

GtkWidget* MakeClamp(GtkWidget* child, int maximum) {
  GtkWidget* clamp = adw_clamp_new();
  adw_clamp_set_maximum_size(ADW_CLAMP(clamp), maximum);
  adw_clamp_set_child(ADW_CLAMP(clamp), child);
  return clamp;
}

GtkWidget* MakeStatusPage(const std::string& icon, const std::string& title,
                          const std::string& description) {
  GtkWidget* status = adw_status_page_new();
  adw_status_page_set_icon_name(ADW_STATUS_PAGE(status), icon.c_str());
  adw_status_page_set_title(ADW_STATUS_PAGE(status), title.c_str());
  adw_status_page_set_description(ADW_STATUS_PAGE(status), description.c_str());
  return status;
}

GtkWidget* MakeActionRow(const std::string& title, const std::string& subtitle,
                         const char* icon) {
  GtkWidget* row = adw_action_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title.c_str());
  if (!subtitle.empty()) {
    adw_action_row_set_subtitle(ADW_ACTION_ROW(row), subtitle.c_str());
  }
  if (icon != nullptr) {
    adw_action_row_add_prefix(ADW_ACTION_ROW(row),
                              gtk_image_new_from_icon_name(icon));
  }
  gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), TRUE);
  return row;
}

GtkWidget* MakeLinkButton(const std::string& label, const std::string& url) {
  GtkWidget* button = gtk_button_new_with_label(label.c_str());
  gtk_widget_add_css_class(button, "pill");
  gtk_widget_set_halign(button, GTK_ALIGN_START);
  g_object_set_data_full(G_OBJECT(button), "url", g_strdup(url.c_str()), g_free);
  g_signal_connect(button, "clicked", G_CALLBACK(&OnLinkClicked), nullptr);
  return button;
}

GtkWidget* MakePillButton(const std::string& label, const char* icon,
                          GCallback callback, gpointer data) {
  GtkWidget* button = gtk_button_new();
  gtk_widget_add_css_class(button, "pill");
  gtk_widget_set_halign(button, GTK_ALIGN_CENTER);
  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  if (icon != nullptr) {
    gtk_box_append(GTK_BOX(box), gtk_image_new_from_icon_name(icon));
  }
  gtk_box_append(GTK_BOX(box), gtk_label_new(label.c_str()));
  gtk_button_set_child(GTK_BUTTON(button), box);
  if (callback != nullptr) {
    g_signal_connect(button, "clicked", callback, data);
  }
  return button;
}

GtkWidget* MakeToolbarButton(const char* icon, const char* tooltip) {
  GtkWidget* button = gtk_button_new();
  gtk_widget_add_css_class(button, "flat");
  gtk_button_set_child(GTK_BUTTON(button),
                       gtk_image_new_from_icon_name(icon));
  if (tooltip != nullptr) gtk_widget_set_tooltip_text(button, tooltip);
  return button;
}

GtkWidget* MakeFieldList() {
  GtkWidget* list = gtk_list_box_new();
  gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
  gtk_list_box_set_show_separators(GTK_LIST_BOX(list), FALSE);
  gtk_widget_add_css_class(list, "boxed-list");
  return list;
}

void AddField(GtkWidget* list, const std::string& label,
              const std::string& value) {
  if (list == nullptr || value.empty()) return;
  GtkWidget* row = adw_action_row_new();
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), label.c_str());
  adw_action_row_set_subtitle(ADW_ACTION_ROW(row), value.c_str());
  adw_action_row_set_subtitle_selectable(ADW_ACTION_ROW(row), TRUE);
  gtk_list_box_append(GTK_LIST_BOX(list), row);
}

GtkWidget* MakeRawExpander(const nlohmann::json& data) {
  GtkWidget* expander = gtk_expander_new("底层数据");
  GtkWidget* label = gtk_label_new(data.dump(2).c_str());
  gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
  gtk_label_set_wrap(GTK_LABEL(label), TRUE);
  gtk_label_set_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
  gtk_label_set_selectable(GTK_LABEL(label), TRUE);
  gtk_widget_add_css_class(label, "monospace");
  gtk_widget_add_css_class(label, "dim-label");
  gtk_widget_set_margin_top(label, 8);
  gtk_widget_set_margin_start(label, 12);
  gtk_expander_set_child(GTK_EXPANDER(expander), label);
  return expander;
}

void ClearListBox(GtkListBox* box) {
  if (box == nullptr) return;
  GtkWidget* child = gtk_widget_get_first_child(GTK_WIDGET(box));
  while (child != nullptr) {
    GtkWidget* next = gtk_widget_get_next_sibling(child);
    gtk_list_box_remove(box, child);
    child = next;
  }
}

void SetListBoxPlaceholder(GtkListBox* box, const std::string& icon,
                           const std::string& title,
                           const std::string& description) {
  if (box == nullptr) return;
  GtkWidget* status = MakeStatusPage(icon, title, description);
  gtk_widget_add_css_class(status, "compact");
  gtk_list_box_set_placeholder(box, status);
}

}  // namespace spark::gtk
