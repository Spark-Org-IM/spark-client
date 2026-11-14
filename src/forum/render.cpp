#include "forum/render.h"

#include <algorithm>
#include <string>
#include <vector>

#include "i18n.h"
#include "util.h"

namespace spark {
namespace {

void AddWrapped(std::vector<std::string>& out, const std::string& text,
                int width, const std::string& prefix) {
  const int available = std::max(10, width - DisplayWidth(prefix));
  const auto lines = WrapText(text, static_cast<std::size_t>(available));
  if (lines.empty()) {
    out.push_back(prefix);
    return;
  }
  out.push_back(prefix + lines.front());
  const std::string pad(static_cast<std::size_t>(DisplayWidth(prefix)), ' ');
  for (std::size_t i = 1; i < lines.size(); ++i) {
    out.push_back(pad + lines[i]);
  }
}

}  // namespace

std::vector<std::string> RenderForumThreadLines(const ForumThread& thread,
                                                int width,
                                                const std::string& lang,
                                                bool include_title) {
  std::vector<std::string> out;
  const int wrap_width = std::max(20, width);

  if (include_title) {
    out.push_back(thread.discussion.title.empty()
                      ? Tr("(无标题)", "(untitled)", lang)
                      : thread.discussion.title);
  }

  std::string meta;
  if (!thread.discussion.tagNames.empty()) {
    meta += "#" + thread.discussion.tagNames.front();
    for (std::size_t i = 1; i < thread.discussion.tagNames.size(); ++i) {
      meta += " #" + thread.discussion.tagNames[i];
    }
  }
  if (!thread.discussion.author.empty()) {
    if (!meta.empty()) meta += "  ";
    meta += Tr("作者: ", "by ", lang) + thread.discussion.author;
  }
  const std::string date = FormatForumDate(thread.discussion.createdAt);
  if (!date.empty()) {
    if (!meta.empty()) meta += "  ";
    meta += date;
  }
  if (thread.discussion.locked) {
    if (!meta.empty()) meta += "  ";
    meta += Tr("已锁定", "locked", lang);
  }
  if (!meta.empty()) out.push_back(meta);
  if (!thread.discussion.url.empty()) {
    out.push_back(Tr("原文: ", "Source: ", lang) + thread.discussion.url);
  }
  out.emplace_back();

  bool has_comment = false;
  for (const ForumPost& post : thread.posts) {
    if (post.contentType != "comment") continue;
    has_comment = true;

    std::string heading = "#" + std::to_string(post.number);
    if (!post.author.empty()) heading += "  " + post.author;
    const std::string post_date = FormatForumDate(post.createdAt);
    if (!post_date.empty()) heading += "  " + post_date;
    if (post.edited) heading += Tr("  (已编辑)", "  (edited)", lang);
    out.push_back(heading);

    std::string body = StripHtml(post.contentHtml);
    if (body.empty()) body = Tr("(无内容)", "(no content)", lang);
    AddWrapped(out, body, wrap_width, "  ");
    out.emplace_back();
  }

  if (!has_comment) {
    out.push_back(Tr("(暂无回复)", "(no replies yet)", lang));
  }

  while (!out.empty() && out.back().empty()) out.pop_back();
  if (out.empty()) out.push_back(Tr("(无内容)", "(no content)", lang));
  return out;
}

}  // namespace spark
