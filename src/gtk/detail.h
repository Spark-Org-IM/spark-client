#pragma once

#include <gtk/gtk.h>

#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "gtk/page.h"

namespace spark::gtk {

// Hosts a detail page while its data is being fetched. The page is pushed
// immediately (so the user gets a back button and a spinner), then filled in
// from a worker thread. The handle self-invalidates when the page is popped
// and destroyed, which makes late worker results harmless.
class DetailHandle {
 public:
  explicit DetailHandle(std::string lang);
  ~DetailHandle();

  DetailHandle(const DetailHandle&) = delete;
  DetailHandle& operator=(const DetailHandle&) = delete;

  GtkWidget* Widget();
  bool Alive() const { return stack_ != nullptr; }
  void SetContent(GtkWidget* content);
  void SetError(const std::string& message);

 private:
  std::string lang_;
  GtkWidget* stack_ = nullptr;
};

// Fetches a news/contact/platform item and fills a newly pushed detail page.
void OpenItemDetail(const PageContext& ctx, const ListItem& item);
// Fetches a Flarum discussion and fills a newly pushed detail page.
void OpenForumThread(const PageContext& ctx, const ForumDiscussion& discussion);

// Builders used once the data is available. They return a scrolled widget.
GtkWidget* BuildItemDetail(const PageContext& ctx, const std::string& kind,
                           const std::string& title,
                           const nlohmann::json& data);
GtkWidget* BuildForumThread(const PageContext& ctx, const ForumThread& thread);
GtkWidget* BuildMeDetail(const PageContext& ctx, const nlohmann::json& data);

}  // namespace spark::gtk
