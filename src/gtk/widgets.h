#pragma once

#include <gtk/gtk.h>

#include <string>

#include <nlohmann/json.hpp>

namespace spark::gtk {

// Small widget helpers shared by every page. They intentionally stick to
// libadwaita's own style classes instead of inventing a custom theme.
GtkWidget* MakeLabel(const std::string& text, const char* css = nullptr,
                     bool wrap = false);
GtkWidget* MakeDimLabel(const std::string& text, bool wrap = false);
GtkWidget* MakeHeading(const std::string& text);
GtkWidget* MakeBodyLabel(const std::string& text);
GtkWidget* MakeSpinner();

GtkWidget* MakeScrolled(GtkWidget* child);
GtkWidget* MakeClamp(GtkWidget* child, int maximum = 720);
GtkWidget* MakeStatusPage(const std::string& icon, const std::string& title,
                          const std::string& description);
GtkWidget* MakeActionRow(const std::string& title, const std::string& subtitle,
                         const char* icon = nullptr);
GtkWidget* MakeLinkButton(const std::string& label, const std::string& url);
GtkWidget* MakePillButton(const std::string& label, const char* icon,
                          GCallback callback, gpointer data);
GtkWidget* MakeToolbarButton(const char* icon, const char* tooltip);

// A vertical list of read-only label/value rows. Add rows with AddField().
GtkWidget* MakeFieldList();
void AddField(GtkWidget* list, const std::string& label,
              const std::string& value);

// A collapsible dump of the underlying JSON, used by detail views.
GtkWidget* MakeRawExpander(const nlohmann::json& data);

void ClearListBox(GtkListBox* box);
void SetListBoxPlaceholder(GtkListBox* box, const std::string& icon,
                           const std::string& title,
                           const std::string& description);

}  // namespace spark::gtk
