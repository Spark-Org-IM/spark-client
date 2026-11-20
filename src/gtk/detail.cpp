#include "gtk/detail.h"

#include <adwaita.h>

#include <string>
#include <utility>
#include <vector>

#include "api/models.h"
#include "forum/models.h"
#include "gtk/widgets.h"
#include "i18n.h"
#include "util.h"

namespace spark::gtk {
namespace {

std::string JsonString(const nlohmann::json& object, const char* key) {
  if (!object.is_object() || !object.contains(key) ||
      !object.at(key).is_string()) {
    return {};
  }
  return object.at(key).get<std::string>();
}

std::string Join(const std::vector<std::string>& parts,
                 const std::string& separator) {
  std::string out;
  for (const std::string& part : parts) {
    if (part.empty()) continue;
    if (!out.empty()) out += separator;
    out += part;
  }
  return out;
}

// Collects a string array from a JSON object. Non-string entries are skipped.
std::vector<std::string> StringList(const nlohmann::json& object,
                                    const char* key) {
  std::vector<std::string> values;
  if (!object.is_object() || !object.contains(key) ||
      !object.at(key).is_array()) {
    return values;
  }
  for (const auto& entry : object.at(key)) {
    if (entry.is_string()) values.push_back(entry.get<std::string>());
  }
  return values;
}

class Builder {
 public:
  explicit Builder(const PageContext&) {
    box_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
    gtk_widget_set_margin_top(box_, 24);
    gtk_widget_set_margin_bottom(box_, 32);
    gtk_widget_set_margin_start(box_, 18);
    gtk_widget_set_margin_end(box_, 18);
  }

  void AddTitle(const std::string& text) {
    if (text.empty()) return;
    GtkWidget* label = MakeLabel(text, "title-1", true);
    gtk_label_set_selectable(GTK_LABEL(label), TRUE);
    gtk_box_append(GTK_BOX(box_), label);
  }

  void AddMeta(const std::string& text) {
    if (text.empty()) return;
    gtk_box_append(GTK_BOX(box_), MakeDimLabel(text, true));
  }

  void AddHeading(const std::string& text) {
    if (text.empty()) return;
    GtkWidget* heading = MakeHeading(text);
    gtk_widget_set_margin_top(heading, 6);
    gtk_box_append(GTK_BOX(box_), heading);
  }

  void AddBody(const std::string& text) {
    if (text.empty()) return;
    gtk_box_append(GTK_BOX(box_), MakeBodyLabel(text));
  }

  void AddField(const std::string& label, const std::string& value) {
    if (value.empty()) return;
    if (fields_ == nullptr) {
      fields_ = MakeFieldList();
      gtk_box_append(GTK_BOX(box_), fields_);
    }
    spark::gtk::AddField(fields_, label, value);
  }

  void AddLink(const std::string& label, const std::string& url) {
    if (url.empty()) return;
    gtk_box_append(GTK_BOX(box_),
                   MakeLinkButton(label.empty() ? url : label, url));
  }

  void AddWidget(GtkWidget* widget) {
    if (widget != nullptr) gtk_box_append(GTK_BOX(box_), widget);
  }

  void AddRaw(const nlohmann::json& data) {
    gtk_box_append(GTK_BOX(box_), MakeRawExpander(data));
  }

  GtkWidget* Build() { return MakeScrolled(MakeClamp(box_)); }

 private:
  GtkWidget* box_ = nullptr;
  GtkWidget* fields_ = nullptr;
};

void AddStringListField(Builder& builder, const nlohmann::json& data,
                        const char* key, const char* zh, const char* en,
                        const std::string& lang) {
  const std::vector<std::string> values = StringList(data, key);
  if (values.empty()) return;
  builder.AddField(Tr(zh, en, lang), Join(values, ", "));
}

void AddObjectLinks(Builder& builder, const nlohmann::json& data,
                    const char* key) {
  if (!data.is_object() || !data.contains(key) ||
      !data.at(key).is_array()) {
    return;
  }
  for (const auto& entry : data.at(key)) {
    if (!entry.is_object()) continue;
    const std::string url = JsonString(entry, "url");
    if (url.empty()) continue;
    std::string name = JsonString(entry, "name");
    if (name.empty()) name = JsonString(entry, "alt");
    builder.AddLink(name.empty() ? url : name, url);
  }
}

struct PgpPayload {
  PageContext ctx;
  std::string key;
};

void OnImportPgp(GtkButton* button, gpointer) {
  auto* payload = static_cast<PgpPayload*>(
      g_object_get_data(G_OBJECT(button), "payload"));
  if (payload == nullptr || payload->ctx.worker == nullptr) return;
  const PageContext ctx = payload->ctx;
  const std::string key = payload->key;
  ctx.worker->Submit([ctx, key] {
    const bool ok = ImportPgpKey(key);
    PostToMainThread([ctx, ok] {
      if (!ctx.toast) return;
      ctx.toast(ok ? Tr("PGP 公钥已导入", "PGP key imported", ctx.lang)
                   : Tr("PGP 公钥导入失败", "Could not import PGP key",
                        ctx.lang));
    });
  });
}

void AddPgpImportButton(Builder& builder, const PageContext& ctx,
                        const std::string& key) {
  if (key.empty()) return;
  GtkWidget* button = MakePillButton(
      Tr("导入 PGP 公钥", "Import PGP key", ctx.lang), "document-open-symbolic",
      G_CALLBACK(&OnImportPgp), nullptr);
  auto* payload = new PgpPayload{ctx, key};
  g_object_set_data_full(G_OBJECT(button), "payload", payload,
                         +[](gpointer data) {
                           delete static_cast<PgpPayload*>(data);
                         });
  builder.AddWidget(button);
}

GtkWidget* BuildNews(const PageContext& ctx, const std::string& title,
                     const nlohmann::json& data) {
  Builder builder(ctx);
  const std::string heading = data.value("title", title);
  builder.AddTitle(heading);

  std::vector<std::string> meta;
  const std::string date = FormatDateField(data);
  if (!date.empty()) meta.push_back(date);
  if (data.contains("place") && data.at("place").is_object()) {
    meta.push_back(JsonString(data.at("place"), "desc"));
  }
  builder.AddMeta(Join(meta, " · "));

  const std::string body = StripHtml(JsonString(data, "desc"));
  if (!body.empty()) {
    builder.AddHeading(Tr("正文", "Body", ctx.lang));
    builder.AddBody(body);
  }

  if (data.contains("images") && data.at("images").is_array()) {
    for (const auto& image : data.at("images")) {
      if (!image.is_object()) continue;
      const std::string url = JsonString(image, "url");
      if (url.empty()) continue;
      std::string alt = JsonString(image, "alt");
      builder.AddLink(alt.empty() ? Tr("查看图片", "View image", ctx.lang) : alt,
                      url);
    }
  }

  if (data.contains("links") && data.at("links").is_array()) {
    for (const auto& link : data.at("links")) {
      if (!link.is_object()) continue;
      const std::string url = JsonString(link, "url");
      if (url.empty()) continue;
      std::string name = JsonString(link, "name");
      builder.AddLink(name.empty() ? url : name, url);
    }
  }

  builder.AddLink(Tr("打开原文", "Open source", ctx.lang),
                  JsonString(data, "url"));
  builder.AddRaw(data);
  return builder.Build();
}

GtkWidget* BuildContact(const PageContext& ctx, const std::string& title,
                        const nlohmann::json& data) {
  Builder builder(ctx);
  const std::string name = JsonString(data, "name");
  builder.AddTitle(name.empty() ? title : name);
  builder.AddMeta(JsonString(data, "title"));

  AddStringListField(builder, data, "tel", "电话", "Phone", ctx.lang);
  AddStringListField(builder, data, "telExtra", "备用电话", "Alt phone",
                     ctx.lang);
  AddStringListField(builder, data, "email", "邮箱", "Email", ctx.lang);
  AddStringListField(builder, data, "emailExtra", "备用邮箱", "Alt email",
                     ctx.lang);

  builder.AddField(Tr("PGP 指纹", "PGP fingerprint", ctx.lang),
                   JsonString(data, "pgpFingerprint"));

  AddObjectLinks(builder, data, "homepage");
  AddObjectLinks(builder, data, "homepageExtra");
  AddObjectLinks(builder, data, "social");

  AddPgpImportButton(builder, ctx, JsonString(data, "pgpKey"));
  builder.AddLink(Tr("打开原文", "Open source", ctx.lang),
                  JsonString(data, "url"));
  builder.AddRaw(data);
  return builder.Build();
}

GtkWidget* BuildPlatform(const PageContext& ctx, const std::string& title,
                         const nlohmann::json& data) {
  Builder builder(ctx);
  const std::string name = JsonString(data, "name");
  builder.AddTitle(name.empty() ? title : name);

  const std::string description = StripHtml(JsonString(data, "desc"));
  if (!description.empty()) {
    builder.AddHeading(Tr("简介", "Description", ctx.lang));
    builder.AddBody(description);
  }

  const std::string label = JsonString(data, "button");
  builder.AddLink(label.empty() ? Tr("打开", "Open", ctx.lang) : label,
                  JsonString(data, "url"));
  builder.AddLink(Tr("站内页面", "Site page", ctx.lang),
                  JsonString(data, "pageUrl"));
  builder.AddRaw(data);
  return builder.Build();
}

std::string DisplayName(const nlohmann::json& user) {
  for (const char* key : {"fullName", "display_name", "displayName"}) {
    const std::string value = JsonString(user, key);
    if (!value.empty()) return value;
  }
  return {};
}

}  // namespace

DetailHandle::DetailHandle(std::string lang) : lang_(std::move(lang)) {
  stack_ = gtk_stack_new();
  g_object_add_weak_pointer(G_OBJECT(stack_),
                            reinterpret_cast<gpointer*>(&stack_));

  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
  gtk_widget_set_hexpand(box, TRUE);
  gtk_widget_set_vexpand(box, TRUE);
  gtk_box_append(GTK_BOX(box), MakeSpinner());
  gtk_stack_add_named(GTK_STACK(stack_), box, "loading");
  gtk_stack_set_visible_child_name(GTK_STACK(stack_), "loading");
}

DetailHandle::~DetailHandle() {
  if (stack_ != nullptr) {
    g_object_remove_weak_pointer(G_OBJECT(stack_),
                                 reinterpret_cast<gpointer*>(&stack_));
  }
}

GtkWidget* DetailHandle::Widget() { return stack_; }

void DetailHandle::SetContent(GtkWidget* content) {
  if (content == nullptr) return;
  if (stack_ == nullptr) {
    g_object_ref_sink(content);
    g_object_unref(content);
    return;
  }
  gtk_stack_add_named(GTK_STACK(stack_), content, "content");
  gtk_stack_set_visible_child_name(GTK_STACK(stack_), "content");
}

void DetailHandle::SetError(const std::string& message) {
  if (stack_ == nullptr) return;
  GtkWidget* status =
      MakeStatusPage("dialog-warning-symbolic",
                     Tr("加载失败", "Could not load", lang_), message);
  gtk_stack_add_named(GTK_STACK(stack_), status, "error");
  gtk_stack_set_visible_child_name(GTK_STACK(stack_), "error");
}

void OpenItemDetail(const PageContext& ctx, const ListItem& item) {
  auto handle = std::make_shared<DetailHandle>(ctx.lang);
  if (ctx.push_detail) {
    ctx.push_detail(item.title.empty() ? Tr("详情", "Details", ctx.lang)
                                       : item.title,
                    handle->Widget());
  }
  const std::string kind = item.kind;
  const std::string slug = item.slug;
  const std::string title = item.title;
  ctx.worker->Submit([ctx, handle, kind, slug, title] {
    auto result = ctx.api->Item(kind, ctx.lang, slug);
    PostToMainThread([ctx, handle, kind, title, result] {
      if (!handle->Alive()) return;
      if (!result.ok) {
        handle->SetError(result.error.ToString());
        return;
      }
      handle->SetContent(BuildItemDetail(ctx, kind, title, result.value));
    });
  });
}

void OpenForumThread(const PageContext& ctx, const ForumDiscussion& discussion) {
  auto handle = std::make_shared<DetailHandle>(ctx.lang);
  if (ctx.push_detail) {
    ctx.push_detail(discussion.title.empty()
                        ? Tr("主题", "Discussion", ctx.lang)
                        : discussion.title,
                    handle->Widget());
  }
  const std::string id = discussion.id;
  ctx.worker->Submit([ctx, handle, id] {
    auto result = ctx.forum->Discussion(id);
    PostToMainThread([ctx, handle, result] {
      if (!handle->Alive()) return;
      if (!result.ok) {
        handle->SetError(result.error.ToString());
        return;
      }
      handle->SetContent(BuildForumThread(ctx, result.value));
    });
  });
}

GtkWidget* BuildItemDetail(const PageContext& ctx, const std::string& kind,
                           const std::string& title,
                           const nlohmann::json& data) {
  const std::string normalized = ToLower(kind);
  if (normalized == "news") return BuildNews(ctx, title, data);
  if (normalized == "contact") return BuildContact(ctx, title, data);
  if (normalized == "platform") return BuildPlatform(ctx, title, data);

  Builder builder(ctx);
  builder.AddTitle(title);
  builder.AddBody(data.dump(2));
  builder.AddRaw(data);
  return builder.Build();
}

GtkWidget* BuildForumThread(const PageContext& ctx, const ForumThread& thread) {
  Builder builder(ctx);
  builder.AddTitle(thread.discussion.title);

  std::vector<std::string> meta;
  meta.push_back(thread.discussion.author);
  meta.push_back(FormatForumDate(thread.discussion.lastPostedAt));
  if (thread.discussion.commentCount > 0) {
    meta.push_back(std::to_string(thread.discussion.commentCount) +
                   Tr(" 条回复", " replies", ctx.lang));
  }
  if (thread.discussion.sticky) meta.push_back(Tr("置顶", "Pinned", ctx.lang));
  if (thread.discussion.locked) meta.push_back(Tr("已锁定", "Locked", ctx.lang));
  builder.AddMeta(Join(meta, " · "));

  std::vector<std::string> tags;
  for (const std::string& tag : thread.discussion.tagNames) {
    if (!tag.empty()) tags.push_back("#" + tag);
  }
  builder.AddMeta(Join(tags, "  "));
  builder.AddLink(Tr("在浏览器打开", "Open in browser", ctx.lang),
                  thread.discussion.url);

  for (const ForumPost& post : thread.posts) {
    if (!post.contentType.empty() && post.contentType != "comment") continue;
    GtkWidget* card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(card, "card");
    gtk_widget_set_margin_top(card, 4);
    gtk_widget_set_margin_bottom(card, 4);

    GtkWidget* inner = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(inner, 14);
    gtk_widget_set_margin_bottom(inner, 14);
    gtk_widget_set_margin_start(inner, 16);
    gtk_widget_set_margin_end(inner, 16);

    GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* author = MakeLabel(post.author, nullptr, false);
    gtk_widget_add_css_class(author, "heading");
    gtk_widget_set_hexpand(author, TRUE);
    gtk_box_append(GTK_BOX(header), author);
    gtk_box_append(GTK_BOX(header), MakeDimLabel(FormatForumDate(post.createdAt)));
    gtk_box_append(GTK_BOX(inner), header);

    const std::string body = StripHtml(post.contentHtml);
    gtk_box_append(GTK_BOX(inner), MakeBodyLabel(body.empty() ? "…" : body));
    gtk_box_append(GTK_BOX(card), inner);
    builder.AddWidget(card);
  }

  return builder.Build();
}

GtkWidget* BuildMeDetail(const PageContext& ctx, const nlohmann::json& data) {
  const nlohmann::json* user = &data;
  if (data.is_object() && data.contains("user") &&
      data.at("user").is_object()) {
    user = &data.at("user");
  }

  Builder builder(ctx);
  const std::string username = JsonString(*user, "username");
  builder.AddTitle(username.empty() ? Tr("账号", "Account", ctx.lang) : username);

  std::vector<std::string> meta;
  meta.push_back(DisplayName(*user));
  meta.push_back(JsonString(*user, "role"));
  builder.AddMeta(Join(meta, " · "));

  builder.AddField(Tr("显示名", "Display name", ctx.lang), DisplayName(*user));
  builder.AddField(Tr("最近在线", "Last seen", ctx.lang),
                   JsonString(*user, "lastSeenAt"));
  builder.AddField(Tr("显示邮箱", "Display email", ctx.lang),
                   JsonString(*user, "displayEmail"));
  builder.AddField(Tr("声明邮箱", "Declared emails", ctx.lang),
                   Join(StringList(*user, "emails"), ", "));
  builder.AddField(Tr("PGP 指纹", "PGP fingerprint", ctx.lang),
                   JsonString(*user, "pgpFingerprint"));
  builder.AddField(Tr("PGP 算法", "PGP algorithm", ctx.lang),
                   JsonString(*user, "pgpAlgorithm"));
  builder.AddField(Tr("PGP 创建时间", "PGP created", ctx.lang),
                   JsonString(*user, "pgpCreatedAt"));

  if (ctx.config != nullptr && !ctx.config->forum.Empty()) {
    builder.AddHeading(Tr("论坛账号", "Forum account", ctx.lang));
    builder.AddField(Tr("用户名", "Username", ctx.lang),
                     ctx.config->forum.username);
    builder.AddField(Tr("显示名", "Display name", ctx.lang),
                     ctx.config->forum.displayName);
  }

  builder.AddRaw(data);
  return builder.Build();
}

}  // namespace spark::gtk
