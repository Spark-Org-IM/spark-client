#include "ui/app.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <map>
#include <string>
#include <thread>
#include <utility>

#include <ftxui/component/component.hpp>

#include "service_source.h"
#include "i18n.h"
#include "forum/render.h"
#include "ui/render_detail.h"
#include "util.h"

namespace spark {

using namespace ftxui;

namespace {

constexpr int kPageSize = 20;

bool Contains(const std::vector<std::string>& values, const std::string& value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

std::string MaskPassword(const std::string& password) {
  std::string masked;
  for (std::size_t i = 0; i < password.size();) {
    char32_t cp = 0;
    const std::size_t consumed = Utf8Decode(password, i, cp);
    if (consumed == 0) break;
    masked.push_back('*');
    i += consumed;
  }
  return masked;
}

Element ScrollLines(const std::vector<std::string>& lines, int& scroll,
                    int height) {
  const int max_scroll =
      std::max(0, static_cast<int>(lines.size()) - std::max(1, height));
  scroll = std::clamp(scroll, 0, max_scroll);

  Elements view;
  for (int i = 0; i < height && scroll + i < static_cast<int>(lines.size());
       ++i) {
    view.push_back(text(lines[static_cast<std::size_t>(scroll + i)]));
  }
  if (view.empty()) view.push_back(text("—"));
  return vbox(std::move(view));
}

}  // namespace

App::App(Options opts, Config cfg)
    : opts_(std::move(opts)),
      cfg_(std::move(cfg)),
      api_(cfg_.EffectiveMain(), cfg_.insecure, cfg_.token.plaintext),
      forumApi_(cfg_.EffectiveForum(), cfg_.insecure, cfg_.forum.token),
      worker_([this] { screen_.PostEvent(Event::Custom); }, 4) {}

int App::Run() {
  LoadSiteSync();

  lang_ = ResolveLanguageTag(cfg_.language, site_.languages,
                             site_.defaultLanguage);
  if (lang_.empty()) lang_ = "zh-cn";

  // The header shows "signed in" immediately; the display name arrives once
  // /me.json has loaded in the background.
  if (LoggedIn()) LoadMe();

  // Start on the home dashboard; news loads only when the user opens it.
  tab_ = Tab::Home;
  LoadHome();

  auto renderer = Renderer([this] { return Render(); });
  auto root = CatchEvent(renderer, [this](Event event) {
    return HandleEvent(std::move(event));
  });

  StartStartupSync();

  screen_.Loop(root);
  worker_.Stop();
  return 0;
}

// ---------------------------------------------------------------------------
// Data loading
// ---------------------------------------------------------------------------

void App::LoadSiteSync() {
  auto result = api_.Site();
  if (!result.ok) {
    SetStatus(T("无法加载站点: ", "Could not load site: ") +
              result.error.ToString());
    if (site_.languages.empty()) {
      site_.name = "Spark ORG";
      site_.defaultLanguage = "zh-cn";
      site_.languages = {"zh-cn", "en-us"};
    }
    return;
  }
  site_ = result.value;
  if (site_.languages.empty()) site_.languages.push_back(site_.defaultLanguage);
}

void App::LoadHome() {
  ++home_.generation;
  const int generation = home_.generation;
  const std::string lang = lang_;

  home_.loading = true;
  worker_.Submit([this, generation, lang] {
    auto result = api_.HomeNews();
    std::vector<ListItem> items;
    std::string error;

    if (!result.ok) {
      error = result.error.ToString();
    } else if (result.value.is_object()) {
      std::vector<std::string> picks;
      if (result.value.contains("picks") && result.value.at("picks").is_array()) {
        for (const auto& pick : result.value.at("picks")) {
          if (pick.is_string()) picks.push_back(pick.get<std::string>());
        }
      }

      // Anonymous titles are not localized; remember them as a fallback for
      // picks whose detail request fails.
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
        auto detail = api_.Item("news", lang, slug);
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

    worker_.Post([this, generation, lang, items, error] {
      if (generation != home_.generation) return;
      home_.loading = false;
      if (lang != lang_) return;

      home_.loaded = true;
      home_.error = error;
      home_.news = items;
      home_.total = static_cast<int>(items.size());
    });
  });
}

void App::OpenHomeManage() {
  if (!LoggedIn()) {
    SetStatus(T("管理主页需要先登录（按 L）",
                "Sign in first to manage the home page (press L)"));
    return;
  }
  homeManage_ = HomeManageState{};
  mode_ = Mode::HomeManage;
  LoadHomeManage();
}

void App::LoadHomeManage() {
  ++homeManage_.generation;
  const int generation = homeManage_.generation;
  homeManage_.loading = true;
  homeManage_.error.clear();

  worker_.Submit([this, generation] {
    auto result = api_.HomeNews();
    worker_.Post([this, generation, result] {
      if (generation != homeManage_.generation) return;
      homeManage_.loading = false;
      if (!result.ok) {
        homeManage_.error = result.error.ToString();
        if (result.error.status == 403) {
          homeManage_.error = T("当前账号没有主页管理权限",
                                "This account cannot manage the home page");
        }
        return;
      }

      const auto& data = result.value;
      homeManage_.max = data.value("max", 5);
      std::map<std::string, std::string> titles;
      if (data.contains("items") && data.at("items").is_array()) {
        for (const auto& item : data.at("items")) {
          if (!item.is_object()) continue;
          const std::string slug = item.value("slug", std::string{});
          if (!slug.empty()) {
            titles[slug] = item.value("title", slug);
          }
        }
      }
      if (data.contains("candidates") && data.at("candidates").is_array()) {
        for (const auto& item : data.at("candidates")) {
          if (!item.is_object()) continue;
          const std::string slug = item.value("slug", std::string{});
          if (slug.empty()) continue;
          titles[slug] = item.value("title", slug);
        }
      } else {
        homeManage_.error =
            T("当前账号没有主页管理权限", "This account cannot manage the home page");
        homeManage_.items.clear();
        return;
      }

      std::vector<HomePickEntry> entries;
      std::map<std::string, bool> picked;
      if (data.contains("picks") && data.at("picks").is_array()) {
        for (const auto& pick : data.at("picks")) {
          if (!pick.is_string()) continue;
          const std::string slug = pick.get<std::string>();
          picked[slug] = true;
          entries.push_back({slug, titles.count(slug) ? titles[slug] : slug, true});
        }
      }
      if (data.contains("candidates") && data.at("candidates").is_array()) {
        for (const auto& item : data.at("candidates")) {
          if (!item.is_object()) continue;
          const std::string slug = item.value("slug", std::string{});
          if (slug.empty() || picked.count(slug)) continue;
          entries.push_back({slug, item.value("title", slug), false});
        }
      }

      homeManage_.items = std::move(entries);
      if (homeManage_.selected >=
          static_cast<int>(homeManage_.items.size())) {
        homeManage_.selected =
            std::max(0, static_cast<int>(homeManage_.items.size()) - 1);
      }
    });
  });
}

void App::ToggleHomePick() {
  if (homeManage_.busy || homeManage_.loading) return;
  if (homeManage_.items.empty() || homeManage_.selected < 0 ||
      homeManage_.selected >=
          static_cast<int>(homeManage_.items.size())) {
    return;
  }

  const HomePickEntry entry =
      homeManage_.items[static_cast<std::size_t>(homeManage_.selected)];
  int picked_count = 0;
  for (const auto& item : homeManage_.items) {
    if (item.picked) ++picked_count;
  }
  if (!entry.picked && picked_count >= homeManage_.max) {
    homeManage_.message =
        T("最多只能置顶 ", "At most ") + std::to_string(homeManage_.max) +
        T(" 条新闻", " stories can be pinned");
    return;
  }

  homeManage_.busy = true;
  homeManage_.message = entry.picked
                            ? T("正在取消置顶…", "Removing the pin…")
                            : T("正在置顶…", "Pinning the story…");
  const int generation = homeManage_.generation;
  worker_.Submit([this, entry, generation] {
    auto result = entry.picked ? api_.HomeNewsRemove(entry.slug)
                               : api_.HomeNewsAdd(entry.slug);
    worker_.Post([this, generation, result, entry] {
      if (generation != homeManage_.generation) return;
      homeManage_.busy = false;
      if (!result.ok) {
        homeManage_.message =
            T("操作失败: ", "Operation failed: ") + result.error.ToString();
        return;
      }
      homeManage_.message = entry.picked
                                ? T("已取消置顶", "Pin removed")
                                : T("已置顶", "Story pinned");
      home_ = HomeState{};
      LoadHomeManage();
    });
  });
}

void App::ReorderHomePicks(int direction) {
  if (homeManage_.busy || homeManage_.loading) return;
  const int selected = homeManage_.selected;
  if (selected < 0 || selected >= static_cast<int>(homeManage_.items.size())) {
    return;
  }
  if (!homeManage_.items[static_cast<std::size_t>(selected)].picked) {
    homeManage_.message =
        T("只有置顶新闻可以调整顺序", "Only pinned stories can be reordered");
    return;
  }

  int picked_count = 0;
  for (const auto& item : homeManage_.items) {
    if (item.picked) ++picked_count;
  }
  const int target = selected + direction;
  if (target < 0 || target >= picked_count) return;

  std::swap(homeManage_.items[static_cast<std::size_t>(selected)],
            homeManage_.items[static_cast<std::size_t>(target)]);
  homeManage_.selected = target;

  std::vector<std::string> picks;
  for (int i = 0; i < picked_count; ++i) {
    picks.push_back(homeManage_.items[static_cast<std::size_t>(i)].slug);
  }

  homeManage_.busy = true;
  homeManage_.message = T("正在调整置顶顺序…", "Reordering pins…");
  const int generation = homeManage_.generation;
  worker_.Submit([this, picks, generation] {
    auto result = api_.HomeNewsReorder(picks);
    worker_.Post([this, generation, result] {
      if (generation != homeManage_.generation) return;
      homeManage_.busy = false;
      if (!result.ok) {
        homeManage_.message =
            T("调整失败: ", "Reorder failed: ") + result.error.ToString();
        LoadHomeManage();
        return;
      }
      homeManage_.message = T("置顶顺序已更新", "Pin order updated");
      home_ = HomeState{};
      LoadHomeManage();
    });
  });
}

void App::LoadList(Tab tab, bool append) {
  if (tab == Tab::Search || tab == Tab::Me || tab == Tab::Home ||
      tab == Tab::Forum) {
    return;
  }

  ListState& list = ListFor(tab);
  if (list.loading) return;

  const std::string kind = tab == Tab::News       ? "news"
                           : tab == Tab::Contact  ? "contact"
                                                  : "platform";
  const std::string lang = lang_;
  const int offset = append ? static_cast<int>(list.items.size()) : 0;

  ++list.generation;
  const int generation = list.generation;
  list.loading = true;
  if (!append) {
    list.items.clear();
    list.selected = 0;
    list.hasMore = false;
  }

  worker_.Submit([this, tab, kind, lang, offset, generation, append] {
    auto result = api_.List(kind, lang, kPageSize, offset);
    worker_.Post([this, tab, lang, generation, result, append] {
      ListState& current = ListFor(tab);
      if (generation != current.generation) return;
      current.loading = false;
      if (lang != lang_) return;
      if (!result.ok) {
        SetStatus(T("加载失败: ", "Load failed: ") + result.error.ToString());
        return;
      }
      if (!append) current.items.clear();
      for (const auto& item : result.value.items) current.items.push_back(item);
      current.total = result.value.total;
      current.hasMore = result.value.hasMore;
      current.loaded = true;
      if (current.selected >= static_cast<int>(current.items.size())) {
        current.selected =
            std::max(0, static_cast<int>(current.items.size()) - 1);
      }
    });
  });
}

void App::LoadMe() {
  if (!LoggedIn()) {
    me_ = nlohmann::json::object();
    return;
  }
  meLoading_ = true;
  worker_.Submit([this] {
    auto result = api_.Me();
    worker_.Post([this, result] {
      meLoading_ = false;
      if (!result.ok) {
        SetStatus(T("读取账号失败: ", "Could not load account: ") +
                  result.error.ToString());
        if (result.error.status == 401) {
          api_.SetToken("");
          cfg_.token = {};
          SaveConfig();
        }
        return;
      }
      me_ = result.value;
      const nlohmann::json* user = &me_;
      if (me_.is_object() && me_.contains("user") &&
          me_.at("user").is_object()) {
        user = &me_.at("user");
      }
      if (user->is_object() && user->contains("username") &&
          user->at("username").is_string()) {
        cfg_.token.username = user->at("username").get<std::string>();
        SaveConfig();
      }
    });
  });
}

void App::LoadForum(bool append) {
  if (forum_.loading) return;

  const std::string next = append ? forum_.next : std::string{};
  ++forum_.generation;
  const int generation = forum_.generation;
  forum_.loading = true;
  forum_.error.clear();
  if (!append) {
    forum_.items.clear();
    forum_.selected = 0;
    forum_.hasMore = false;
    forum_.next.clear();
  }

  worker_.Submit([this, next, generation] {
    auto result = forumApi_.Discussions(kPageSize, next);
    worker_.Post([this, generation, result] {
      if (generation != forum_.generation) return;
      forum_.loading = false;
      if (!result.ok && result.error.status == 401 && ForumLoggedIn()) {
        cfg_.forum = {};
        forumApi_.SetToken("");
        SaveConfig();
        SetStatus(T("论坛登录已失效，已转为匿名浏览",
                    "Forum sign-in expired; browsing anonymously"));
        LoadForum(false);
        return;
      }
      if (!result.ok) {
        forum_.error = result.error.ToString();
        SetStatus(T("论坛加载失败: ", "Could not load the forum: ") +
                  result.error.ToString());
        return;
      }
      if (forum_.items.empty()) {
        forum_.items = result.value.items;
      } else {
        for (const auto& item : result.value.items) {
          forum_.items.push_back(item);
        }
      }
      forum_.next = result.value.next;
      forum_.hasMore = result.value.hasMore;
      forum_.loaded = true;
      if (forum_.selected >= static_cast<int>(forum_.items.size())) {
        forum_.selected =
            std::max(0, static_cast<int>(forum_.items.size()) - 1);
      }
    });
  });
}

void App::OpenForumDiscussion() {
  if (forum_.items.empty() || forum_.selected < 0 ||
      forum_.selected >= static_cast<int>(forum_.items.size())) {
    return;
  }
  OpenForumThread(forum_.items[static_cast<std::size_t>(forum_.selected)]);
}

void App::OpenForumThread(const ForumDiscussion& discussion) {
  mode_ = Mode::ForumDetail;
  ++forumThread_.generation;
  const int generation = forumThread_.generation;
  forumThread_ = ForumThreadState{};
  forumThread_.generation = generation;
  forumThread_.thread.discussion = discussion;
  forumThread_.loading = true;
  status_.clear();

  worker_.Submit([this, discussion, generation] {
    auto result = forumApi_.Discussion(discussion.id);
    worker_.Post([this, generation, result] {
      if (generation != forumThread_.generation) return;
      forumThread_.loading = false;
      if (!result.ok && result.error.status == 401 && ForumLoggedIn()) {
        cfg_.forum = {};
        forumApi_.SetToken("");
        SaveConfig();
        SetStatus(T("论坛登录已失效，已转为匿名浏览",
                    "Forum sign-in expired; browsing anonymously"));
        OpenForumDiscussion();
        return;
      }
      if (!result.ok) {
        forumThread_.error = result.error.ToString();
        SetStatus(T("主题加载失败: ", "Could not load the discussion: ") +
                  result.error.ToString());
        return;
      }
      forumThread_.thread = result.value;
    });
  });
}

std::vector<App::ForumAction> App::ForumActions() const {
  std::vector<ForumAction> actions;
  const ForumDiscussion& discussion = forumManage_.discussion;
  if (discussion.canSticky) actions.push_back(ForumAction::Sticky);
  if (discussion.canLock) actions.push_back(ForumAction::Lock);
  if (discussion.canHide) actions.push_back(ForumAction::Hide);
  if (discussion.canTag) actions.push_back(ForumAction::Tags);
  if (discussion.canRename) actions.push_back(ForumAction::Rename);
  if (discussion.canDelete) actions.push_back(ForumAction::Delete);
  return actions;
}

void App::OpenForumManage(bool from_detail) {
  if (!ForumLoggedIn()) {
    SetStatus(T("管理论坛需要先登录论坛账号",
                "Sign in to the forum to manage it"));
    return;
  }

  const ForumDiscussion* discussion = nullptr;
  if (from_detail) {
    discussion = &forumThread_.thread.discussion;
  } else if (!forum_.items.empty() && forum_.selected >= 0 &&
             forum_.selected < static_cast<int>(forum_.items.size())) {
    discussion = &forum_.items[static_cast<std::size_t>(forum_.selected)];
  }
  if (discussion == nullptr) return;

  forumManage_ = ForumManageState{};
  forumManage_.discussion = *discussion;
  forumManage_.from_detail = from_detail;
  mode_ = Mode::ForumManage;
  status_.clear();
}

void App::RunForumAction(ForumAction action) {
  if (forumManage_.busy) return;
  forumManage_.confirm = 0;

  if (action == ForumAction::Rename) {
    forumManage_.renaming = true;
    forumManage_.title = forumManage_.discussion.title;
    forumManage_.title_cursor = forumManage_.title.size();
    forumManage_.message.clear();
    return;
  }
  if (action == ForumAction::Tags) {
    StartForumTagEdit();
    return;
  }

  forumManage_.busy = true;
  const std::string id = forumManage_.discussion.id;
  const bool from_detail = forumManage_.from_detail;
  const int generation = ++forumManage_.generation;

  if (action == ForumAction::Delete) {
    forumManage_.message = T("正在删除主题…", "Deleting the discussion…");
    worker_.Submit([this, id, generation, from_detail] {
      auto result = forumApi_.DeleteDiscussion(id);
      worker_.Post([this, generation, result, from_detail] {
        if (generation != forumManage_.generation) return;
        forumManage_.busy = false;
        if (!result.ok) {
          forumManage_.message =
              T("删除失败: ", "Delete failed: ") + result.error.ToString();
          return;
        }
        mode_ = Mode::Browse;
        forum_ = ForumState{};
        forumThread_ = ForumThreadState{};
        LoadForum(false);
        SetStatus(T("主题已删除", "Discussion deleted"));
      });
    });
    return;
  }

  nlohmann::json attributes = nlohmann::json::object();
  std::string success;
  if (action == ForumAction::Sticky) {
    const bool value = !forumManage_.discussion.sticky;
    attributes["isSticky"] = value;
    forumManage_.message =
        value ? T("正在置顶…", "Pinning…") : T("正在取消置顶…", "Unpinning…");
    success = value ? T("主题已置顶", "Discussion pinned")
                    : T("已取消置顶", "Pin removed");
  } else if (action == ForumAction::Lock) {
    const bool value = !forumManage_.discussion.locked;
    attributes["isLocked"] = value;
    forumManage_.message =
        value ? T("正在锁定…", "Locking…") : T("正在取消锁定…", "Unlocking…");
    success = value ? T("主题已锁定", "Discussion locked")
                    : T("已取消锁定", "Lock removed");
  } else if (action == ForumAction::Hide) {
    attributes["isHidden"] = true;
    forumManage_.message = T("正在隐藏主题…", "Hiding the discussion…");
    success = T("主题已隐藏", "Discussion hidden");
  }

  ForumDiscussion updated = forumManage_.discussion;
  if (action == ForumAction::Sticky) updated.sticky = attributes["isSticky"];
  if (action == ForumAction::Lock) updated.locked = attributes["isLocked"];

  worker_.Submit([this, id, attributes, generation, success, updated,
                  from_detail] {
    auto result = forumApi_.UpdateDiscussion(id, attributes);
    worker_.Post([this, generation, result, success, updated, from_detail] {
      if (generation != forumManage_.generation) return;
      forumManage_.busy = false;
      if (!result.ok) {
        forumManage_.message =
            T("操作失败: ", "Operation failed: ") + result.error.ToString();
        return;
      }
      mode_ = Mode::Browse;
      forum_ = ForumState{};
      forumThread_ = ForumThreadState{};
      if (from_detail) {
        OpenForumThread(updated);
      } else {
        LoadForum(false);
      }
      SetStatus(success);
    });
  });
}

void App::SubmitForumRename() {
  if (forumManage_.busy) return;
  const std::string title = Trim(forumManage_.title);
  if (title.size() < 3 || title.size() > 80) {
    forumManage_.message =
        T("标题长度需要在 3–80 个字符之间",
          "The title must be between 3 and 80 characters");
    return;
  }

  forumManage_.busy = true;
  const std::string id = forumManage_.discussion.id;
  const bool from_detail = forumManage_.from_detail;
  const int generation = ++forumManage_.generation;
  ForumDiscussion updated = forumManage_.discussion;
  updated.title = title;
  forumManage_.message = T("正在保存标题…", "Saving the title…");

  worker_.Submit([this, id, title, generation, updated, from_detail] {
    auto result = forumApi_.UpdateDiscussion(id, {{"title", title}});
    worker_.Post([this, generation, result, updated, from_detail] {
      if (generation != forumManage_.generation) return;
      forumManage_.busy = false;
      if (!result.ok) {
        forumManage_.message =
            T("重命名失败: ", "Rename failed: ") + result.error.ToString();
        return;
      }
      mode_ = Mode::Browse;
      forumManage_.renaming = false;
      forum_ = ForumState{};
      forumThread_ = ForumThreadState{};
      if (from_detail) {
        OpenForumThread(updated);
      } else {
        LoadForum(false);
      }
      SetStatus(T("标题已更新", "Title updated"));
    });
  });
}

std::optional<nlohmann::json> App::EditJsonDocument(
    const nlohmann::json& initial, const std::string& prefix,
    std::string& error) {
  error.clear();
  const auto path =
      WriteTempFile(prefix, initial.dump(2) + "\n");
  if (!path) {
    error = T("无法创建临时文件", "Could not create a temporary file");
    return std::nullopt;
  }

  bool edited = false;
  screen_.WithRestoredIO([&] { edited = EditFile(*path); })();
  const std::string content = ReadTextFile(*path);
  std::remove(path->c_str());
  if (!edited) return std::nullopt;
  if (Trim(content).empty()) {
    error = T("内容为空，已取消", "Empty content; cancelled");
    return std::nullopt;
  }

  nlohmann::json parsed = nlohmann::json::parse(content, nullptr, false);
  if (parsed.is_discarded()) {
    error = T("JSON 解析失败", "Invalid JSON");
    return std::nullopt;
  }
  return parsed;
}

std::optional<std::string> App::EditTextDocument(
    const std::string& initial, const std::string& prefix,
    std::string& error) {
  error.clear();
  const auto path = WriteTempFile(prefix, initial);
  if (!path) {
    error = T("无法创建临时文件", "Could not create a temporary file");
    return std::nullopt;
  }

  bool edited = false;
  screen_.WithRestoredIO([&] { edited = EditFile(*path); })();
  std::string content = ReadTextFile(*path);
  std::remove(path->c_str());
  if (!edited) return std::nullopt;
  content = Trim(content);
  if (content.empty()) {
    error = T("内容为空，已取消", "Empty content; cancelled");
    return std::nullopt;
  }
  return content;
}

std::vector<App::ForumApiPreset> App::ForumApiPresets() const {
  return {
      {T("论坛信息", "Forum info"), "GET", "/api", "{}"},
      {T("主题列表", "Discussions"),
       "GET", "/api/discussions?page[limit]=20&page[offset]=0", "{}"},
      {T("标签", "Tags"), "GET", "/api/tags", "{}"},
      {T("用户组", "Groups"), "GET", "/api/groups", "{}"},
      {T("通知（需登录）", "Notifications (auth)"), "GET",
       "/api/notifications", "{}"},
      {T("回复主题", "Reply"),
       "POST", "/api/posts",
       R"({"data":{"type":"posts","attributes":{"content":"Markdown 正文"},"relationships":{"discussion":{"data":{"type":"discussions","id":"11"}}}}})"},
      {T("发新主题", "New discussion"),
       "POST", "/api/discussions",
       R"({"data":{"type":"discussions","attributes":{"title":"标题","content":"Markdown 正文"},"relationships":{"tags":{"data":[{"type":"tags","id":"1"}]}}}})"},
      {T("修改主题", "Update discussion"),
       "PATCH", "/api/discussions/11",
       R"({"data":{"type":"discussions","id":"11","attributes":{"isSticky":true}}})"},
      {T("删除主题", "Delete discussion"), "DELETE",
       "/api/discussions/11", "{}"},
      {T("扩展接口示例", "Extension endpoint example"), "GET",
       "/api/flags", "{}"},
  };
}

void App::OpenForumApi() {
  forumApiTool_ = ForumApiState{};
  forumApiTool_.from_detail = mode_ == Mode::ForumDetail;
  if (forumApiTool_.from_detail &&
      !forumThread_.thread.discussion.id.empty()) {
    forumApiTool_.path =
        "/api/discussions/" + forumThread_.thread.discussion.id;
  }
  forumApiTool_.cursor = forumApiTool_.path.size();
  mode_ = Mode::ForumApi;
  status_.clear();
}

void App::SendForumApiRequest() {
  if (forumApiTool_.busy) return;
  static const char* kMethods[] = {"GET", "POST", "PATCH", "DELETE"};
  const int index = std::clamp(forumApiTool_.method, 0, 3);
  const std::string method = kMethods[index];
  const std::string path = Trim(forumApiTool_.path);
  if (path.empty()) {
    forumApiTool_.status = T("路径不能为空", "Path must not be empty");
    return;
  }

  nlohmann::json body = nlohmann::json::object();
  if (method == "POST" || method == "PATCH") {
    body = nlohmann::json::parse(forumApiTool_.body, nullptr, false);
    if (body.is_discarded()) {
      forumApiTool_.status =
          T("请求体不是合法 JSON", "Request body is not valid JSON");
      return;
    }
  }

  forumApiTool_.busy = true;
  forumApiTool_.status = T("请求中…", "Sending…");
  forumApiTool_.response.clear();
  forumApiTool_.scroll = 0;
  const int generation = ++forumApiTool_.generation;
  worker_.Submit([this, method, path, body, generation] {
    auto result = forumApi_.RawRequest(method, path, body);
    worker_.Post([this, generation, result, method, path] {
      if (generation != forumApiTool_.generation) return;
      forumApiTool_.busy = false;
      if (!result.ok) {
        forumApiTool_.status =
            T("请求失败: ", "Request failed: ") + result.error.ToString();
        forumApiTool_.response = result.error.ToString();
        return;
      }
      forumApiTool_.status =
          T("成功: ", "OK: ") + method + " " + path;
      if (result.value.empty()) {
        forumApiTool_.response = T("(空响应)", "(empty response)");
      } else {
        forumApiTool_.response = result.value.dump(2);
      }
      forumApiTool_.scroll = 0;
    });
  });
}

void App::OpenNewsMenu() {
  if (!LoggedIn()) {
    SetStatus(T("管理新闻需要先登录（按 L）",
                "Sign in first to manage news (press L)"));
    return;
  }
  ListState& list = ListFor(Tab::News);
  if (list.items.empty() || list.selected < 0 ||
      list.selected >= static_cast<int>(list.items.size())) {
    SetStatus(T("没有选中的新闻", "No story selected"));
    return;
  }
  newsManage_ = NewsManageState{};
  newsManage_.item = list.items[static_cast<std::size_t>(list.selected)];
  mode_ = Mode::NewsMenu;
  status_.clear();
}

void App::StartNewsEdit(bool is_new) {
  if (!LoggedIn()) {
    SetStatus(T("管理新闻需要先登录（按 L）",
                "Sign in first to manage news (press L)"));
    return;
  }

  std::vector<std::string> languages = site_.languages;
  if (languages.empty()) languages.push_back(lang_.empty() ? "zh-cn" : lang_);

  if (is_new) {
    nlohmann::json titles = nlohmann::json::object();
    nlohmann::json descs = nlohmann::json::object();
    for (const auto& language : languages) {
      titles[language] = "";
      descs[language] = "";
    }

    char date[32] = "2026-01-01";
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    if (localtime_r(&now, &local) != nullptr) {
      std::strftime(date, sizeof(date), "%Y-%m-%d", &local);
    }

    nlohmann::json data = {
        {"title", titles},
        {"desc", descs},
        {"date", {{"accuracy", "day"}, {"start", std::string(date) + " 00:00:00"}}},
        {"priority", 999},
        {"active", false},
        {"place", nullptr},
        {"images", nlohmann::json::array()},
    };
    nlohmann::json body = {{"slug", "new-news-slug"}, {"data", data}};
    OpenNewsEditor(body, true);
    return;
  }

  if (newsManage_.item.slug.empty()) return;
  const std::string slug = newsManage_.item.slug;
  newsManage_.busy = true;
  newsManage_.message = T("正在读取新闻…", "Loading the story…");

  worker_.Submit([this, slug, languages] {
    nlohmann::json data = nlohmann::json::object();
    nlohmann::json titles = nlohmann::json::object();
    nlohmann::json descs = nlohmann::json::object();
    bool any = false;
    for (const auto& language : languages) {
      auto result = api_.Item("news", language, slug);
      if (!result.ok || !result.value.is_object()) continue;
      if (!any) {
        data = result.value;
        data.erase("slug");
        data.erase("url");
        data.erase("title");
        data.erase("desc");
        any = true;
      }
      titles[language] = result.value.value("title", "");
      descs[language] = result.value.value("desc", "");
    }
    worker_.Post([this, any, data, titles, descs, slug] {
      newsManage_.busy = false;
      if (!any) {
        newsManage_.message =
            T("读取新闻失败，请稍后重试", "Could not load the story, please retry");
        SetStatus(newsManage_.message);
        return;
      }
      nlohmann::json full = data;
      full["title"] = titles;
      full["desc"] = descs;
      nlohmann::json body = {{"slug", slug}, {"data", full}};
      OpenNewsEditor(body, false);
    });
  });
}

void App::OpenNewsEditor(const nlohmann::json& body, bool is_new) {
  std::string error;
  auto edited = EditJsonDocument(body, "spark-tui-news", error);
  newsManage_.busy = false;
  if (!edited) {
    if (!error.empty()) {
      newsManage_.message = error;
      SetStatus(error);
    }
    return;
  }
  if (!edited->is_object() || !edited->contains("slug") ||
      !edited->at("slug").is_string() || !edited->contains("data") ||
      !edited->at("data").is_object()) {
    newsManage_.message =
        T("需要一个包含 slug 和 data 的 JSON 对象",
          "Expected a JSON object with slug and data");
    SetStatus(newsManage_.message);
    return;
  }

  const std::string slug = Trim(edited->at("slug").get<std::string>());
  const nlohmann::json data = edited->at("data");
  if (slug.empty()) {
    newsManage_.message = T("slug 不能为空", "slug must not be empty");
    SetStatus(newsManage_.message);
    return;
  }

  newsManage_.busy = true;
  newsManage_.message = T("正在保存新闻…", "Saving the story…");
  const int generation = ++newsManage_.generation;
  worker_.Submit([this, slug, data, generation, is_new] {
    auto result = api_.SaveNews(slug, data);
    worker_.Post([this, generation, result, is_new] {
      if (generation != newsManage_.generation) return;
      newsManage_.busy = false;
      if (!result.ok) {
        newsManage_.message =
            T("保存失败: ", "Save failed: ") + result.error.ToString();
        SetStatus(newsManage_.message);
        return;
      }
      lists_[0] = ListState{};
      home_ = HomeState{};
      mode_ = Mode::Browse;
      if (tab_ == Tab::News) LoadList(Tab::News, false);
      SetStatus(is_new ? T("新闻已创建", "Story created")
                       : T("新闻已保存", "Story saved"));
    });
  });
}

void App::DeleteSelectedNews() {
  if (newsManage_.item.slug.empty()) return;
  newsManage_.confirm = 0;
  newsManage_.busy = true;
  newsManage_.message = T("正在删除新闻…", "Deleting the story…");
  const std::string slug = newsManage_.item.slug;
  const int generation = ++newsManage_.generation;
  worker_.Submit([this, slug, generation] {
    auto result = api_.DeleteNews(slug);
    worker_.Post([this, generation, result, slug] {
      if (generation != newsManage_.generation) return;
      newsManage_.busy = false;
      if (!result.ok) {
        newsManage_.message =
            T("删除失败: ", "Delete failed: ") + result.error.ToString();
        SetStatus(newsManage_.message);
        return;
      }
      ListState& list = lists_[0];
      auto it = std::find_if(list.items.begin(), list.items.end(),
                             [&](const ListItem& item) {
                               return item.slug == slug;
                             });
      if (it != list.items.end()) {
        list.items.erase(it);
        list.total = std::max(0, list.total - 1);
        if (list.selected >= static_cast<int>(list.items.size())) {
          list.selected =
              std::max(0, static_cast<int>(list.items.size()) - 1);
        }
      }
      search_ = ListState{};
      home_ = HomeState{};
      mode_ = Mode::Browse;
      SetStatus(T("新闻已删除", "Story deleted"));
    });
  });
}

void App::StartNewsHistory() {
  if (newsManage_.item.slug.empty()) return;
  newsHistory_ = NewsHistoryState{};
  newsHistory_.slug = newsManage_.item.slug;
  newsHistory_.title = newsManage_.item.title;
  newsHistory_.loading = true;
  mode_ = Mode::NewsHistory;
  const int generation = ++newsHistory_.generation;
  const std::string slug = newsHistory_.slug;

  worker_.Submit([this, slug, generation] {
    auto result = api_.History("news", slug);
    worker_.Post([this, generation, result] {
      if (generation != newsHistory_.generation) return;
      newsHistory_.loading = false;
      if (!result.ok) {
        newsHistory_.error = result.error.ToString();
        return;
      }
      newsHistory_.entries.clear();
      if (result.value.is_object() && result.value.contains("ops") &&
          result.value.at("ops").is_array()) {
        for (const auto& op : result.value.at("ops")) {
          if (!op.is_object()) continue;
          HistoryEntry entry;
          if (op.contains("id")) {
            entry.id = op.at("id").is_string()
                           ? op.at("id").get<std::string>()
                           : op.at("id").dump();
          }
          auto read_string = [&](const char* key) {
            if (!op.contains(key) || !op.at(key).is_string()) return std::string{};
            return op.at(key).get<std::string>();
          };
          entry.op = read_string("op");
          entry.actor = read_string("actor");
          entry.note = read_string("note");
          entry.createdAt = read_string("createdAt");
          if (op.contains("payload")) entry.payload = op.at("payload");
          newsHistory_.entries.push_back(std::move(entry));
        }
      }
      if (newsHistory_.selected >=
          static_cast<int>(newsHistory_.entries.size())) {
        newsHistory_.selected =
            std::max(0, static_cast<int>(newsHistory_.entries.size()) - 1);
      }
    });
  });
}

void App::RevertSelectedHistory() {
  if (newsHistory_.entries.empty() || newsHistory_.selected < 0 ||
      newsHistory_.selected >=
          static_cast<int>(newsHistory_.entries.size())) {
    return;
  }
  newsHistory_.confirm = 0;
  const std::string id =
      newsHistory_.entries[static_cast<std::size_t>(newsHistory_.selected)].id;
  if (id.empty()) return;
  newsHistory_.message = T("正在回滚…", "Reverting…");
  const int generation = ++newsHistory_.generation;
  worker_.Submit([this, id, generation] {
    auto result = api_.RevertHistory(id);
    worker_.Post([this, generation, result] {
      if (generation != newsHistory_.generation) return;
      if (!result.ok) {
        newsHistory_.message =
            T("回滚失败: ", "Revert failed: ") + result.error.ToString();
        return;
      }
      lists_[0] = ListState{};
      home_ = HomeState{};
      StartNewsHistory();
      newsHistory_.message = T("已回滚", "Reverted");
    });
  });
}

void App::StartForumReply() {
  if (!ForumLoggedIn()) {
    SetStatus(T("回复需要先登录论坛", "Sign in to the forum to reply"));
    return;
  }
  if (mode_ != Mode::ForumDetail) return;
  const ForumDiscussion discussion = forumThread_.thread.discussion;
  if (discussion.id.empty()) return;

  std::string error;
  auto content = EditTextDocument("", "spark-tui-reply", error);
  if (!content) {
    if (!error.empty()) SetStatus(error);
    return;
  }

  forumThread_.loading = true;
  SetStatus(T("正在发送回复…", "Sending the reply…"));
  const int generation = ++forumThread_.generation;
  worker_.Submit([this, discussion, content, generation] {
    auto result = forumApi_.Reply(discussion.id, *content);
    worker_.Post([this, generation, result, discussion] {
      if (generation != forumThread_.generation) return;
      forumThread_.loading = false;
      if (!result.ok) {
        SetStatus(T("回复失败: ", "Reply failed: ") + result.error.ToString());
        return;
      }
      OpenForumThread(discussion);
      SetStatus(T("回复已发送", "Reply sent"));
    });
  });
}

void App::StartForumCreate() {
  if (!ForumLoggedIn()) {
    SetStatus(T("发主题需要先登录论坛", "Sign in to the forum to post"));
    return;
  }
  SetStatus(T("正在加载论坛标签…", "Loading forum tags…"));

  worker_.Submit([this] {
    auto result = forumApi_.Tags();
    worker_.Post([this, result] {
      if (!result.ok) {
        SetStatus(T("加载标签失败: ", "Could not load tags: ") +
                  result.error.ToString());
        return;
      }
      nlohmann::json available = nlohmann::json::array();
      std::string default_tag;
      for (const auto& tag : result.value) {
        available.push_back(
            {{"id", tag.id}, {"name", tag.name}, {"slug", tag.slug}});
        if (default_tag.empty()) default_tag = tag.slug;
      }
      nlohmann::json initial = {
          {"title", ""},
          {"content", ""},
          {"tags", default_tag.empty() ? nlohmann::json::array()
                                       : nlohmann::json::array({default_tag})},
          {"available_tags", available},
      };

      std::string error;
      auto edited = EditJsonDocument(initial, "spark-tui-discussion", error);
      if (!edited) {
        if (!error.empty()) SetStatus(error);
        return;
      }
      if (!edited->is_object() || !edited->contains("title") ||
          !edited->at("title").is_string() || !edited->contains("content") ||
          !edited->at("content").is_string() || !edited->contains("tags") ||
          !edited->at("tags").is_array()) {
        SetStatus(T("需要 title、content 和 tags 字段",
                    "Expected title, content and tags fields"));
        return;
      }

      const std::string title = Trim(edited->at("title").get<std::string>());
      const std::string content = edited->at("content").get<std::string>();
      if (title.empty() || content.empty()) {
        SetStatus(T("标题和正文不能为空", "Title and content must not be empty"));
        return;
      }

      std::map<std::string, std::string> ids;
      for (const auto& tag : result.value) ids[tag.slug] = tag.id;
      std::vector<std::string> tag_ids;
      for (const auto& entry : edited->at("tags")) {
        if (!entry.is_string()) continue;
        const std::string slug = entry.get<std::string>();
        const auto it = ids.find(slug);
        if (it == ids.end()) {
          SetStatus(T("未知标签: ", "Unknown tag: ") + slug);
          return;
        }
        tag_ids.push_back(it->second);
      }

      SetStatus(T("正在创建主题…", "Creating the discussion…"));
      worker_.Submit([this, title, content, tag_ids] {
        auto created = forumApi_.CreateDiscussion(title, content, tag_ids);
        worker_.Post([this, created] {
          if (!created.ok) {
            SetStatus(T("创建主题失败: ", "Could not create the discussion: ") +
                      created.error.ToString());
            return;
          }
          forum_ = ForumState{};
          forumThread_ = ForumThreadState{};
          if (tab_ == Tab::Forum) LoadForum(false);
          SetStatus(T("主题已创建", "Discussion created"));
        });
      });
    });
  });
}

void App::StartForumTagEdit() {
  if (!ForumLoggedIn() || !forumManage_.discussion.canTag) {
    forumManage_.message =
        T("当前账号不能编辑标签", "This account cannot edit tags");
    return;
  }
  if (forumManage_.busy) return;
  forumManage_.busy = true;
  forumManage_.message = T("正在加载论坛标签…", "Loading forum tags…");

  const std::string id = forumManage_.discussion.id;
  const bool from_detail = forumManage_.from_detail;
  const std::vector<std::string> current = forumManage_.discussion.tagSlugs;
  const int fetch_generation = ++forumManage_.generation;

  worker_.Submit([this, id, current, from_detail, fetch_generation] {
    auto result = forumApi_.Tags();
    worker_.Post([this, id, current, from_detail, result, fetch_generation] {
      if (fetch_generation != forumManage_.generation) return;
      if (!result.ok) {
        forumManage_.busy = false;
        forumManage_.message = T("加载标签失败: ", "Could not load tags: ") +
                               result.error.ToString();
        return;
      }
      nlohmann::json available = nlohmann::json::array();
      for (const auto& tag : result.value) {
        available.push_back(
            {{"id", tag.id}, {"name", tag.name}, {"slug", tag.slug}});
      }
      nlohmann::json initial = {
          {"tags", current},
          {"available_tags", available},
      };

      std::string error;
      auto edited = EditJsonDocument(initial, "spark-tui-tags", error);
      forumManage_.busy = false;
      if (!edited) {
        if (!error.empty()) forumManage_.message = error;
        return;
      }
      if (!edited->is_object() || !edited->contains("tags") ||
          !edited->at("tags").is_array()) {
        forumManage_.message =
            T("需要一个包含 tags 数组的 JSON 对象",
              "Expected a JSON object with a tags array");
        return;
      }

      std::map<std::string, std::string> ids;
      for (const auto& tag : result.value) ids[tag.slug] = tag.id;
      std::vector<std::string> tag_ids;
      std::vector<std::string> slugs;
      std::vector<std::string> names;
      for (const auto& entry : edited->at("tags")) {
        if (!entry.is_string()) continue;
        const std::string slug = entry.get<std::string>();
        const auto it = ids.find(slug);
        if (it == ids.end()) {
          forumManage_.message =
              T("未知标签: ", "Unknown tag: ") + slug;
          return;
        }
        tag_ids.push_back(it->second);
        slugs.push_back(slug);
        for (const auto& tag : result.value) {
          if (tag.slug == slug) names.push_back(tag.name);
        }
      }

      forumManage_.busy = true;
      forumManage_.message = T("正在保存标签…", "Saving tags…");
      const int save_generation = ++forumManage_.generation;
      ForumDiscussion updated = forumManage_.discussion;
      updated.tagSlugs = slugs;
      updated.tagNames = names;

      worker_.Submit([this, id, tag_ids, save_generation, updated, from_detail] {
        auto saved = forumApi_.SetDiscussionTags(id, tag_ids);
        worker_.Post([this, save_generation, saved, updated, from_detail] {
          if (save_generation != forumManage_.generation) return;
          forumManage_.busy = false;
          if (!saved.ok) {
            forumManage_.message = T("保存标签失败: ", "Tag update failed: ") +
                                   saved.error.ToString();
            return;
          }
          mode_ = Mode::Browse;
          forum_ = ForumState{};
          forumThread_ = ForumThreadState{};
          if (from_detail) {
            OpenForumThread(updated);
          } else {
            LoadForum(false);
          }
          SetStatus(T("标签已更新", "Tags updated"));
        });
      });
    });
  });
}

void App::RunSearch() {
  if (searchQuery_.empty()) return;

  ListState& list = search_;
  ++list.generation;
  const int generation = list.generation;
  list.loading = true;
  list.items.clear();
  list.selected = 0;
  list.loaded = true;

  const std::string lang = lang_;
  const std::string query = searchQuery_;
  worker_.Submit([this, generation, lang, query] {
    auto result = api_.Search(lang, query, 100, 0);
    worker_.Post([this, generation, lang, result] {
      if (generation != search_.generation) return;
      search_.loading = false;
      if (lang != lang_) return;
      if (!result.ok) {
        SetStatus(T("搜索失败: ", "Search failed: ") + result.error.ToString());
        return;
      }
      search_.items = result.value.items;
      search_.total = result.value.total;
      search_.hasMore = result.value.hasMore;
      searched_ = true;
    });
  });
}

void App::OpenDetail() {
  ListState& list = CurrentList();
  if (list.items.empty()) return;
  if (list.selected < 0 ||
      list.selected >= static_cast<int>(list.items.size())) {
    return;
  }

  const ListItem item = list.items[static_cast<std::size_t>(list.selected)];
  const std::string lang = lang_;

  mode_ = Mode::Detail;
  detail_ = item.raw;
  detailTitle_ = item.title;
  detailKind_ = item.kind;
  detailRaw_ = cfg_.showRawDetails;
  detailLoading_ = true;
  detailScroll_ = 0;
  status_.clear();

  worker_.Submit([this, item, lang] {
    auto result = api_.Item(item.kind, lang, item.slug);
    worker_.Post([this, lang, result] {
      if (lang != lang_) return;
      detailLoading_ = false;
      if (result.ok) {
        detail_ = result.value;
      } else {
        detail_ = nlohmann::json::object();
        detail_["error"] = result.error.ToString();
        SetStatus(T("详情加载失败: ", "Could not load details: ") +
                  result.error.ToString());
      }
    });
  });
}

void App::StartLogin() {
  const int generation = ++loginGeneration_;
  login_ = LoginState{};
  login_.active = true;
  login_.status = T("正在申请设备码…", "Requesting device code…");
  mode_ = Mode::Login;
  status_.clear();

  worker_.Submit([this, generation] {
    auto result = api_.StartDeviceCode();
    worker_.Post([this, generation, result] {
      if (generation != loginGeneration_.load()) return;
      if (!result.ok) {
        login_.active = false;
        login_.status = T("申请失败: ", "Request failed: ") +
                        result.error.ToString();
        return;
      }
      login_.code = result.value;
      login_.status = T("等待浏览器确认…", "Waiting for browser approval…");
      PollLogin(result.value, generation);
    });
  });
}

void App::PollLogin(DeviceCode code, int generation) {
  worker_.Submit([this, code, generation] {
    using namespace std::chrono;
    const auto deadline = steady_clock::now() + seconds(code.expiresIn);
    int interval = std::max(1, code.interval);

    while (!worker_.Stopping() && generation == loginGeneration_.load() &&
           steady_clock::now() < deadline) {
      for (int i = 0; i < interval * 4; ++i) {
        if (worker_.Stopping() || generation != loginGeneration_.load()) return;
        std::this_thread::sleep_for(milliseconds(250));
      }

      auto result = api_.PollDeviceToken(code.deviceCode);
      if (generation != loginGeneration_.load()) return;

      if (result.ok) {
        worker_.Post([this, generation, result] {
          if (generation == loginGeneration_.load()) {
            OnLoginSuccess(result.value);
          }
        });
        return;
      }

      if (result.error.code == "authorization_pending") {
        worker_.Post([this, generation] {
          if (generation == loginGeneration_.load()) {
            login_.status = T("等待浏览器确认…", "Waiting for browser approval…");
          }
        });
        continue;
      }

      if (result.error.code == "slow_down") {
        interval += 5;
        continue;
      }

      worker_.Post([this, generation, result] {
        if (generation == loginGeneration_.load()) {
          login_.active = false;
          login_.status = T("登录失败: ", "Sign-in failed: ") +
                          result.error.ToString();
        }
      });
      return;
    }

    if (generation == loginGeneration_.load()) {
      worker_.Post([this, generation] {
        if (generation == loginGeneration_.load()) {
          login_.active = false;
          login_.status =
              T("设备码已过期，请按 Esc 后重试",
                "The device code expired; press Esc and try again");
        }
      });
    }
  });
}

void App::OnLoginSuccess(const DeviceToken& token) {
  api_.SetToken(token.plaintext);
  cfg_.token.plaintext = token.plaintext;

  if (token.token.is_object()) {
    if (token.token.contains("id")) {
      cfg_.token.id = token.token["id"].is_string()
                          ? token.token["id"].get<std::string>()
                          : token.token["id"].dump();
    }
    if (token.token.contains("label") && token.token["label"].is_string()) {
      cfg_.token.label = token.token["label"].get<std::string>();
    }
  }

  SaveConfig();
  login_.active = false;
  login_.status = T("登录成功", "Signed in");
  mode_ = Mode::Browse;
  SetStatus(T("登录成功", "Signed in"));
  LoadMe();
}

void App::CancelLogin() {
  ++loginGeneration_;
  login_ = LoginState{};
  mode_ = Mode::Browse;
}

void App::OpenLoginMenu() {
  loginMenu_.selected = 0;
  mode_ = Mode::LoginMenu;
  status_.clear();
}

void App::OpenForumLogin() {
  ++forumLoginGeneration_;
  forumLogin_ = ForumLoginState{};
  forumLogin_.username = cfg_.forum.username;
  forumLogin_.cursor = forumLogin_.username.size();
  mode_ = Mode::ForumLogin;
  status_.clear();
}

void App::CancelForumLogin() {
  ++forumLoginGeneration_;
  forumLogin_.password.clear();
  forumLogin_.status.clear();
  forumLogin_.busy = false;
  mode_ = Mode::LoginMenu;
}

void App::SubmitForumLogin() {
  if (forumLogin_.busy) return;

  const std::string username = Trim(forumLogin_.username);
  const std::string password = forumLogin_.password;
  if (username.empty()) {
    forumLogin_.status = T("请输入用户名或邮箱", "Enter a username or email");
    forumLogin_.field = 0;
    forumLogin_.cursor = forumLogin_.username.size();
    return;
  }
  if (password.empty()) {
    forumLogin_.status = T("请输入密码", "Enter your password");
    forumLogin_.field = 1;
    forumLogin_.cursor = forumLogin_.password.size();
    return;
  }

  forumLogin_.busy = true;
  forumLogin_.status = T("正在登录论坛…", "Signing in to the forum…");
  const int generation = ++forumLoginGeneration_;

  worker_.Submit([this, username, password, generation] {
    // A fresh client keeps a stale forum token out of the token exchange.
    ForumClient login_client(forumApi_.BaseUrl(), forumApi_.Insecure());
    auto auth = login_client.Login(username, password);
    if (auth.ok && !auth.value.token.empty() && !auth.value.userId.empty()) {
      ForumClient user_client(forumApi_.BaseUrl(), forumApi_.Insecure(),
                             auth.value.token);
      auto user = user_client.User(auth.value.userId);
      if (user.ok) auth.value.user = user.value;
    }
    worker_.Post([this, auth, username, generation] {
      if (generation != forumLoginGeneration_.load()) return;
      if (!auth.ok) {
        forumLogin_.busy = false;
        forumLogin_.status =
            T("论坛登录失败: ", "Forum sign-in failed: ") +
            auth.error.ToString();
        return;
      }
      if (auth.value.token.empty()) {
        forumLogin_.busy = false;
        forumLogin_.status =
            T("论坛没有返回令牌", "The forum did not return a token");
        return;
      }
      OnForumLoginSuccess(auth.value, username);
    });
  });
}

void App::OnForumLoginSuccess(const ForumAuth& auth,
                              const std::string& submitted_username) {
  cfg_.forum.token = auth.token;
  cfg_.forum.userId = auth.userId;
  cfg_.forum.username = auth.user.username.empty() ? submitted_username
                                                    : auth.user.username;
  cfg_.forum.displayName = ForumDisplayName(auth.user);
  if (cfg_.forum.displayName.empty()) {
    cfg_.forum.displayName = cfg_.forum.username;
  }

  forumApi_.SetToken(auth.token);
  SaveConfig();

  forumLogin_ = ForumLoginState{};
  mode_ = Mode::Browse;
  SetStatus(T("论坛登录成功", "Signed in to the forum"));

  forum_ = ForumState{};
  forumThread_ = ForumThreadState{};
  if (tab_ == Tab::Forum) {
    LoadForum(false);
  } else {
    SwitchTab(Tab::Forum);
  }
}

void App::LogoutForum() {
  cfg_.forum = {};
  forumApi_.SetToken("");
  SaveConfig();

  forum_ = ForumState{};
  forumThread_ = ForumThreadState{};
  if (mode_ == Mode::ForumDetail) mode_ = Mode::Browse;
  if (tab_ == Tab::Forum) LoadForum(false);
  SetStatus(T("已退出论坛登录", "Signed out of the forum"));
}

void App::Logout() {
  const std::string token = api_.Token();
  const std::string id = cfg_.token.id;

  api_.SetToken("");
  cfg_.token = {};
  me_ = nlohmann::json::object();
  SaveConfig();
  SetStatus(T("已登出", "Signed out"));

  if (!id.empty() && !token.empty()) {
    worker_.Submit([this, id, token] {
      ApiClient temp(api_.BaseUrl(), api_.Insecure(), token);
      temp.RevokeToken(id);
    });
  }
}

void App::SwitchTab(Tab tab) {
  if (tab_ == tab) return;
  tab_ = tab;
  mode_ = Mode::Browse;
  detail_ = nlohmann::json::object();
  detailKind_.clear();
  detailRaw_ = false;
  detailScroll_ = 0;
  status_.clear();

  if (tab == Tab::Home) {
    if (!home_.loaded && !home_.loading) LoadHome();
    return;
  }
  if (tab == Tab::Me) {
    meRaw_ = cfg_.showRawDetails;
    meScroll_ = 0;
    if (LoggedIn() && me_.empty()) LoadMe();
    return;
  }
  if (tab == Tab::Search) {
    searchCursor_ = searchQuery_.size();
    return;
  }
  if (tab == Tab::Forum) {
    if (!forum_.loaded && !forum_.loading) LoadForum(false);
    return;
  }
  ListState& list = ListFor(tab);
  if (!list.loaded && !list.loading) LoadList(tab, false);
}

void App::CycleLanguage() {
  if (site_.languages.empty()) return;

  auto it = std::find(site_.languages.begin(), site_.languages.end(), lang_);
  if (it == site_.languages.end()) {
    it = site_.languages.begin();
  } else {
    ++it;
    if (it == site_.languages.end()) it = site_.languages.begin();
  }
  lang_ = *it;

  for (auto& list : lists_) list = ListState{};
  search_ = ListState{};
  searched_ = false;
  home_ = HomeState{};
  SetStatus(T("语言: ", "Language: ") + lang_);

  if (tab_ == Tab::Search) {
    if (!searchQuery_.empty()) RunSearch();
  } else if (tab_ == Tab::Home) {
    LoadHome();
  } else if (tab_ != Tab::Me) {
    LoadList(tab_, false);
  }
}

void App::CycleUiLanguage() {
  std::vector<std::string> preferences;
  preferences.push_back("system");
  for (const auto& language : site_.languages) {
    if (!Contains(preferences, language)) preferences.push_back(language);
  }
  if (preferences.size() == 1) {
    preferences.push_back("zh-cn");
    preferences.push_back("en-us");
  }

  const std::string current = UiLanguagePreference();
  auto it = std::find(preferences.begin(), preferences.end(), current);
  if (it == preferences.end()) {
    it = preferences.begin();
  } else {
    ++it;
    if (it == preferences.end()) it = preferences.begin();
  }

  cfg_.language = *it;
  SaveConfig();
  lang_ = ResolveLanguageTag(cfg_.language, site_.languages,
                             site_.defaultLanguage);
  if (lang_.empty()) lang_ = "zh-cn";

  for (auto& list : lists_) list = ListState{};
  search_ = ListState{};
  searched_ = false;
  home_ = HomeState{};
  settings_.message = T("界面语言: ", "Interface language: ") + UiLanguageDisplay();

  if (tab_ == Tab::Search) {
    if (!searchQuery_.empty()) RunSearch();
  } else if (tab_ == Tab::Home) {
    LoadHome();
  } else if (tab_ != Tab::Me) {
    LoadList(tab_, false);
  }
}

void App::RefreshCurrent() {
  if (site_.name.empty()) {
    LoadSiteSync();
    lang_ = ResolveLanguageTag(cfg_.language, site_.languages,
                               site_.defaultLanguage);
    if (lang_.empty()) lang_ = "zh-cn";
  }
  if (tab_ == Tab::Home) {
    LoadHome();
    return;
  }
  if (tab_ == Tab::Me) {
    LoadMe();
    return;
  }
  if (tab_ == Tab::Forum) {
    if (mode_ == Mode::ForumDetail) {
      OpenForumDiscussion();
    } else {
      LoadForum(false);
    }
    return;
  }
  if (tab_ == Tab::Search) {
    RunSearch();
    return;
  }
  LoadList(tab_, false);
}

void App::OpenCurrent() {
  std::string url;
  if (mode_ == Mode::Detail) {
    if (detail_.is_object() && detail_.contains("url") &&
        detail_["url"].is_string()) {
      url = detail_["url"].get<std::string>();
    }
  } else if (mode_ == Mode::ForumDetail) {
    url = forumThread_.thread.discussion.url;
  } else if (tab_ == Tab::Forum) {
    if (!forum_.items.empty() && forum_.selected >= 0 &&
        forum_.selected < static_cast<int>(forum_.items.size())) {
      url = forum_.items[static_cast<std::size_t>(forum_.selected)].url;
    }
  } else {
    ListState& list = CurrentList();
    if (!list.items.empty() && list.selected >= 0 &&
        list.selected < static_cast<int>(list.items.size())) {
      url = list.items[static_cast<std::size_t>(list.selected)].url;
    }
  }

  if (url.empty()) {
    SetStatus(T("没有可打开的链接", "No link to open"));
    return;
  }
  if (url.rfind("http", 0) != 0) url = api_.Origin() + url;
  if (OpenUrl(url)) {
    SetStatus(T("已在浏览器打开: ", "Opened in browser: ") + url);
  } else {
    SetStatus(T("无法启动 xdg-open", "Could not start xdg-open"));
  }
}

void App::ImportContactPgp() {
  std::string key;
  if (detail_.is_object() && detail_.contains("pgpKey") &&
      detail_.at("pgpKey").is_string()) {
    key = detail_.at("pgpKey").get<std::string>();
  }
  if (Trim(key).empty()) {
    SetStatus(T("该联系人没有 PGP 公钥", "This contact has no PGP key"));
    return;
  }

  SetStatus(T("正在导入 PGP 公钥…", "Importing PGP key…"));
  worker_.Submit([this, key] {
    const bool ok = ImportPgpKey(key);
    worker_.Post([this, ok] {
      SetStatus(ok ? T("PGP 公钥已导入", "PGP key imported")
                   : T("PGP 公钥导入失败", "Failed to import PGP key"));
    });
  });
}

// ---------------------------------------------------------------------------
// Service sources / instance settings
// ---------------------------------------------------------------------------

void App::StartStartupSync() {
  if (!cfg_.syncOnStart || opts_.noSync) return;

  const Config snapshot = cfg_;
  const int generation = instanceGeneration_;
  SetStatus(T("正在同步服务源…", "Syncing service source…"));
  worker_.Submit([this, snapshot, generation] {
    Config working = snapshot;
    const SyncOutcome outcome = SyncInstanceConfig(working);
    worker_.Post([this, working, outcome, generation] {
      if (generation != instanceGeneration_) return;
      if (!outcome.success) {
        SetStatus(T("服务源同步失败: ", "Service source sync failed: ") +
                  outcome.error);
        return;
      }

      const std::string oldMain = cfg_.EffectiveMain();
      // Only the automatic cache is copied: the user may have edited the
      // manual overrides while this sync was in flight.
      cfg_.instance.autoMain = working.instance.autoMain;
      cfg_.instance.autoStatus = working.instance.autoStatus;
      cfg_.instance.autoForum = working.instance.autoForum;
      const std::string newMain = cfg_.EffectiveMain();
      SaveConfig();

      if (newMain != oldMain) {
        ApplyInstanceChange();
        SetStatus(T("主服务器已更新: ", "Main server updated: ") + newMain);
      } else if (outcome.mainRejected) {
        SetStatus(T("新主服务器不接受旧令牌，已保留 ",
                    "The new main server rejected the old token; kept ") +
                  oldMain +
                  T("；请在设置中退出登录后再切换",
                    "; sign out in Settings before switching"));
      } else if (outcome.mainHeld) {
        SetStatus(T("服务源已同步；新主服务器不可达，保留 ",
                    "Service source synced; new main server unreachable, kept ") +
                  oldMain);
      } else if (outcome.forumRejected) {
        SetStatus(T("新论坛服务器不接受旧令牌，已保留原论坛地址",
                    "The new forum server rejected the old token; kept the old "
                    "forum address"));
      } else if (outcome.forumHeld) {
        SetStatus(T("新论坛服务器不可达，已保留原论坛地址",
                    "The new forum server is unreachable; kept the old forum "
                    "address"));
      } else if (outcome.forumChanged) {
        SetStatus(T("论坛地址已更新: ", "Forum address updated: ") +
                  outcome.forumUrl);
      } else {
        status_.clear();
      }
    });
  });
}

void App::OpenSettings() {
  settings_.selected = 0;
  settings_.message.clear();
  mode_ = Mode::Settings;
  status_.clear();
}

void App::OpenInstanceSettings() {
  settings_.field = 0;
  settings_.sync = cfg_.instance.syncUrl;
  settings_.main = cfg_.instance.mainOverride.value_or("");
  settings_.status = cfg_.instance.statusOverride.value_or("");
  settings_.forum = cfg_.instance.forumOverride.value_or("");
  settings_.cursor = settings_.sync.size();
  settings_.message.clear();
  mode_ = Mode::InstanceSettings;
  status_.clear();
}

bool App::SaveSettings() {
  if (settings_.syncing) {
    settings_.message = T("同步进行中，请稍候", "A sync is already running");
    return false;
  }

  const auto sync = NormalizeUrl(settings_.sync);
  if (!sync) {
    settings_.message = T("同步源地址无效", "Invalid sync source URL");
    return false;
  }

  std::optional<std::string> main;
  std::optional<std::string> status;
  std::optional<std::string> forum;
  if (!Trim(settings_.main).empty()) {
    main = NormalizeUrl(settings_.main);
    if (!main) {
      settings_.message = T("主服务器地址无效", "Invalid main server URL");
      return false;
    }
  }
  if (!Trim(settings_.status).empty()) {
    status = NormalizeUrl(settings_.status);
    if (!status) {
      settings_.message = T("状态服务器地址无效", "Invalid status server URL");
      return false;
    }
  }
  if (!Trim(settings_.forum).empty()) {
    forum = NormalizeUrl(settings_.forum);
    if (!forum) {
      settings_.message = T("论坛服务器地址无效", "Invalid forum server URL");
      return false;
    }
  }

  Config pending = cfg_;
  pending.instance.syncUrl = *sync;
  pending.instance.mainOverride = main;
  pending.instance.statusOverride = status;
  pending.instance.forumOverride = forum;

  std::vector<std::string> changes;
  auto add_change = [&](const std::string& label, const std::string& from,
                        const std::string& to) {
    changes.push_back(label + ": " + from + " -> " + to);
  };
  if (pending.instance.syncUrl != cfg_.instance.syncUrl) {
    add_change(T("同步源", "Sync source"), cfg_.instance.syncUrl,
               pending.instance.syncUrl);
  }
  if (pending.EffectiveMain() != cfg_.EffectiveMain()) {
    add_change(T("主服务器", "Main server"), cfg_.EffectiveMain(),
               pending.EffectiveMain());
  }
  if (pending.EffectiveStatus() != cfg_.EffectiveStatus()) {
    add_change(T("状态服务器", "Status server"), cfg_.EffectiveStatus(),
               pending.EffectiveStatus());
  }
  if (pending.EffectiveForum() != cfg_.EffectiveForum()) {
    add_change(T("论坛服务器", "Forum server"), cfg_.EffectiveForum(),
               pending.EffectiveForum());
  }

  settings_.sync = *sync;
  settings_.main = main.value_or("");
  settings_.status = status.value_or("");
  settings_.forum = forum.value_or("");
  settings_.cursor = SettingsField().size();

  if (changes.empty()) {
    settings_.message = T("没有变更", "No changes");
    return true;
  }

  StartSourceReview(std::move(pending), std::move(changes));
  return true;
}

void App::StartSourceReview(Config pending,
                            std::vector<std::string> changes) {
  sourceReview_ = SourceReviewState{};
  sourceReview_.pending = std::move(pending);
  sourceReview_.changes = std::move(changes);
  sourceReview_.base_generation = instanceGeneration_;
  mode_ = Mode::SourceReview;
  status_.clear();
  ProbeSourceReview();
}

void App::ProbeSourceReview() {
  if (sourceReview_.busy) return;
  const int generation = ++sourceReview_.generation;
  const bool need_spark =
      sourceReview_.pending.EffectiveMain() != cfg_.EffectiveMain() &&
      !sourceReview_.pending.token.Empty();
  const bool need_forum =
      sourceReview_.pending.EffectiveForum() != cfg_.EffectiveForum() &&
      !sourceReview_.pending.forum.Empty();

  sourceReview_.spark_probe = need_spark ? -2 : 2;
  sourceReview_.forum_probe = need_forum ? -2 : 2;
  sourceReview_.message.clear();
  if (!need_spark && !need_forum) {
    sourceReview_.busy = false;
    return;
  }

  sourceReview_.busy = true;
  const Config snapshot = sourceReview_.pending;
  worker_.Submit([this, snapshot, need_spark, need_forum, generation] {
    int spark = 2;
    int forum = 2;
    if (need_spark) {
      spark = ProbeToken(snapshot.EffectiveMain(), snapshot.insecure,
                         snapshot.token.plaintext);
    }
    if (need_forum) {
      ForumClient probe(snapshot.EffectiveForum(), snapshot.insecure,
                        snapshot.forum.token);
      auto result = probe.RawRequest("GET", "/api", nlohmann::json::object());
      if (result.ok) {
        forum = 1;
      } else if (result.error.status == 401 || result.error.status == 403) {
        forum = 0;
      } else {
        forum = -1;
      }
    }
    worker_.Post([this, generation, spark, forum] {
      if (generation != sourceReview_.generation) return;
      sourceReview_.spark_probe = spark;
      sourceReview_.forum_probe = forum;
      sourceReview_.busy = false;
    });
  });
}

bool App::SourceReviewBlocked() const {
  if (sourceReview_.busy) return true;
  return sourceReview_.spark_probe < 1 || sourceReview_.forum_probe < 1;
}

void App::ApplySourceReview() {
  if (sourceReview_.busy) return;
  if (sourceReview_.spark_probe < 1 || sourceReview_.forum_probe < 1) {
    sourceReview_.message =
        T("令牌尚未验证通过，请先退出失效的登录或重试",
          "Tokens are not verified; sign out or retry first");
    return;
  }
  if (sourceReview_.base_generation != instanceGeneration_) {
    sourceReview_.message =
        T("服务源已在后台更新，请取消后重新编辑",
          "The service source changed in the background; cancel and retry");
    return;
  }

  const bool need_login = sourceReview_.logged_out_spark ||
                          sourceReview_.logged_out_forum;
  const bool spark_first = sourceReview_.logged_out_spark;

  cfg_ = sourceReview_.pending;
  ++instanceGeneration_;
  SaveConfig();
  ApplyInstanceChange();

  settings_.sync = cfg_.instance.syncUrl;
  settings_.main = cfg_.instance.mainOverride.value_or("");
  settings_.status = cfg_.instance.statusOverride.value_or("");
  settings_.forum = cfg_.instance.forumOverride.value_or("");
  settings_.message = T("已保存变更", "Changes saved");
  mode_ = Mode::InstanceSettings;

  if (need_login) {
    loginMenu_.selected = spark_first ? 0 : 1;
    mode_ = Mode::LoginMenu;
    SetStatus(spark_first
                  ? T("主服务器已切换，请重新登录 Spark ORG",
                      "Main server switched; sign in to Spark ORG again")
                  : T("论坛服务器已切换，请重新登录论坛",
                      "Forum server switched; sign in to the forum again"));
  }
  sourceReview_ = SourceReviewState{};
}

void App::SyncNow() {
  if (settings_.syncing) return;

  const auto sync = NormalizeUrl(settings_.sync);
  if (!sync) {
    settings_.message = T("同步源地址无效", "Invalid sync source URL");
    return;
  }

  cfg_.instance.syncUrl = *sync;
  settings_.sync = *sync;
  ++instanceGeneration_;
  SaveConfig();

  settings_.syncing = true;
  settings_.message = T("正在同步服务源…", "Syncing service source…");

  const Config snapshot = cfg_;
  worker_.Submit([this, snapshot] {
    Config working = snapshot;
    const SyncOutcome outcome = SyncInstanceConfig(working);
    worker_.Post([this, working, outcome] {
      settings_.syncing = false;
      if (!outcome.success) {
        settings_.message =
            T("同步失败: ", "Sync failed: ") + outcome.error;
        return;
      }

      const std::string oldMain = cfg_.EffectiveMain();
      cfg_.instance = working.instance;
      const std::string newMain = cfg_.EffectiveMain();
      SaveConfig();
      settings_.main = cfg_.instance.mainOverride.value_or("");
      settings_.status = cfg_.instance.statusOverride.value_or("");
      settings_.forum = cfg_.instance.forumOverride.value_or("");

      if (newMain != oldMain) {
        ApplyInstanceChange();
        settings_.message =
            T("已同步；主服务器: ", "Synced; main server: ") + newMain;
      } else if (outcome.mainRejected) {
        settings_.message =
            T("已同步；新主服务器不接受旧令牌，已保留原地址。请先在设置中退出登录",
              "Synced; the new main server rejected the old token. Sign out "
              "in Settings first");
      } else if (outcome.mainHeld) {
        settings_.message =
            T("已同步；新主服务器不可达，保留原地址",
              "Synced; new main server unreachable, kept the old address");
      } else if (outcome.forumRejected) {
        settings_.message =
            T("已同步；新论坛服务器不接受旧令牌，已保留原论坛地址",
              "Synced; the new forum server rejected the old token; kept the "
              "old forum address");
      } else if (outcome.forumHeld) {
        settings_.message =
            T("已同步；新论坛服务器不可达，已保留原论坛地址",
              "Synced; the new forum server is unreachable; kept the old forum "
              "address");
      } else if (outcome.forumChanged) {
        settings_.message =
            T("已同步；论坛: ", "Synced; forum: ") + outcome.forumUrl;
      } else {
        settings_.message = T("服务源已同步", "Service source synced");
      }
    });
  });
}

void App::ApplyInstanceChange() {
  if (mode_ == Mode::Login) CancelLogin();
  if (mode_ == Mode::Detail) mode_ = Mode::Browse;

  api_.SetBaseUrl(cfg_.EffectiveMain());
  forumApi_.SetBaseUrl(cfg_.EffectiveForum());
  forumApi_.SetToken(cfg_.forum.token);
  forum_ = ForumState{};
  forumThread_ = ForumThreadState{};
  ResetContent();
  LoadSiteSync();

  lang_ = ResolveLanguageTag(cfg_.language, site_.languages,
                             site_.defaultLanguage);
  if (lang_.empty()) lang_ = "zh-cn";

  if (tab_ == Tab::Home) {
    LoadHome();
  } else if (tab_ == Tab::Me) {
    if (LoggedIn()) LoadMe();
  } else if (tab_ == Tab::Forum) {
    LoadForum(false);
  } else if (tab_ == Tab::Search) {
    if (!searchQuery_.empty()) RunSearch();
  } else {
    LoadList(tab_, false);
  }
}

void App::ResetContent() {
  for (auto& list : lists_) list = ListState{};
  search_ = ListState{};
  searched_ = false;
  home_ = HomeState{};

  detail_ = nlohmann::json::object();
  detailTitle_.clear();
  detailKind_.clear();
  detailRaw_ = false;
  detailLoading_ = false;
  detailScroll_ = 0;

  me_ = nlohmann::json::object();
  meRaw_ = false;
  meLoading_ = false;
  meScroll_ = 0;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

App::ListState& App::ListFor(Tab tab) {
  switch (tab) {
    case Tab::Contact:
      return lists_[1];
    case Tab::Platform:
      return lists_[2];
    case Tab::News:
    default:
      return lists_[0];
  }
}

App::ListState& App::CurrentList() {
  if (tab_ == Tab::Search) return search_;
  return ListFor(tab_);
}

bool App::LoggedIn() const { return !api_.Token().empty(); }

std::string App::ForumAccountLabel() const {
  if (!cfg_.forum.displayName.empty()) return cfg_.forum.displayName;
  if (!cfg_.forum.username.empty()) return cfg_.forum.username;
  return T("已登录", "Signed in");
}

std::string App::T(const std::string& zh, const std::string& en) const {
  return Tr(zh, en, lang_);
}

std::string App::UiLanguagePreference() const {
  const std::string preference = ToLower(Trim(cfg_.language));
  if (preference.empty() || preference == "system" || preference == "auto" ||
      preference == "follow") {
    return "system";
  }
  return preference;
}

std::string App::UiLanguageDisplay() const {
  return LanguageDisplayName(UiLanguagePreference(), lang_);
}

void App::SaveConfig() {
  cfg_.insecure = api_.Insecure();
  // cfg_.language is the *preference* (system / en-us / zh-cn). The temporary
  // `l` switch only changes lang_ and must not be persisted here.
  cfg_.Save();
}

void App::SetStatus(std::string status) { status_ = std::move(status); }

void App::MoveSelection(int delta) {
  ListState& list = CurrentList();
  if (list.items.empty()) return;
  list.selected = std::clamp(list.selected + delta, 0,
                             static_cast<int>(list.items.size()) - 1);
}

std::string App::ItemUrl(const ListItem& item) const {
  if (item.url.empty()) return {};
  if (item.url.rfind("http", 0) == 0) return item.url;
  return api_.Origin() + item.url;
}

std::string App::Hints() const { return T("? 帮助", "? Help"); }

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

Element App::Render() {
  Element body = help_ ? HelpView() : Content();

  const bool status_is_error =
      status_.find("失败") != std::string::npos ||
      status_.find("错误") != std::string::npos ||
      status_.find("无法") != std::string::npos ||
      status_.find("无效") != std::string::npos ||
      status_.find("不接受") != std::string::npos ||
      status_.find("fail") != std::string::npos ||
      status_.find("Fail") != std::string::npos ||
      status_.find("error") != std::string::npos ||
      status_.find("Error") != std::string::npos ||
      status_.find("invalid") != std::string::npos ||
      status_.find("Invalid") != std::string::npos ||
      status_.find("cannot") != std::string::npos ||
      status_.find("Could not") != std::string::npos;

  Element footer = hbox({
      text(Hints()) | dim,
      filler(),
      status_.empty()
          ? text("")
          : (text(status_) | color(status_is_error ? Color::Red
                                                   : Color::Yellow)),
  });

  Element layout = vbox({
                       Header(),
                       separator(),
                       body | flex,
                       separator(),
                       footer,
                   }) |
                   border;

  if (quitConfirm_) {
    Element dialog =
        vbox({
            text(T("确定退出？", "Quit?")) | bold,
            separator(),
            text(T("y 退出    n / Esc 取消    Enter 确认",
                   "y quit    n / Esc cancel    Enter confirm")) | dim,
        }) |
        border | bgcolor(Color::Black) | clear_under;
    layout = dbox({layout, dialog | center});
  }

  if (mode_ == Mode::NewsMenu && newsManage_.confirm == 1) {
    Element dialog =
        vbox({
            text(T("删除这条新闻？此操作不可撤销",
                   "Delete this story? This cannot be undone")) |
                bold,
            separator(),
            text(T("y 确认    n / Esc 取消", "y confirm    n / Esc cancel")) |
                dim,
        }) |
        border | bgcolor(Color::Black) | clear_under;
    layout = dbox({layout, dialog | center});
  }

  if (mode_ == Mode::NewsHistory && newsHistory_.confirm == 1) {
    Element dialog =
        vbox({
            text(T("回滚到这个版本？", "Revert to this revision?")) | bold,
            separator(),
            text(T("y 确认    n / Esc 取消", "y confirm    n / Esc cancel")) |
                dim,
        }) |
        border | bgcolor(Color::Black) | clear_under;
    layout = dbox({layout, dialog | center});
  }

  if (mode_ == Mode::ForumManage && forumManage_.confirm > 0) {
    const auto actions = ForumActions();
    const int index = forumManage_.confirm - 1;
    std::string label;
    if (index >= 0 && index < static_cast<int>(actions.size())) {
      switch (actions[static_cast<std::size_t>(index)]) {
        case ForumAction::Hide:
          label = T("隐藏这个主题？", "Hide this discussion?");
          break;
        case ForumAction::Delete:
          label = T("删除这个主题？此操作不可撤销",
                    "Delete this discussion? This cannot be undone");
          break;
        default:
          label = T("确认执行这个操作？", "Run this action?");
          break;
      }
    }
    Element dialog =
        vbox({
            text(label) | bold,
            separator(),
            text(T("y 确认    n / Esc 取消", "y confirm    n / Esc cancel")) |
                dim,
        }) |
        border | bgcolor(Color::Black) | clear_under;
    layout = dbox({layout, dialog | center});
  }

  return layout;
}

Element App::Header() {
  std::string who;
  if (LoggedIn()) {
    who = T("已登录", "Signed in");
    if (me_.is_object()) {
      const nlohmann::json* user = &me_;
      if (me_.contains("user") && me_.at("user").is_object()) {
        user = &me_.at("user");
      }
      if (user->is_object()) {
        std::string full_name = user->value("fullName", std::string{});
        if (full_name.empty()) {
          full_name = user->value("display_name", std::string{});
        }
        if (full_name.empty()) {
          full_name = user->value("displayName", std::string{});
        }
        const std::string username = user->value("username", std::string{});
        if (!full_name.empty()) {
          who = full_name;
        } else if (!username.empty()) {
          who = username;
        }
      }
    }
  }

  const Decorator who_style =
      LoggedIn() ? color(Color::Green) : Decorator(dim);

  return hbox({
      text(site_.name.empty() ? "Spark ORG" : site_.name) | bold |
          color(Color::Cyan) | xflex_shrink,
      filler(),
      TabBar(),
      filler(),
      text(who) | who_style | xflex_shrink,
  });
}

Element App::TabBar() {
  const std::array<std::string, 7> names = {
      T("1 主页", "1 Home"),       T("2 新闻", "2 News"),
      T("3 联系", "3 Contact"),    T("4 平台", "4 Platform"),
      T("5 搜索", "5 Search"),     T("6 论坛", "6 Forum"),
      T("7 我的", "7 Me")};
  Elements tabs;
  for (int i = 0; i < 7; ++i) {
    Element item = text(names[static_cast<std::size_t>(i)]);
    if (static_cast<int>(tab_) == i) item = item | inverted | bold;
    tabs.push_back(item);
    if (i < 6) tabs.push_back(text(" "));
  }
  return hbox(std::move(tabs));
}

Element App::Content() {
  if (mode_ == Mode::LoginMenu) return LoginMenuView();
  if (mode_ == Mode::Login) return LoginView();
  if (mode_ == Mode::ForumLogin) return ForumLoginView();
  if (mode_ == Mode::Settings) return SettingsMenuView();
  if (mode_ == Mode::InstanceSettings) return InstanceSettingsView();
  if (mode_ == Mode::HomeManage) return HomeManageView();
  if (mode_ == Mode::ForumManage) return ForumManageView();
  if (mode_ == Mode::ForumApi) return ForumApiView();
  if (mode_ == Mode::SourceReview) return SourceReviewView();
  if (mode_ == Mode::NewsMenu) return NewsMenuView();
  if (mode_ == Mode::NewsHistory) return NewsHistoryView();
  if (mode_ == Mode::ForumDetail) return ForumDetailView();
  if (mode_ == Mode::Detail) return DetailView();
  if (tab_ == Tab::Home) return HomeView();
  if (tab_ == Tab::Search) return SearchView();
  if (tab_ == Tab::Forum) return ForumView();
  if (tab_ == Tab::Me) return MeView();
  return ListView(ListFor(tab_), false);
}

Element App::HomeView() {
  const std::string title = site_.name.empty() ? "Spark ORG" : site_.name;

  Elements lines;
  lines.push_back(text(title) | bold | color(Color::Cyan));
  lines.push_back(text(T("资讯、联系人与平台入口",
                         "News, contacts and platform links")) |
                  dim);
  lines.push_back(separator());

  Element right;
  if (home_.loading) {
    right = text(T("加载中…", "Loading…")) | dim;
  } else if (home_.loaded) {
    right = text(T("共 ", "") + std::to_string(home_.total) +
                 T(" 条", " items")) |
            dim;
  } else {
    right = text("");
  }
  lines.push_back(hbox({
      text(T("最新新闻", "Latest news")) | bold,
      filler(),
      right,
  }));

  if (home_.loading && home_.news.empty()) {
    lines.push_back(text(T("加载中…", "Loading…")) | dim);
  } else if (!home_.error.empty()) {
    lines.push_back(text(T("暂时无法获取新闻，请稍后重试",
                           "News is temporarily unavailable, please retry later")) |
                    color(Color::Red));
    lines.push_back(text(T("按 r 重试", "Press r to retry")) | dim);
  } else if (home_.news.empty()) {
    lines.push_back(text(home_.loaded ? T("暂无新闻", "No news yet")
                                      : T("按 r 加载", "Press r to load")) |
                    dim);
  } else {
    for (const ListItem& item : home_.news) {
      const std::string date = FormatDateField(item.raw);
      const int title_width = std::max(8, screen_.dimx() - 20);
      lines.push_back(hbox({
          text("  "),
          text(date.empty() ? "          " : date) | dim,
          text("  "),
          text(Ellipsize(item.title, title_width)),
          filler(),
      }));
    }
  }

  lines.push_back(filler());

  return vbox(std::move(lines)) | border;
}

Element App::HomeManageView() {
  int picked_count = 0;
  for (const auto& item : homeManage_.items) {
    if (item.picked) ++picked_count;
  }

  Elements lines;
  lines.push_back(hbox({
      text(T("主页置顶管理", "Home page picks")) | bold,
      filler(),
      text(std::to_string(picked_count) + "/" +
           std::to_string(homeManage_.max)) |
          dim,
  }));
  lines.push_back(separator());

  if (homeManage_.loading && homeManage_.items.empty()) {
    lines.push_back(text(T("加载中…", "Loading…")) | dim);
  } else if (!homeManage_.error.empty()) {
    lines.push_back(text(homeManage_.error) | color(Color::Red));
    lines.push_back(text(T("按 r 重试   Esc 返回", "Press r to retry   Esc to go back")) |
                    dim);
  } else if (homeManage_.items.empty()) {
    lines.push_back(text(T("没有可管理的新闻", "No stories to manage")) | dim);
  } else {
    Elements rows;
    const int list_width = std::max(20, screen_.dimx() - 4);
    int pick_index = 0;
    for (std::size_t i = 0; i < homeManage_.items.size(); ++i) {
      const HomePickEntry& item = homeManage_.items[i];
      const bool selected = static_cast<int>(i) == homeManage_.selected;
      std::string marker = "    ";
      if (item.picked) {
        ++pick_index;
        marker = "[" + std::to_string(pick_index) + "]";
        marker = marker + std::string(4 - marker.size(), ' ');
      }

      Elements row;
      row.push_back(text(selected ? "▶ " : "  ") |
                    color(selected ? Color::Cyan : Color::GrayDark));
      row.push_back(text(marker) |
                    color(item.picked ? Color::Yellow : Color::GrayDark));
      row.push_back(text("  "));
      const int title_width = std::max(8, list_width - 10);
      row.push_back(text(Ellipsize(item.title, title_width)) |
                    (selected ? Decorator(bold) : Decorator(nothing)) |
                    xflex_shrink);
      row.push_back(filler());

      Element line = hbox(std::move(row));
      if (selected) {
        line = line | inverted;
        line = focus(line);
      }
      rows.push_back(line);
    }
    lines.push_back(vbox(std::move(rows)) | yframe | flex);
  }

  if (homeManage_.busy || homeManage_.loading) {
    lines.push_back(text(T("处理中…", "Working…")) | color(Color::Yellow));
  }
  if (!homeManage_.message.empty()) {
    lines.push_back(text(homeManage_.message) | color(Color::Yellow));
  }
  lines.push_back(text(T("Enter 置顶 / 取消   J/K 调整顺序   r 刷新   Esc 返回",
                         "Enter pin / unpin   J/K reorder   r refresh   Esc back")) |
                  dim);
  return vbox(std::move(lines)) | border;
}

Element App::ForumManageView() {
  Elements lines;
  lines.push_back(text(T("论坛管理", "Forum moderation")) | bold);
  lines.push_back(text(forumManage_.discussion.title) | dim);
  lines.push_back(separator());

  if (forumManage_.renaming) {
    const std::size_t cursor =
        std::min(forumManage_.title_cursor, forumManage_.title.size());
    lines.push_back(hbox({
        text(T("标题: ", "Title: ")) | bold,
        text(forumManage_.title.substr(0, cursor)),
        text("▏") | color(Color::Cyan) | bold,
        text(forumManage_.title.substr(cursor)),
        filler(),
    }));
    lines.push_back(separator());
    if (!forumManage_.message.empty()) {
      lines.push_back(text(forumManage_.message) | color(Color::Yellow));
    }
    lines.push_back(text(T("Enter 保存   Esc 取消", "Enter save   Esc cancel")) |
                    dim);
    return vbox(std::move(lines)) | border;
  }

  const std::vector<ForumAction> actions = ForumActions();
  if (actions.empty()) {
    lines.push_back(text(T("当前账号没有可用的管理操作",
                           "This account has no moderation actions")) |
                    color(Color::Red));
  } else {
    for (std::size_t i = 0; i < actions.size(); ++i) {
      const bool selected = static_cast<int>(i) == forumManage_.selected;
      std::string label;
      switch (actions[i]) {
        case ForumAction::Sticky:
          label = forumManage_.discussion.sticky
                      ? T("取消置顶", "Unpin")
                      : T("置顶", "Pin");
          break;
        case ForumAction::Lock:
          label = forumManage_.discussion.locked
                      ? T("取消锁定", "Unlock")
                      : T("锁定", "Lock");
          break;
        case ForumAction::Hide:
          label = T("隐藏主题", "Hide discussion");
          break;
        case ForumAction::Tags:
          label = T("编辑标签", "Edit tags");
          break;
        case ForumAction::Rename:
          label = T("重命名", "Rename");
          break;
        case ForumAction::Delete:
          label = T("删除主题", "Delete discussion");
          break;
      }
      Element line = hbox({
          text(selected ? "▶ " : "  ") |
              color(selected ? Color::Cyan : Color::GrayDark),
          text(label) | (selected ? Decorator(bold) : Decorator(nothing)),
          filler(),
      });
      if (selected) line = line | inverted;
      lines.push_back(line);
    }
  }
  lines.push_back(separator());
  if (forumManage_.busy) {
    lines.push_back(text(T("处理中…", "Working…")) | color(Color::Yellow));
  }
  if (!forumManage_.message.empty()) {
    lines.push_back(text(forumManage_.message) | color(Color::Yellow));
  }
  lines.push_back(text(T("↑↓ 选择   Enter 执行   Esc 返回",
                         "↑↓ select   Enter run   Esc back")) |
                  dim);
  return vbox(std::move(lines)) | border;
}

Element App::ForumApiView() {
  static const char* kMethods[] = {"GET", "POST", "PATCH", "DELETE"};
  const auto presets = ForumApiPresets();

  if (forumApiTool_.show_presets) {
    Elements lines;
    lines.push_back(text(T("论坛 API 预设", "Forum API presets")) | bold);
    lines.push_back(text(T("选择常用或扩展接口，Enter 载入；路径可继续编辑",
                           "Pick an endpoint, Enter to load; then edit the path")) |
                    dim);
    lines.push_back(separator());
    Elements rows;
    for (std::size_t i = 0; i < presets.size(); ++i) {
      const bool selected =
          static_cast<int>(i) == forumApiTool_.preset_selected;
      Element line = hbox({
          text(selected ? "▶ " : "  ") |
              color(selected ? Color::Cyan : Color::GrayDark),
          text(presets[i].name) |
              (selected ? Decorator(bold) : Decorator(nothing)),
          filler(),
          text(presets[i].method + " " + presets[i].path) | dim,
      });
      if (selected) line = line | inverted;
      rows.push_back(line);
    }
    lines.push_back(vbox(std::move(rows)) | yframe | flex);
    lines.push_back(text(T("↑↓ 选择   Enter 载入   Esc / p 关闭",
                           "↑↓ select   Enter load   Esc / p close")) |
                    dim);
    return vbox(std::move(lines)) | border;
  }

  const std::string method =
      kMethods[std::clamp(forumApiTool_.method, 0, 3)];
  const std::size_t cursor =
      std::min(forumApiTool_.cursor, forumApiTool_.path.size());

  Elements lines;
  lines.push_back(hbox({
      text(T("论坛 API 工具", "Forum API console")) | bold,
      filler(),
      text(method) | color(Color::Cyan),
  }));
  lines.push_back(text(T("用于调用 Flarum 扩展的 JSON:API；不会写入预设文件",
                         "Call Flarum extension JSON:API endpoints directly")) |
                  dim);
  lines.push_back(separator());
  lines.push_back(hbox({
      text(T("方法: ", "Method: ")),
      text("[" + method + "]") | bold | color(Color::Cyan),
      text(T("   Tab 切换   Ctrl-E 请求体",
             "   Tab cycle   Ctrl-E body")) | dim,
      filler(),
  }));
  lines.push_back(hbox({
      text(T("路径: ", "Path: ")),
      text(forumApiTool_.path.substr(0, cursor)),
      text("▏") | bold | color(Color::Cyan),
      text(forumApiTool_.path.substr(cursor)),
      filler(),
  }));
  if (method == "GET" || method == "DELETE") {
    lines.push_back(text(T("请求体: 不使用", "Body: not used")) | dim);
  } else {
    lines.push_back(text(T("请求体: ", "Body: ") +
                         std::to_string(forumApiTool_.body.size()) +
                         T(" 字节（e 编辑）", " bytes (e to edit)")) |
                    dim);
  }
  lines.push_back(separator());

  std::vector<std::string> response;
  if (forumApiTool_.response.empty()) {
    response.push_back(T("按 Enter 发送请求", "Press Enter to send"));
  } else {
    response = WrapText(forumApiTool_.response,
                        static_cast<std::size_t>(
                            std::max(20, screen_.dimx() - 4)));
  }
  const int height = std::max(3, screen_.dimy() - 14);
  lines.push_back(ScrollLines(response, forumApiTool_.scroll, height) | flex);
  if (forumApiTool_.busy) {
    lines.push_back(text(T("请求中…", "Sending…")) | color(Color::Yellow));
  }
  if (!forumApiTool_.status.empty()) {
    lines.push_back(text(forumApiTool_.status) | color(Color::Yellow));
  }
  lines.push_back(text(T("Enter/Ctrl-R 发送   Ctrl-P 预设   Tab 方法   Ctrl-E 请求体   ↑↓ 滚动   Esc 返回",
                         "Enter/Ctrl-R send   Ctrl-P presets   Tab method   Ctrl-E body   ↑↓ scroll   Esc back")) |
                  dim);
  return vbox(std::move(lines)) | border;
}

Element App::SourceReviewView() {
  Elements lines;
  lines.push_back(text(T("确认服务源变更", "Review source changes")) | bold);
  lines.push_back(separator());

  const int width = std::max(20, screen_.dimx() - 6);
  for (const auto& change : sourceReview_.changes) {
    const auto wrapped = WrapText(
        change, static_cast<std::size_t>(std::max(10, width - 4)));
    lines.push_back(text("  - " + (wrapped.empty() ? "" : wrapped.front())));
    for (std::size_t i = 1; i < wrapped.size(); ++i) {
      lines.push_back(text("    " + wrapped[i]));
    }
  }

  lines.push_back(separator());
  if (sourceReview_.busy) {
    lines.push_back(text(T("正在验证令牌…", "Checking tokens…")) |
                    color(Color::Yellow));
  }

  auto probe_line = [&](const std::string& label, int probe) {
    if (probe == 2) return;
    std::string text_value;
    Color tone = Color::Green;
    if (probe == -2) {
      text_value = T("检查中…", "checking…");
      tone = Color::Yellow;
    } else if (probe == 1) {
      text_value = T("可用", "accepted");
    } else if (probe == 0) {
      text_value = T("被拒绝，需要先退出登录", "rejected; sign out first");
      tone = Color::Red;
    } else {
      text_value = T("无法连接，暂时不能切换（r 重试）",
                     "unreachable; cannot switch yet (r to retry)");
      tone = Color::Yellow;
    }
    lines.push_back(hbox({
        text(label + ": ") | bold,
        text(text_value) | color(tone),
    }));
  };
  probe_line(T("Spark ORG 令牌", "Spark ORG token"),
             sourceReview_.spark_probe);
  probe_line(T("论坛令牌", "Forum token"), sourceReview_.forum_probe);

  if (sourceReview_.logged_out_spark) {
    lines.push_back(text(T("已退出 Spark ORG；应用后进入登录管理",
                           "Signed out of Spark ORG; login menu opens after apply")) |
                    dim);
  }
  if (sourceReview_.logged_out_forum) {
    lines.push_back(text(T("已退出论坛；应用后进入登录管理",
                           "Signed out of the forum; login menu opens after apply")) |
                    dim);
  }
  if (!sourceReview_.message.empty()) {
    lines.push_back(text(sourceReview_.message) | color(Color::Yellow));
  }
  lines.push_back(separator());
  lines.push_back(text(T("Enter 应用   O 退出失效登录   r 重新检查   Esc 取消",
                         "Enter apply   O sign out invalid session   r retry   Esc cancel")) |
                  dim);
  return vbox(std::move(lines)) | border;
}

Element App::NewsMenuView() {
  Elements lines;
  lines.push_back(text(T("新闻管理", "News management")) | bold);
  lines.push_back(text(newsManage_.item.title) | dim);
  lines.push_back(separator());

  const std::array<std::string, 4> labels = {
      T("编辑底层数据", "Edit raw data"),
      T("历史与回滚", "History & revert"),
      T("删除新闻", "Delete story"),
      T("返回", "Back"),
  };
  for (int i = 0; i < 4; ++i) {
    const bool selected = newsManage_.selected == i;
    Element line = hbox({
        text(selected ? "▶ " : "  ") |
            color(selected ? Color::Cyan : Color::GrayDark),
        text(labels[static_cast<std::size_t>(i)]) |
            (selected ? Decorator(bold) : Decorator(nothing)),
        filler(),
    });
    if (selected) line = line | inverted;
    lines.push_back(line);
  }
  lines.push_back(separator());
  if (newsManage_.busy) {
    lines.push_back(text(T("处理中…", "Working…")) | color(Color::Yellow));
  }
  if (!newsManage_.message.empty()) {
    lines.push_back(text(newsManage_.message) | color(Color::Yellow));
  }
  lines.push_back(text(T("↑↓ 选择   Enter 执行   Esc 返回",
                         "↑↓ select   Enter run   Esc back")) |
                  dim);
  return vbox(std::move(lines)) | border;
}

Element App::NewsHistoryView() {
  Elements lines;
  lines.push_back(hbox({
      text(T("新闻历史", "News history")) | bold,
      filler(),
      text(newsHistory_.title) | dim,
  }));
  lines.push_back(separator());

  if (newsHistory_.loading && newsHistory_.entries.empty()) {
    lines.push_back(text(T("加载中…", "Loading…")) | dim);
  } else if (!newsHistory_.error.empty()) {
    lines.push_back(text(newsHistory_.error) | color(Color::Red));
  } else if (newsHistory_.entries.empty()) {
    lines.push_back(text(T("没有历史记录", "No revisions")) | dim);
  } else {
    Elements rows;
    const int width = std::max(20, screen_.dimx() - 4);
    for (std::size_t i = 0; i < newsHistory_.entries.size(); ++i) {
      const HistoryEntry& entry = newsHistory_.entries[i];
      const bool selected = static_cast<int>(i) == newsHistory_.selected;
      std::string line_text = entry.createdAt;
      if (!entry.op.empty()) line_text += "  " + entry.op;
      if (!entry.actor.empty()) line_text += "  " + entry.actor;
      if (!entry.note.empty()) line_text += "  " + entry.note;
      Elements row;
      row.push_back(text(selected ? "▶ " : "  ") |
                    color(selected ? Color::Cyan : Color::GrayDark));
      row.push_back(text(Ellipsize(line_text, width - 4)) |
                    (selected ? Decorator(bold) : Decorator(nothing)) |
                    xflex_shrink);
      row.push_back(filler());
      Element line = hbox(std::move(row));
      if (selected) {
        line = line | inverted;
        line = focus(line);
      }
      rows.push_back(line);
    }
    lines.push_back(vbox(std::move(rows)) | yframe | flex);
  }

  if (!newsHistory_.message.empty()) {
    lines.push_back(text(newsHistory_.message) | color(Color::Yellow));
  }
  lines.push_back(text(T("↑↓ 选择   Enter 回滚到此版本   Esc 返回",
                         "↑↓ select   Enter revert   Esc back")) |
                  dim);
  return vbox(std::move(lines)) | border;
}

Element App::ListView(ListState& list, bool show_kind) {
  if (list.loading && list.items.empty()) {
    return text(T("加载中…", "Loading…")) | center;
  }
  if (list.items.empty()) {
    return text(list.loaded ? T("没有内容", "Nothing here")
                            : T("按 r 加载", "Press r to load")) |
           center;
  }

  auto kind_text = [&](const std::string& kind) {
    if (kind == "news") return T("新闻", "News");
    if (kind == "contact") return T("联系", "Contact");
    if (kind == "platform") return T("平台", "Platform");
    return KindLabel(kind);
  };

  Elements rows;
  const int list_width = std::max(20, screen_.dimx() - 4);
  for (std::size_t i = 0; i < list.items.size(); ++i) {
    const ListItem& item = list.items[i];
    const bool selected = static_cast<int>(i) == list.selected;

    Elements row;
    int reserved = 2;  // selection marker
    if (show_kind) {
      const std::string label = "[" + kind_text(item.kind) + "] ";
      row.push_back(text(label) | color(Color::Yellow));
      reserved += DisplayWidth(label);
    }
    row.push_back(text(selected ? "▶ " : "  ") |
                  color(selected ? Color::Cyan : Color::GrayDark));
    const int subtitle_width = DisplayWidth(item.subtitle);
    const int title_width =
        std::max(8, list_width - reserved - subtitle_width - 4);
    const Decorator title_style = selected ? Decorator(bold) : Decorator(nothing);
    row.push_back(text(Ellipsize(item.title, title_width)) |
                  title_style | xflex_shrink);
    row.push_back(filler());
    row.push_back(text("  "));
    row.push_back(text(item.subtitle) | dim);

    Element line = hbox(std::move(row));
    if (selected) {
      line = line | inverted;
      line = focus(line);
    }
    rows.push_back(line);
  }

  Element header = hbox({
      text(T("共 ", "") + std::to_string(list.total) + T(" 条", " items")) |
          dim,
      filler(),
      text(T("已加载 ", "Loaded ") + std::to_string(list.items.size())) | dim,
      list.loading ? text(T("   加载中…", "   loading…")) | color(Color::Yellow)
                   : text(""),
  });

  return vbox({
      header,
      separatorLight(),
      yframe(vbox(std::move(rows))) | flex,
  });
}

Element App::DetailView() {
  if (detailLoading_ && detail_.empty()) {
    return text(T("加载中…", "Loading…")) | center;
  }

  const int width = std::max(20, screen_.dimx() - 6);
  const int height = std::max(3, screen_.dimy() - 8);
  const auto lines =
      RenderDetailLines(detailKind_, detail_, width, detailRaw_, lang_);

  Element title = hbox({
      text(detailTitle_.empty() ? T("详情", "Details") : detailTitle_) | bold,
      text(detailRaw_ ? T("  [底层数据]", "  [Raw data]") : "") | dim,
      filler(),
      detailLoading_ ? text(T("加载中…", "Loading…")) | color(Color::Yellow)
                     : text(""),
      text("  " + std::to_string(detailScroll_ + 1) + "/" +
           std::to_string(std::max<int>(1, static_cast<int>(lines.size())))) |
          dim,
  });

  return vbox({
      title,
      separator(),
      ScrollLines(lines, detailScroll_, height),
  });
}

Element App::SearchView() {
  const std::string before = searchQuery_.substr(0, searchCursor_);
  const std::string after = searchQuery_.substr(searchCursor_);

  Element input = hbox({
      text(T("搜索: ", "Search: ")) | bold,
      text(before),
      text("▏") | bold | color(Color::Cyan),
      text(after),
  });

  Element results;
  if (search_.loading) {
    results = text(T("搜索中…", "Searching…")) | center;
  } else if (searchQuery_.empty()) {
    results = text(T("输入关键词后按 Enter", "Type a query and press Enter")) |
              center;
  } else if (!searched_) {
    results = text(T("按 Enter 搜索", "Press Enter to search")) | center;
  } else if (search_.items.empty()) {
    results = text(T("没有结果", "No results")) | center;
  } else {
    results = ListView(search_, true);
  }

  return vbox({
      input,
      text(T("Enter 搜索   Esc 清空/返回   ↑↓ 选择   Enter 详情",
             "Enter search   Esc clear/back   ↑↓ select   Enter details")) |
          dim,
      separator(),
      results | flex,
  });
}

Element App::ForumView() {
  Elements lines;
  lines.push_back(hbox({
      text(T("论坛", "Forum")) | bold,
      filler(),
      text(ForumLoggedIn() ? T("已登录: ", "Signed in: ") + ForumAccountLabel()
                           : T("匿名浏览 · L 登录",
                               "Anonymous · L to sign in")) |
          dim,
  }));
  lines.push_back(separator());

  if (forum_.loading && forum_.items.empty()) {
    lines.push_back(text(T("加载中…", "Loading…")) | dim);
  } else if (!forum_.error.empty()) {
    lines.push_back(text(T("论坛暂时无法访问，请稍后重试",
                           "The forum is temporarily unavailable, please retry later")) |
                    color(Color::Red));
    lines.push_back(text(T("按 r 重试", "Press r to retry")) | dim);
  } else if (forum_.items.empty()) {
    lines.push_back(text(forum_.loaded ? T("暂无主题", "No discussions yet")
                                       : T("按 r 加载", "Press r to load")) |
                    dim);
  } else {
    Elements rows;
    const int list_width = std::max(20, screen_.dimx() - 4);
    for (std::size_t i = 0; i < forum_.items.size(); ++i) {
      const ForumDiscussion& discussion = forum_.items[i];
      const bool selected = static_cast<int>(i) == forum_.selected;

      std::string right;
      if (!discussion.tagNames.empty()) {
        right += "#" + discussion.tagNames.front() + "  ";
      }
      const int replies = std::max(0, discussion.commentCount - 1);
      right += std::to_string(replies) + T(" 回复", " replies");
      const std::string date = FormatForumDate(discussion.lastPostedAt);
      if (!date.empty()) right += "  " + date.substr(0, 10);
      if (discussion.locked) right += T("  锁", "  locked");

      const int reserved = 4;  // selection marker + sticky marker
      const int title_width =
          std::max(8, list_width - reserved - DisplayWidth(right) - 4);

      Elements row;
      row.push_back(text(selected ? "▶ " : "  ") |
                    color(selected ? Color::Cyan : Color::GrayDark));
      row.push_back(text(discussion.sticky ? "★ " : "  ") |
                    color(discussion.sticky ? Color::Yellow : Color::GrayDark));
      row.push_back(text(Ellipsize(discussion.title, title_width)) |
                    (selected ? Decorator(bold) : Decorator(nothing)) |
                    xflex_shrink);
      row.push_back(filler());
      row.push_back(text(right) | dim);

      Element line = hbox(std::move(row));
      if (selected) {
        line = line | inverted;
        line = focus(line);
      }
      rows.push_back(line);
    }

    lines.push_back(vbox(std::move(rows)) | yframe | flex);
  }

  if (forum_.loading && !forum_.items.empty()) {
    lines.push_back(text(T("加载中…", "Loading…")) | color(Color::Yellow));
  }
  if (forum_.hasMore) {
    lines.push_back(text(T("按 n 加载更多", "Press n for more")) | dim);
  }

  return vbox(std::move(lines)) | border;
}

Element App::ForumDetailView() {
  const ForumDiscussion& discussion = forumThread_.thread.discussion;
  const int width = std::max(20, screen_.dimx() - 6);
  const int height = std::max(3, screen_.dimy() - 8);
  const auto lines =
      RenderForumThreadLines(forumThread_.thread, width, lang_, false);

  Element title = hbox({
      text(discussion.title.empty() ? T("论坛主题", "Discussion")
                                    : discussion.title) |
          bold | xflex_shrink,
      filler(),
      forumThread_.loading
          ? text(T("加载中…", "Loading…")) | color(Color::Yellow)
          : text(""),
      text("  " + std::to_string(forumThread_.scroll + 1) + "/" +
           std::to_string(std::max<int>(1, static_cast<int>(lines.size())))) |
          dim,
  });

  if (forumThread_.loading && forumThread_.thread.posts.empty()) {
    return vbox({
        title,
        separator(),
        text(T("加载中…", "Loading…")) | dim,
    });
  }

  if (!forumThread_.error.empty() && forumThread_.thread.posts.empty()) {
    return vbox({
               title,
               separator(),
               text(T("主题暂时无法加载，请稍后重试",
                      "The discussion is temporarily unavailable, please retry later")) |
                   color(Color::Red),
               text(T("按 r 重试   Esc 返回", "Press r to retry   Esc to go back")) |
                   dim,
           }) |
           border;
  }

  return vbox({
      title,
      separator(),
      ScrollLines(lines, forumThread_.scroll, height),
  });
}

Element App::MeView() {
  if (!LoggedIn()) {
    return vbox({
               text(T("未登录", "Not signed in")) | bold,
               text(""),
               text(T("按 L 打开登录菜单（Spark ORG 设备码 / 论坛账号）",
                      "Press L for the sign-in menu "
                      "(Spark ORG device code / forum account)")) |
                   dim,
           }) |
           center;
  }
  if (meLoading_ && me_.empty()) {
    return text(T("加载中…", "Loading…")) | center;
  }

  const int width = std::max(20, screen_.dimx() - 6);
  const int height = std::max(3, screen_.dimy() - 8);
  const auto lines = RenderMeLines(me_, width, meRaw_, lang_);

  return vbox({
      hbox({
          text(T("账号信息", "Account")) | bold,
          text(meRaw_ ? T("  [底层数据]", "  [Raw data]") : "") | dim,
          filler(),
      }),
      separator(),
      ScrollLines(lines, meScroll_, height),
  });
}

Element App::LoginMenuView() {
  auto row = [&](int index, const std::string& label,
                 const std::string& value) -> Element {
    const bool selected = loginMenu_.selected == index;
    const Decorator label_style =
        selected ? Decorator(bold) : Decorator(nothing);
    Element line = hbox({
        text(selected ? "▶ " : "  ") |
            color(selected ? Color::Cyan : Color::GrayDark),
        text(label) | label_style,
        filler(),
        text(value) | dim,
    });
    if (selected) line = line | inverted;
    return line;
  };

  Elements lines;
  lines.push_back(text(T("登录", "Sign in")) | bold);
  lines.push_back(separator());
  lines.push_back(row(0, T("Spark ORG 账号", "Spark ORG account"),
                      LoggedIn() ? T("已登录", "Signed in")
                                 : T("未登录", "Not signed in")));
  lines.push_back(row(1, T("论坛账号", "Forum account"),
                      ForumLoggedIn() ? ForumAccountLabel()
                                      : T("未登录", "Not signed in")));
  lines.push_back(separator());
  if (!status_.empty()) {
    lines.push_back(text(status_) | color(Color::Yellow));
  }
  const std::string hint =
      ForumLoggedIn()
          ? T("↑↓ 选择   Enter 登录   O 退出论坛   Esc 关闭",
              "↑↓ select   Enter sign in   O sign out forum   Esc close")
      : LoggedIn()
          ? T("↑↓ 选择   Enter 登录   O 退出 Spark ORG   Esc 关闭",
              "↑↓ select   Enter sign in   O sign out Spark ORG   Esc close")
          : T("↑↓ 选择   Enter 登录   Esc 关闭",
              "↑↓ select   Enter sign in   Esc close");
  lines.push_back(text(hint) | dim);
  return vbox(std::move(lines)) | border;
}

Element App::ForumLoginView() {
  auto field = [&](int index, const std::string& label,
                   const std::string& value, bool masked) -> Element {
    const bool active = forumLogin_.field == index;
    const std::string shown = masked ? MaskPassword(value) : value;
    const std::size_t cursor = std::min(forumLogin_.cursor, value.size());
    const std::string before = masked ? MaskPassword(value.substr(0, cursor))
                                      : value.substr(0, cursor);
    const std::string after = masked ? MaskPassword(value.substr(cursor))
                                     : value.substr(cursor);

    Elements row;
    row.push_back(text(active ? "▶ " : "  ") |
                  color(active ? Color::Cyan : Color::GrayDark));
    row.push_back(text(label) | bold);
    row.push_back(text("  "));
    if (active) {
      row.push_back(text(before));
      row.push_back(text("▏") | bold | color(Color::Cyan));
      row.push_back(text(after));
    } else {
      row.push_back(text(shown.empty() ? "—" : shown) | dim);
    }
    row.push_back(filler());
    return hbox(std::move(row));
  };

  Elements lines;
  lines.push_back(text(T("论坛登录", "Forum sign-in")) | bold);
  lines.push_back(text(T("使用 Flarum 账号登录；密码只用于本次登录，不会保存",
                         "Use your Flarum account. The password is used once and never saved")) |
                  dim);
  lines.push_back(separator());
  lines.push_back(field(0, T("用户名 / 邮箱", "Username / email"),
                        forumLogin_.username, false));
  lines.push_back(field(1, T("密码", "Password"), forumLogin_.password, true));
  lines.push_back(separator());
  if (!forumLogin_.status.empty()) {
    const bool error = forumLogin_.status.find("失败") != std::string::npos ||
                       forumLogin_.status.find("failed") != std::string::npos;
    lines.push_back(text(forumLogin_.status) |
                    color(error ? Color::Red : Color::Yellow));
  }
  lines.push_back(text(T("Tab / ↑↓ 切换   Enter 登录   Esc 返回",
                         "Tab / ↑↓ switch   Enter sign in   Esc back")) |
                  dim);
  return vbox(std::move(lines)) | border;
}

Element App::LoginView() {
  Elements lines;
  lines.push_back(text(T("设备码登录", "Device-code sign-in")) | bold);
  lines.push_back(text(""));

  if (login_.code.userCode.empty()) {
    lines.push_back(text(login_.status.empty()
                             ? T("正在申请设备码…", "Requesting device code…")
                             : login_.status) |
                    dim);
  } else {
    lines.push_back(text(T("1. 在浏览器打开：", "1. Open in your browser:")));
    lines.push_back(text("   " + login_.code.verificationUriComplete) |
                    color(Color::Cyan));
    lines.push_back(text(""));
    lines.push_back(text(T("2. 输入代码：", "2. Enter the code:")));
    lines.push_back(text("   " + login_.code.userCode) | bold |
                    color(Color::Green));
    lines.push_back(text(""));
    lines.push_back(text(login_.status) | dim);
    lines.push_back(
        text(T("有效期 ", "Valid for ") + std::to_string(login_.code.expiresIn) +
             T(" 秒，每 ", " seconds, checking every ") +
             std::to_string(login_.code.interval) + T(" 秒检查一次", " seconds")) |
        dim);
  }

  lines.push_back(text(""));
  lines.push_back(text(T("Esc 取消", "Esc cancel")) | dim);
  return vbox(std::move(lines)) | center | border;
}

Element App::SettingsMenuView() {
  Elements rows;
  for (int i = 0; i < 3; ++i) {
    const bool selected = settings_.selected == i;
    const Decorator style = selected ? Decorator(bold) : Decorator(nothing);
    std::string label;
    std::string value;
    if (i == 0) {
      label = T("服务源与实例地址", "Service source & instance addresses");
      value = cfg_.instance.MainIsManual() ? T("手动", "Manual")
                                           : T("自动", "Automatic");
    } else if (i == 1) {
      label = T("详情显示底层数据", "Show raw data in details");
      value = cfg_.showRawDetails ? T("开", "On") : T("关", "Off");
    } else {
      label = T("界面语言", "Interface language");
      value = UiLanguageDisplay();
    }
    rows.push_back(hbox({
        text(selected ? "▶ " : "  ") |
            color(selected ? Color::Cyan : Color::GrayDark),
        text(label) | style,
        filler(),
        text(value) | dim,
    }));
  }

  Elements lines;
  lines.push_back(text(T("设置", "Settings")) | bold);
  lines.push_back(separator());
  lines.insert(lines.end(), rows.begin(), rows.end());
  lines.push_back(separator());
  lines.push_back(hbox({
      text(T("生效主服务器: ", "Effective main server: ")) | dim,
      text(cfg_.instance.EffectiveMain()) | dim,
  }));
  lines.push_back(hbox({
      text(T("生效论坛服务器: ", "Effective forum server: ")) | dim,
      text(cfg_.instance.EffectiveForum()) | dim,
  }));
  lines.push_back(hbox({
      text(T("配置文件: ", "Config file: ")) | dim,
      text(cfg_.configPath) | dim,
  }));
  lines.push_back(text(T("版本 ", "Version ") + std::string(SPARK_TUI_VERSION)) |
                  dim);
  lines.push_back(separator());
  if (!settings_.message.empty()) {
    lines.push_back(text(settings_.message) | color(Color::Yellow));
  }
  if (settings_.syncing) {
    lines.push_back(text(T("服务源同步中…", "Syncing service source…")) |
                    color(Color::Yellow));
  }
  lines.push_back(text(T("↑↓ 选择   Enter 进入 / 切换   Esc / q 返回",
                         "↑↓ select   Enter open / toggle   Esc / q back")) |
                  dim);
  return vbox(std::move(lines)) | border;
}

Element App::InstanceSettingsView() {
  auto fieldRow = [&](int index, const std::string& label,
                      const std::string& value,
                      const std::string& placeholder) -> Element {
    const bool active = settings_.field == index;
    Elements row;
    row.push_back(text(active ? "▶ " : "  ") |
                  color(active ? Color::Cyan : Color::GrayDark));
    row.push_back(text(label) | bold);
    row.push_back(text("  "));
    if (active) {
      const std::size_t cursor = std::min(settings_.cursor, value.size());
      row.push_back(text(value.substr(0, cursor)));
      row.push_back(text("▏") | bold | color(Color::Cyan));
      row.push_back(text(value.substr(cursor)));
    } else if (value.empty()) {
      row.push_back(text(placeholder) | dim);
    } else {
      row.push_back(text(value));
    }
    row.push_back(filler());
    return hbox(std::move(row));
  };

  Elements lines;
  lines.push_back(text(T("服务源与实例地址", "Service source & instance addresses")) |
                  bold);
  lines.push_back(text(T("优先级: 手动覆盖 > 上次同步值 > 内置默认",
                         "Priority: manual override > last sync > built-in")) |
                  dim);
  lines.push_back(separator());
  lines.push_back(fieldRow(0, T("同步源", "Sync source"), settings_.sync, ""));
  lines.push_back(fieldRow(1, T("主服务器", "Main server"), settings_.main,
                           T("留空 = 自动", "blank = automatic")));
  lines.push_back(fieldRow(2, T("状态服务器", "Status server"), settings_.status,
                           T("留空 = 自动", "blank = automatic")));
  lines.push_back(fieldRow(3, T("论坛服务器", "Forum server"), settings_.forum,
                           T("留空 = 自动", "blank = automatic")));
  lines.push_back(separator());
  lines.push_back(hbox({
      text(T("生效主服务器: ", "Effective main server: ")) | bold,
      text(cfg_.instance.EffectiveMain()) | color(Color::Green),
  }));
  lines.push_back(hbox({
      text(T("生效状态服务器: ", "Effective status server: ")) | bold,
      text(cfg_.instance.EffectiveStatus()) | color(Color::Green),
  }));
  lines.push_back(hbox({
      text(T("生效论坛服务器: ", "Effective forum server: ")) | bold,
      text(cfg_.instance.EffectiveForum()) | color(Color::Green),
  }));
  lines.push_back(hbox({
      text(T("上次同步: 主 ", "Last sync: main ")) | dim,
      text(cfg_.instance.autoMain.value_or(T("(无)", "(none)"))) | dim,
      text(T("   状态 ", "   status ")) | dim,
      text(cfg_.instance.autoStatus.value_or(T("(无)", "(none)"))) | dim,
  }));
  lines.push_back(hbox({
      text(T("上次同步论坛: ", "Last synced forum: ")) | dim,
      text(cfg_.instance.autoForum.value_or(T("(无)", "(none)"))) | dim,
  }));
  lines.push_back(hbox({
      text(T("启动时自动同步: ", "Sync on startup: ")) | dim,
      text(cfg_.syncOnStart ? T("开", "On") : T("关", "Off")) | dim,
  }));
  lines.push_back(separator());
  lines.push_back(text(settings_.message.empty()
                           ? T("Enter 保存   F5 立即同步   Esc 返回设置",
                               "Enter save   F5 sync now   Esc back to settings")
                           : settings_.message) |
                  (settings_.message.empty() ? dim : color(Color::Yellow)));
  if (settings_.syncing) {
    lines.push_back(text(T("同步中…", "Syncing…")) | color(Color::Yellow));
  }
  return vbox(std::move(lines)) | border;
}

Element App::HelpView() {
  std::vector<std::string> rows;
  auto add = [&](const std::string& line) { rows.push_back(line); };

  add(T("[全局]", "[Global]"));
  add(T("  1-7              主页 / 新闻 / 联系 / 平台 / 搜索 / 论坛 / 我的",
        "  1-7              Home / News / Contact / Platform / Search / Forum / Me"));
  add(T("  Tab / Shift-Tab  切换标签", "  Tab / Shift-Tab  switch tabs"));
  add(T("  ?                打开 / 关闭帮助", "  ?                open / close help"));
  add(T("  s                设置", "  s                settings"));
  add(T("  l                临时切换语言（不保存）",
        "  l                switch language temporarily (not saved)"));
  add(T("  r                刷新当前页面", "  r                refresh current page"));
  add(T("  /                搜索", "  /                search"));
  add(T("  L                登录菜单（Spark ORG / 论坛）",
        "  L                sign-in menu (Spark ORG / forum)"));
  add(T("  O                登出（论坛页优先退出论坛）",
        "  O                sign out (forum first on the forum tab)"));
  add(T("  q                退出（会先确认）", "  q                quit (asks first)"));
  add(T("  Ctrl-C           立即退出", "  Ctrl-C           quit immediately"));

  auto section = [&](const std::string& zh, const std::string& en) {
    rows.emplace_back();
    add(T(zh, en));
  };

  if (mode_ == Mode::SourceReview) {
    section("[确认服务源变更]", "[Review source changes]");
    add(T("  Enter / y        应用变更", "  Enter / y        apply changes"));
    add(T("  O                退出失效的登录", "  O                sign out invalid session"));
    add(T("  r                重新检查令牌", "  r                retry token checks"));
    add(T("  Esc / q          取消变更", "  Esc / q          cancel changes"));
  } else if (mode_ == Mode::ForumApi) {
    section("[论坛 API 工具]", "[Forum API console]");
    add(T("  Tab              切换 GET / POST / PATCH / DELETE",
          "  Tab              cycle GET / POST / PATCH / DELETE"));
    add(T("  输入             编辑路径", "  type             edit the path"));
    add(T("  Ctrl-E           编辑 JSON 请求体", "  Ctrl-E           edit the JSON body"));
    add(T("  Enter / Ctrl-R   发送请求", "  Enter / Ctrl-R   send the request"));
    add(T("  Ctrl-P           载入常用接口预设", "  Ctrl-P           load a preset"));
    add(T("  Ctrl-U           清空路径", "  Ctrl-U           clear the path"));
    add(T("  ↑↓ / PgUp/PgDn   滚动响应", "  ↑↓ / PgUp/PgDn   scroll the response"));
    add(T("  Esc              返回", "  Esc              back"));
  } else if (mode_ == Mode::NewsMenu) {
    section("[新闻管理]", "[News management]");
    add(T("  ↑↓ / j k         选择操作", "  ↑↓ / j k         select an action"));
    add(T("  Enter            执行", "  Enter            run"));
    add(T("  Esc / q          返回", "  Esc / q          back"));
  } else if (mode_ == Mode::NewsHistory) {
    section("[新闻历史]", "[News history]");
    add(T("  ↑↓ / j k         选择版本", "  ↑↓ / j k         select a revision"));
    add(T("  Enter            回滚到此版本", "  Enter            revert to it"));
    add(T("  r                刷新历史", "  r                refresh history"));
    add(T("  Esc              返回", "  Esc              back"));
  } else if (mode_ == Mode::HomeManage) {
    section("[主页置顶管理]", "[Home picks]");
    add(T("  ↑↓ / j k         选择新闻", "  ↑↓ / j k         select a story"));
    add(T("  Enter / Space    置顶 / 取消置顶",
          "  Enter / Space    pin / unpin"));
    add(T("  J / K            下移 / 上移置顶顺序",
          "  J / K            move pin down / up"));
    add(T("  r                刷新候选列表", "  r                refresh candidates"));
    add(T("  Esc / q          返回主页", "  Esc / q          back to home"));
  } else if (mode_ == Mode::ForumManage) {
    section("[论坛管理]", "[Forum moderation]");
    add(T("  ↑↓ / j k         选择操作", "  ↑↓ / j k         select an action"));
    add(T("  Enter            执行操作", "  Enter            run the action"));
    add(T("  Esc / q          返回", "  Esc / q          back"));
    if (forumManage_.renaming) {
      add(T("  输入             编辑标题", "  type             edit the title"));
      add(T("  Enter            保存标题", "  Enter            save the title"));
    }
  } else if (mode_ == Mode::ForumDetail) {
    section("[论坛主题]", "[Forum discussion]");
    add(T("  ↑↓ / j k         滚动", "  ↑↓ / j k         scroll"));
    add(T("  PgUp / PgDn      翻页", "  PgUp / PgDn      page"));
    add(T("  Home / End       顶部 / 底部", "  Home / End       top / bottom"));
    add(T("  a                回复主题", "  a                reply"));
    add(T("  A                论坛 API 工具", "  A                forum API console"));
    add(T("  o                在浏览器打开", "  o                open in browser"));
    add(T("  m                管理主题", "  m                moderate discussion"));
    add(T("  r                刷新主题", "  r                refresh discussion"));
    add(T("  Esc              返回论坛列表", "  Esc              back to the forum"));
  } else if (mode_ == Mode::Detail) {
    section("[详情]", "[Details]");
    add(T("  ↑↓ / j k         滚动", "  ↑↓ / j k         scroll"));
    add(T("  PgUp / PgDn      翻页", "  PgUp / PgDn      page"));
    add(T("  Home / End       顶部 / 底部", "  Home / End       top / bottom"));
    add(T("  x                渲染 / 底层数据", "  x                rendered / raw data"));
    add(T("  o                在浏览器打开", "  o                open in browser"));
    add(T("  Esc              返回列表", "  Esc              back to list"));
    if (detailKind_ == "contact" && detail_.is_object() &&
        detail_.contains("pgpKey") && detail_.at("pgpKey").is_string() &&
        !Trim(detail_.at("pgpKey").get<std::string>()).empty()) {
      add(T("  i                导入联系人的 PGP 公钥",
            "  i                import the contact's PGP key"));
    }
  } else if (mode_ == Mode::Settings) {
    section("[设置]", "[Settings]");
    add(T("  ↑↓ / j k         选择条目", "  ↑↓ / j k         select entry"));
    add(T("  Enter            进入 / 切换", "  Enter            open / toggle"));
    add(T("  Esc / q          关闭设置", "  Esc / q          close settings"));
  } else if (mode_ == Mode::InstanceSettings) {
    section("[服务源与实例地址]", "[Service source & instance addresses]");
    add(T("  Tab / ↑↓         切换字段", "  Tab / ↑↓         switch field"));
    add(T("  Enter / Ctrl-S   保存", "  Enter / Ctrl-S   save"));
    add(T("  F5 / Ctrl-R      立即同步服务源", "  F5 / Ctrl-R      sync service source now"));
    add(T("  Esc              返回设置", "  Esc              back to settings"));
  } else if (mode_ == Mode::LoginMenu) {
    section("[登录]", "[Sign-in]");
    add(T("  ↑↓ / j k         选择账号", "  ↑↓ / j k         select account"));
    add(T("  Enter            登录 / 查看状态",
          "  Enter            sign in / show status"));
    add(T("  O                退出论坛登录", "  O                sign out of the forum"));
    add(T("  Esc / q          关闭", "  Esc / q          close"));
  } else if (mode_ == Mode::Login) {
    section("[设备码登录]", "[Device-code sign-in]");
    add(T("  Esc              取消登录", "  Esc              cancel sign-in"));
  } else if (mode_ == Mode::ForumLogin) {
    section("[论坛登录]", "[Forum sign-in]");
    add(T("  Tab / ↑↓         切换用户名 / 密码",
          "  Tab / ↑↓         switch username / password"));
    add(T("  Enter            登录", "  Enter            sign in"));
    add(T("  输入             编辑", "  type             edit"));
    add(T("  Esc              返回登录菜单", "  Esc              back to sign-in"));
  } else if (tab_ == Tab::Home) {
    section("[主页]", "[Home]");
    add(T("  r                刷新主页", "  r                refresh home"));
    add(T("  m                管理主页置顶", "  m                manage home picks"));
  } else if (tab_ == Tab::Forum) {
    section("[论坛]", "[Forum]");
    add(T("  ↑↓ / j k         选择主题", "  ↑↓ / j k         select discussion"));
    add(T("  Enter            查看主题", "  Enter            open discussion"));
    add(T("  n                加载下一页", "  n                load next page"));
    add(T("  r                刷新论坛", "  r                refresh forum"));
    add(T("  c                发新主题", "  c                new discussion"));
    add(T("  A                论坛 API 工具", "  A                forum API console"));
    add(T("  m                管理选中的主题", "  m                moderate the selected discussion"));
    add(T("  o                在浏览器打开", "  o                open in browser"));
    add(T("  L                登录论坛", "  L                sign in to the forum"));
    add(T("  O                退出论坛登录", "  O                sign out of the forum"));
  } else if (tab_ == Tab::Search) {
    section("[搜索]", "[Search]");
    add(T("  输入             编辑关键词", "  type             edit the query"));
    add(T("  Enter            搜索 / 查看选中的结果",
          "  Enter            search / open selected result"));
    add(T("  ↑↓ / PgUp/PgDn   选择结果", "  ↑↓ / PgUp/PgDn   select results"));
    add(T("  Tab              切换标签", "  Tab              switch tabs"));
    add(T("  Esc              清空关键词 / 返回", "  Esc              clear query / back"));
    add(T("  退格 / Delete    删除字符", "  Backspace/Del    delete a character"));
  } else if (tab_ == Tab::Me) {
    section("[我的]", "[Me]");
    add(T("  ↑↓ / j k         滚动", "  ↑↓ / j k         scroll"));
    add(T("  PgUp / PgDn      翻页", "  PgUp / PgDn      page"));
    add(T("  x                渲染 / 底层数据", "  x                rendered / raw data"));
    add(T("  r                刷新", "  r                refresh"));
    add(T("  L                登录", "  L                sign in"));
    add(T("  O                登出", "  O                sign out"));
    add(T("  s                设置", "  s                settings"));
  } else {
    section("[列表]", "[List]");
    add(T("  ↑↓ / j k         选择条目", "  ↑↓ / j k         select item"));
    add(T("  PgUp / PgDn      翻页", "  PgUp / PgDn      page"));
    add(T("  Home / End       首条 / 末条", "  Home / End       first / last"));
    add(T("  Enter            查看详情", "  Enter            view details"));
    add(T("  n                加载下一页", "  n                load next page"));
    add(T("  o                在浏览器打开当前条目",
          "  o                open current item in browser"));
    if (tab_ == Tab::News) {
      add(T("  a                新建新闻", "  a                new story"));
      add(T("  m                管理选中的新闻", "  m                manage the selected story"));
    }
  }

  section("[帮助]", "[Help]");
  add(T("  ↑↓ / j k         滚动", "  ↑↓ / j k         scroll"));
  add(T("  PgUp / PgDn      翻页", "  PgUp / PgDn      page"));
  add(T("  Home / End       顶部 / 底部", "  Home / End       top / bottom"));
  add(T("  ? / Esc / q      关闭帮助", "  ? / Esc / q      close help"));

  const int height = std::max(3, screen_.dimy() - 8);
  return vbox({
             hbox({
                 text(T("快捷键", "Shortcuts")) | bold,
                 filler(),
                 text(T("↑↓ 滚动   ? / Esc 关闭",
                        "↑↓ scroll   ? / Esc close")) |
                     dim,
             }),
             separator(),
             ScrollLines(rows, helpScroll_, height),
         }) |
         border;
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

bool App::HandleEvent(Event event) {
  if (event == Event::Custom) {
    worker_.Drain();
    return true;
  }

  // Ctrl-C always quits, even while typing.
  if (event.input() == std::string(1, '\x03')) {
    screen_.Exit();
    return true;
  }

  if (quitConfirm_) {
    if (event == Event::Character('y') || event == Event::Character('Y') ||
        event == Event::Return) {
      screen_.Exit();
      return true;
    }
    if (event == Event::Character('n') || event == Event::Character('N') ||
        event == Event::Escape) {
      quitConfirm_ = false;
      return true;
    }
    return true;
  }

  if (mode_ == Mode::HomeManage) {
    return HandleHomeManageKey(std::move(event));
  }
  if (mode_ == Mode::ForumManage) {
    return HandleForumManageKey(std::move(event));
  }
  if (mode_ == Mode::ForumApi) {
    return HandleForumApiKey(std::move(event));
  }
  if (mode_ == Mode::SourceReview) {
    return HandleSourceReviewKey(std::move(event));
  }
  if (mode_ == Mode::NewsMenu) {
    return HandleNewsMenuKey(std::move(event));
  }
  if (mode_ == Mode::NewsHistory) {
    return HandleNewsHistoryKey(std::move(event));
  }
  if (mode_ == Mode::LoginMenu) {
    return HandleLoginMenuKey(std::move(event));
  }
  if (mode_ == Mode::ForumLogin) {
    return HandleForumLoginKey(std::move(event));
  }
  if (mode_ == Mode::Login) {
    if (event == Event::Escape) {
      CancelLogin();
    }
    return true;
  }

  if (mode_ == Mode::Settings) {
    return HandleSettingsMenuKey(std::move(event));
  }
  if (mode_ == Mode::InstanceSettings) {
    return HandleInstanceSettingsKey(std::move(event));
  }

  if (help_) {
    if (event == Event::Escape || event == Event::Character('?')) {
      help_ = false;
    } else if (event == Event::Character('q')) {
      help_ = false;
    } else if (event == Event::ArrowUp || event == Event::Character('k')) {
      if (helpScroll_ > 0) --helpScroll_;
    } else if (event == Event::ArrowDown || event == Event::Character('j')) {
      ++helpScroll_;
    } else if (event == Event::PageUp) {
      helpScroll_ = std::max(0, helpScroll_ - 10);
    } else if (event == Event::PageDown) {
      helpScroll_ += 10;
    } else if (event == Event::Home) {
      helpScroll_ = 0;
    } else if (event == Event::End) {
      helpScroll_ = 1 << 30;
    }
    return true;
  }

  if (tab_ == Tab::Search && mode_ == Mode::Browse) {
    return HandleSearchKey(std::move(event));
  }

  // Global keys.
  if (event == Event::Character('q')) {
    quitConfirm_ = true;
    return true;
  }
  if (event == Event::Character('?')) {
    help_ = true;
    helpScroll_ = 0;
    return true;
  }
  if (event == Event::Character('l')) {
    CycleLanguage();
    return true;
  }
  if (event == Event::Character('r')) {
    RefreshCurrent();
    return true;
  }
  if (event == Event::Character('s')) {
    OpenSettings();
    return true;
  }
  if (event == Event::Character('m')) {
    if (mode_ == Mode::Browse && tab_ == Tab::Home) {
      OpenHomeManage();
    } else if (mode_ == Mode::Browse && tab_ == Tab::News) {
      OpenNewsMenu();
    } else if (mode_ == Mode::Browse && tab_ == Tab::Forum) {
      OpenForumManage(false);
    } else if (mode_ == Mode::ForumDetail) {
      OpenForumManage(true);
    }
    return true;
  }
  if (event == Event::Character('a')) {
    if (mode_ == Mode::Browse && tab_ == Tab::News) {
      StartNewsEdit(true);
    } else if (mode_ == Mode::ForumDetail) {
      StartForumReply();
    }
    return true;
  }
  if (event == Event::Character('c')) {
    if (mode_ == Mode::Browse && tab_ == Tab::Forum) {
      StartForumCreate();
    }
    return true;
  }
  if (event == Event::Character('A')) {
    if ((mode_ == Mode::Browse && tab_ == Tab::Forum) ||
        mode_ == Mode::ForumDetail) {
      OpenForumApi();
    }
    return true;
  }
  if (event == Event::Character('L')) {
    OpenLoginMenu();
    return true;
  }
  if (event == Event::Character('O')) {
    if (tab_ == Tab::Forum && ForumLoggedIn()) {
      LogoutForum();
    } else if (LoggedIn()) {
      Logout();
    } else if (ForumLoggedIn()) {
      LogoutForum();
    } else {
      SetStatus(T("尚未登录任何账号", "Not signed in"));
    }
    return true;
  }
  if (event == Event::Character('/')) {
    SwitchTab(Tab::Search);
    return true;
  }
  for (int i = 0; i < 7; ++i) {
    if (event == Event::Character(static_cast<char>('1' + i))) {
      SwitchTab(static_cast<Tab>(i));
      return true;
    }
  }
  if (event == Event::Tab) {
    SwitchTab(static_cast<Tab>((static_cast<int>(tab_) + 1) % 7));
    return true;
  }
  if (event == Event::TabReverse) {
    SwitchTab(static_cast<Tab>((static_cast<int>(tab_) + 6) % 7));
    return true;
  }

  if (tab_ == Tab::Home && mode_ == Mode::Browse) {
    // The home page is informational; `r` refreshes it. It does not open the
    // news list (use tab 2 for that).
    return true;
  }

  if (mode_ == Mode::Detail) {
    if (event == Event::Escape) {
      mode_ = Mode::Browse;
      status_.clear();
      return true;
    }
    if (event == Event::ArrowUp || event == Event::Character('k')) {
      if (detailScroll_ > 0) --detailScroll_;
      return true;
    }
    if (event == Event::ArrowDown || event == Event::Character('j')) {
      ++detailScroll_;
      return true;
    }
    if (event == Event::PageUp) {
      detailScroll_ = std::max(0, detailScroll_ - 10);
      return true;
    }
    if (event == Event::PageDown) {
      detailScroll_ += 10;
      return true;
    }
    if (event == Event::Home) {
      detailScroll_ = 0;
      return true;
    }
    if (event == Event::End) {
      detailScroll_ = 1 << 30;
      return true;
    }
    if (event == Event::Character('o')) {
      OpenCurrent();
      return true;
    }
    if (event == Event::Character('i') && detailKind_ == "contact") {
      ImportContactPgp();
      return true;
    }
    if (event == Event::Character('x')) {
      detailRaw_ = !detailRaw_;
      detailScroll_ = 0;
      SetStatus(detailRaw_ ? T("底层数据: 开", "Raw data: on")
                           : T("底层数据: 关", "Raw data: off"));
      return true;
    }
    return true;
  }

  if (mode_ == Mode::ForumDetail) {
    if (event == Event::Escape) {
      mode_ = Mode::Browse;
      forumThread_ = ForumThreadState{};
      status_.clear();
      return true;
    }
    if (event == Event::ArrowUp || event == Event::Character('k')) {
      if (forumThread_.scroll > 0) --forumThread_.scroll;
      return true;
    }
    if (event == Event::ArrowDown || event == Event::Character('j')) {
      ++forumThread_.scroll;
      return true;
    }
    if (event == Event::PageUp) {
      forumThread_.scroll = std::max(0, forumThread_.scroll - 10);
      return true;
    }
    if (event == Event::PageDown) {
      forumThread_.scroll += 10;
      return true;
    }
    if (event == Event::Home) {
      forumThread_.scroll = 0;
      return true;
    }
    if (event == Event::End) {
      forumThread_.scroll = 1 << 30;
      return true;
    }
    if (event == Event::Character('o')) {
      OpenCurrent();
      return true;
    }
    return true;
  }

  if (tab_ == Tab::Forum) {
    if (event == Event::ArrowUp || event == Event::Character('k')) {
      if (forum_.selected > 0) --forum_.selected;
      return true;
    }
    if (event == Event::ArrowDown || event == Event::Character('j')) {
      if (forum_.selected + 1 < static_cast<int>(forum_.items.size())) {
        ++forum_.selected;
      } else if (forum_.hasMore && !forum_.loading) {
        LoadForum(true);
      }
      return true;
    }
    if (event == Event::PageUp) {
      forum_.selected = std::max(0, forum_.selected - 10);
      return true;
    }
    if (event == Event::PageDown) {
      forum_.selected =
          std::min(std::max(0, static_cast<int>(forum_.items.size()) - 1),
                   forum_.selected + 10);
      return true;
    }
    if (event == Event::Home) {
      forum_.selected = 0;
      return true;
    }
    if (event == Event::End) {
      forum_.selected =
          std::max(0, static_cast<int>(forum_.items.size()) - 1);
      return true;
    }
    if (event == Event::Return) {
      OpenForumDiscussion();
      return true;
    }
    if (event == Event::Character('n')) {
      if (forum_.hasMore && !forum_.loading) LoadForum(true);
      return true;
    }
    if (event == Event::Character('o')) {
      OpenCurrent();
      return true;
    }
    return true;
  }

  if (tab_ == Tab::Me) {
    if (event == Event::ArrowUp || event == Event::Character('k')) {
      if (meScroll_ > 0) --meScroll_;
      return true;
    }
    if (event == Event::ArrowDown || event == Event::Character('j')) {
      ++meScroll_;
      return true;
    }
    if (event == Event::PageUp) {
      meScroll_ = std::max(0, meScroll_ - 10);
      return true;
    }
    if (event == Event::PageDown) {
      meScroll_ += 10;
      return true;
    }
    if (event == Event::Character('x')) {
      meRaw_ = !meRaw_;
      meScroll_ = 0;
      SetStatus(meRaw_ ? T("底层数据: 开", "Raw data: on")
                       : T("底层数据: 关", "Raw data: off"));
      return true;
    }
    return true;
  }

  if (event == Event::ArrowUp || event == Event::Character('k')) {
    MoveSelection(-1);
    return true;
  }
  if (event == Event::ArrowDown || event == Event::Character('j')) {
    MoveSelection(1);
    return true;
  }
  if (event == Event::PageUp) {
    MoveSelection(-10);
    return true;
  }
  if (event == Event::PageDown) {
    MoveSelection(10);
    return true;
  }
  if (event == Event::Home) {
    CurrentList().selected = 0;
    return true;
  }
  if (event == Event::End) {
    ListState& list = CurrentList();
    list.selected = std::max(0, static_cast<int>(list.items.size()) - 1);
    return true;
  }
  if (event == Event::Return) {
    OpenDetail();
    return true;
  }
  if (event == Event::Character('n')) {
    ListState& list = CurrentList();
    if (list.hasMore && !list.loading) LoadList(tab_, true);
    return true;
  }
  if (event == Event::Character('o')) {
    OpenCurrent();
    return true;
  }

  return false;
}

std::string& App::SettingsField() {
  switch (settings_.field) {
    case 1:
      return settings_.main;
    case 2:
      return settings_.status;
    case 3:
      return settings_.forum;
    default:
      return settings_.sync;
  }
}

bool App::HandleSettingsMenuKey(Event event) {
  if (event == Event::Escape || event == Event::Character('q')) {
    mode_ = Mode::Browse;
    status_.clear();
    return true;
  }
  if (event == Event::ArrowUp || event == Event::Character('k') ||
      event == Event::ArrowDown || event == Event::Character('j')) {
    settings_.selected = (settings_.selected + 1) % 3;
    return true;
  }
  if (event == Event::Return || event == Event::Character(' ')) {
    if (settings_.selected == 0) {
      OpenInstanceSettings();
    } else if (settings_.selected == 1) {
      cfg_.showRawDetails = !cfg_.showRawDetails;
      SaveConfig();
      settings_.message =
          T("详情显示底层数据: ", "Show raw data in details: ") +
          (cfg_.showRawDetails ? T("开", "On") : T("关", "Off"));
    } else {
      CycleUiLanguage();
    }
    return true;
  }
  return true;
}

bool App::HandleInstanceSettingsKey(Event event) {
  if (event == Event::Escape) {
    mode_ = Mode::Settings;
    return true;
  }

  if (event == Event::Tab || event == Event::ArrowDown) {
    settings_.field = (settings_.field + 1) % 4;
    settings_.cursor = SettingsField().size();
    return true;
  }
  if (event == Event::TabReverse || event == Event::ArrowUp) {
    settings_.field = (settings_.field + 3) % 4;
    settings_.cursor = SettingsField().size();
    return true;
  }

  std::string& field = SettingsField();

  if (event == Event::Home) {
    settings_.cursor = 0;
    return true;
  }
  if (event == Event::End) {
    settings_.cursor = field.size();
    return true;
  }
  if (event == Event::ArrowLeft) {
    settings_.cursor = Utf8Prev(field, settings_.cursor);
    return true;
  }
  if (event == Event::ArrowRight) {
    settings_.cursor = Utf8Next(field, settings_.cursor);
    return true;
  }
  if (event == Event::Backspace) {
    if (settings_.cursor > 0) {
      const std::size_t previous = Utf8Prev(field, settings_.cursor);
      field.erase(previous, settings_.cursor - previous);
      settings_.cursor = previous;
    }
    return true;
  }
  if (event == Event::Delete) {
    if (settings_.cursor < field.size()) {
      const std::size_t next = Utf8Next(field, settings_.cursor);
      field.erase(settings_.cursor, next - settings_.cursor);
    }
    return true;
  }

  if (event == Event::Return || event.input() == std::string(1, '\x13')) {
    SaveSettings();
    return true;
  }
  if (event == Event::F5 || event.input() == std::string(1, '\x12')) {
    SyncNow();
    return true;
  }

  if (event.is_character()) {
    const std::string character = event.character();
    if (character.size() == 1 &&
        static_cast<unsigned char>(character[0]) >= 32 &&
        character[0] != 127) {
      field.insert(settings_.cursor, character);
      settings_.cursor += character.size();
      return true;
    }
  }
  return true;
}

bool App::HandleForumApiKey(Event event) {
  const auto presets = ForumApiPresets();

  if (forumApiTool_.show_presets) {
    if (event == Event::Escape || event == Event::Character('p')) {
      forumApiTool_.show_presets = false;
      return true;
    }
    if (event == Event::ArrowUp || event == Event::Character('k')) {
      forumApiTool_.preset_selected =
          (forumApiTool_.preset_selected +
           static_cast<int>(presets.size()) - 1) %
          static_cast<int>(presets.size());
      return true;
    }
    if (event == Event::ArrowDown || event == Event::Character('j')) {
      forumApiTool_.preset_selected =
          (forumApiTool_.preset_selected + 1) %
          static_cast<int>(presets.size());
      return true;
    }
    if (event == Event::Return) {
      if (!presets.empty()) {
        const ForumApiPreset& preset =
            presets[static_cast<std::size_t>(forumApiTool_.preset_selected)];
        forumApiTool_.path = preset.path;
        forumApiTool_.body = preset.body;
        forumApiTool_.cursor = forumApiTool_.path.size();
        if (preset.method == "POST") {
          forumApiTool_.method = 1;
        } else if (preset.method == "PATCH") {
          forumApiTool_.method = 2;
        } else if (preset.method == "DELETE") {
          forumApiTool_.method = 3;
        } else {
          forumApiTool_.method = 0;
        }
        forumApiTool_.status =
            T("已载入预设: ", "Preset loaded: ") + preset.name;
      }
      forumApiTool_.show_presets = false;
      return true;
    }
    return true;
  }

  if (event == Event::Escape) {
    mode_ = forumApiTool_.from_detail ? Mode::ForumDetail : Mode::Browse;
    return true;
  }
  if (event == Event::Tab) {
    forumApiTool_.method = (forumApiTool_.method + 1) % 4;
    return true;
  }
  if (event.input() == std::string(1, '\x10')) {  // Ctrl-P: presets
    forumApiTool_.show_presets = true;
    forumApiTool_.preset_selected = 0;
    return true;
  }
  if (event.input() == std::string(1, '\x05')) {  // Ctrl-E: edit body
    if (forumApiTool_.method == 0 || forumApiTool_.method == 3) {
      forumApiTool_.status =
          T("GET / DELETE 不需要请求体", "GET / DELETE have no request body");
      return true;
    }
    nlohmann::json current =
        nlohmann::json::parse(forumApiTool_.body, nullptr, false);
    if (current.is_discarded()) current = nlohmann::json::object();
    std::string error;
    auto edited = EditJsonDocument(current, "spark-tui-api", error);
    if (!edited) {
      if (!error.empty()) forumApiTool_.status = error;
      return true;
    }
    forumApiTool_.body = edited->dump(2);
    forumApiTool_.status = T("请求体已更新", "Request body updated");
    return true;
  }
  if (forumApiTool_.busy) return true;

  if (event == Event::Return ||
      event.input() == std::string(1, '\x12')) {  // Ctrl-R: send
    SendForumApiRequest();
    return true;
  }
  if (event == Event::ArrowUp) {
    if (forumApiTool_.scroll > 0) --forumApiTool_.scroll;
    return true;
  }
  if (event == Event::ArrowDown) {
    ++forumApiTool_.scroll;
    return true;
  }
  if (event == Event::PageUp) {
    forumApiTool_.scroll = std::max(0, forumApiTool_.scroll - 10);
    return true;
  }
  if (event == Event::PageDown) {
    forumApiTool_.scroll += 10;
    return true;
  }
  std::string& path = forumApiTool_.path;
  if (event.input() == std::string(1, '\x15')) {  // Ctrl-U clears the path
    path.clear();
    forumApiTool_.cursor = 0;
    return true;
  }
  if (event == Event::Home) {
    forumApiTool_.cursor = 0;
    return true;
  }
  if (event == Event::End) {
    forumApiTool_.cursor = path.size();
    return true;
  }
  if (event == Event::Backspace) {
    if (forumApiTool_.cursor > 0) {
      const std::size_t previous =
          Utf8Prev(path, forumApiTool_.cursor);
      path.erase(previous, forumApiTool_.cursor - previous);
      forumApiTool_.cursor = previous;
    }
    return true;
  }
  if (event == Event::Delete) {
    if (forumApiTool_.cursor < path.size()) {
      const std::size_t next = Utf8Next(path, forumApiTool_.cursor);
      path.erase(forumApiTool_.cursor, next - forumApiTool_.cursor);
    }
    return true;
  }
  if (event == Event::ArrowLeft) {
    forumApiTool_.cursor = Utf8Prev(path, forumApiTool_.cursor);
    return true;
  }
  if (event == Event::ArrowRight) {
    forumApiTool_.cursor = Utf8Next(path, forumApiTool_.cursor);
    return true;
  }
  if (event.is_character()) {
    const std::string character = event.character();
    if (!character.empty() &&
        static_cast<unsigned char>(character[0]) >= 32 &&
        character[0] != 127) {
      path.insert(forumApiTool_.cursor, character);
      forumApiTool_.cursor += character.size();
      return true;
    }
  }
  return true;
}

bool App::HandleSourceReviewKey(Event event) {
  if (event == Event::Escape || event == Event::Character('q')) {
    ++sourceReview_.generation;
    sourceReview_.busy = false;
    settings_.sync = cfg_.instance.syncUrl;
    settings_.main = cfg_.instance.mainOverride.value_or("");
    settings_.status = cfg_.instance.statusOverride.value_or("");
    settings_.forum = cfg_.instance.forumOverride.value_or("");
    settings_.cursor = SettingsField().size();
    mode_ = Mode::InstanceSettings;
    settings_.message = T("已取消变更", "Changes cancelled");
    return true;
  }
  if (event == Event::Character('r')) {
    ProbeSourceReview();
    return true;
  }
  if (event == Event::Character('O')) {
    bool changed = false;
    if (sourceReview_.spark_probe == 0) {
      Logout();
      sourceReview_.pending.token = {};
      sourceReview_.spark_probe = 2;
      sourceReview_.logged_out_spark = true;
      changed = true;
    }
    if (sourceReview_.forum_probe == 0) {
      LogoutForum();
      sourceReview_.pending.forum = {};
      sourceReview_.forum_probe = 2;
      sourceReview_.logged_out_forum = true;
      changed = true;
    }
    sourceReview_.message =
        changed ? T("已退出失效登录；按 Enter 应用变更",
                    "Invalid session signed out; press Enter to apply")
                : T("没有需要退出的失效登录",
                    "No invalid session to sign out");
    return true;
  }
  if (sourceReview_.busy) return true;
  if (event == Event::Return || event == Event::Character('y') ||
      event == Event::Character('Y')) {
    ApplySourceReview();
    return true;
  }
  return true;
}

bool App::HandleNewsMenuKey(Event event) {
  if (newsManage_.confirm == 1) {
    if (event == Event::Character('y') || event == Event::Character('Y') ||
        event == Event::Return) {
      DeleteSelectedNews();
    } else if (event == Event::Character('n') ||
               event == Event::Character('N') ||
               event == Event::Escape) {
      newsManage_.confirm = 0;
    }
    return true;
  }

  if (event == Event::Escape || event == Event::Character('q')) {
    ++newsManage_.generation;
    mode_ = Mode::Browse;
    return true;
  }
  if (newsManage_.busy) return true;

  if (event == Event::ArrowUp || event == Event::Character('k')) {
    newsManage_.selected = (newsManage_.selected + 3) % 4;
    return true;
  }
  if (event == Event::ArrowDown || event == Event::Character('j')) {
    newsManage_.selected = (newsManage_.selected + 1) % 4;
    return true;
  }
  if (event == Event::Return || event == Event::Character(' ')) {
    if (newsManage_.selected == 0) {
      StartNewsEdit(false);
    } else if (newsManage_.selected == 1) {
      StartNewsHistory();
    } else if (newsManage_.selected == 2) {
      newsManage_.confirm = 1;
    } else {
      mode_ = Mode::Browse;
    }
    return true;
  }
  return true;
}

bool App::HandleNewsHistoryKey(Event event) {
  if (newsHistory_.confirm == 1) {
    if (event == Event::Character('y') || event == Event::Character('Y') ||
        event == Event::Return) {
      RevertSelectedHistory();
    } else if (event == Event::Character('n') ||
               event == Event::Character('N') ||
               event == Event::Escape) {
      newsHistory_.confirm = 0;
    }
    return true;
  }

  if (event == Event::Escape || event == Event::Character('q')) {
    ++newsHistory_.generation;
    mode_ = newsManage_.item.slug.empty() ? Mode::Browse : Mode::NewsMenu;
    return true;
  }
  if (event == Event::ArrowUp || event == Event::Character('k')) {
    if (newsHistory_.selected > 0) --newsHistory_.selected;
    return true;
  }
  if (event == Event::ArrowDown || event == Event::Character('j')) {
    if (newsHistory_.selected + 1 <
        static_cast<int>(newsHistory_.entries.size())) {
      ++newsHistory_.selected;
    }
    return true;
  }
  if (event == Event::PageUp) {
    newsHistory_.selected = std::max(0, newsHistory_.selected - 10);
    return true;
  }
  if (event == Event::PageDown) {
    newsHistory_.selected =
        std::min(std::max(0, static_cast<int>(newsHistory_.entries.size()) - 1),
                 newsHistory_.selected + 10);
    return true;
  }
  if (event == Event::Home) {
    newsHistory_.selected = 0;
    return true;
  }
  if (event == Event::End) {
    newsHistory_.selected =
        std::max(0, static_cast<int>(newsHistory_.entries.size()) - 1);
    return true;
  }
  if (event == Event::Return || event == Event::Character(' ')) {
    if (!newsHistory_.entries.empty()) newsHistory_.confirm = 1;
    return true;
  }
  if (event == Event::Character('r')) {
    StartNewsHistory();
    return true;
  }
  return true;
}

bool App::HandleHomeManageKey(Event event) {
  if (event == Event::Escape || event == Event::Character('q')) {
    ++homeManage_.generation;  // discard any in-flight operation result
    mode_ = Mode::Browse;
    home_ = HomeState{};
    LoadHome();
    return true;
  }
  if (homeManage_.busy || homeManage_.loading) return true;

  if (event == Event::ArrowUp || event == Event::Character('k')) {
    if (homeManage_.selected > 0) --homeManage_.selected;
    return true;
  }
  if (event == Event::ArrowDown || event == Event::Character('j')) {
    if (homeManage_.selected + 1 <
        static_cast<int>(homeManage_.items.size())) {
      ++homeManage_.selected;
    }
    return true;
  }
  if (event == Event::PageUp) {
    homeManage_.selected = std::max(0, homeManage_.selected - 10);
    return true;
  }
  if (event == Event::PageDown) {
    homeManage_.selected =
        std::min(std::max(0, static_cast<int>(homeManage_.items.size()) - 1),
                 homeManage_.selected + 10);
    return true;
  }
  if (event == Event::Home) {
    homeManage_.selected = 0;
    return true;
  }
  if (event == Event::End) {
    homeManage_.selected =
        std::max(0, static_cast<int>(homeManage_.items.size()) - 1);
    return true;
  }
  if (event == Event::Return || event == Event::Character(' ')) {
    ToggleHomePick();
    return true;
  }
  if (event == Event::Character('J')) {
    ReorderHomePicks(1);
    return true;
  }
  if (event == Event::Character('K')) {
    ReorderHomePicks(-1);
    return true;
  }
  if (event == Event::Character('r')) {
    LoadHomeManage();
    return true;
  }
  return true;
}

bool App::HandleForumManageKey(Event event) {
  if (forumManage_.confirm > 0) {
    if (event == Event::Character('y') || event == Event::Character('Y') ||
        event == Event::Return) {
      const auto actions = ForumActions();
      const int index = forumManage_.confirm - 1;
      if (index >= 0 && index < static_cast<int>(actions.size())) {
        RunForumAction(actions[static_cast<std::size_t>(index)]);
      } else {
        forumManage_.confirm = 0;
      }
    } else if (event == Event::Character('n') ||
               event == Event::Character('N') ||
               event == Event::Escape) {
      forumManage_.confirm = 0;
    }
    return true;
  }

  if (forumManage_.renaming) {
    if (forumManage_.busy) return true;
    if (event == Event::Escape) {
      forumManage_.renaming = false;
      forumManage_.message.clear();
      return true;
    }
    if (event == Event::Return) {
      SubmitForumRename();
      return true;
    }
    std::string& title = forumManage_.title;
    if (event == Event::Home) {
      forumManage_.title_cursor = 0;
      return true;
    }
    if (event == Event::End) {
      forumManage_.title_cursor = title.size();
      return true;
    }
    if (event == Event::ArrowLeft) {
      forumManage_.title_cursor = Utf8Prev(title, forumManage_.title_cursor);
      return true;
    }
    if (event == Event::ArrowRight) {
      forumManage_.title_cursor = Utf8Next(title, forumManage_.title_cursor);
      return true;
    }
    if (event == Event::Backspace) {
      if (forumManage_.title_cursor > 0) {
        const std::size_t previous =
            Utf8Prev(title, forumManage_.title_cursor);
        title.erase(previous, forumManage_.title_cursor - previous);
        forumManage_.title_cursor = previous;
      }
      return true;
    }
    if (event == Event::Delete) {
      if (forumManage_.title_cursor < title.size()) {
        const std::size_t next =
            Utf8Next(title, forumManage_.title_cursor);
        title.erase(forumManage_.title_cursor,
                    next - forumManage_.title_cursor);
      }
      return true;
    }
    if (event.is_character()) {
      const std::string character = event.character();
      if (!character.empty() &&
          static_cast<unsigned char>(character[0]) >= 32 &&
          character[0] != 127) {
        title.insert(forumManage_.title_cursor, character);
        forumManage_.title_cursor += character.size();
        return true;
      }
    }
    return true;
  }

  if (event == Event::Escape || event == Event::Character('q')) {
    ++forumManage_.generation;
    forumManage_.confirm = 0;
    forumManage_.renaming = false;
    mode_ = forumManage_.from_detail ? Mode::ForumDetail : Mode::Browse;
    return true;
  }
  if (forumManage_.busy) return true;

  const auto actions = ForumActions();
  if (actions.empty()) return true;

  if (event == Event::ArrowUp || event == Event::Character('k')) {
    forumManage_.selected =
        (forumManage_.selected + static_cast<int>(actions.size()) - 1) %
        static_cast<int>(actions.size());
    return true;
  }
  if (event == Event::ArrowDown || event == Event::Character('j')) {
    forumManage_.selected =
        (forumManage_.selected + 1) % static_cast<int>(actions.size());
    return true;
  }
  if (event == Event::Return || event == Event::Character(' ')) {
    const int index = forumManage_.selected;
    if (index < 0 || index >= static_cast<int>(actions.size())) return true;
    const ForumAction action = actions[static_cast<std::size_t>(index)];
    if (action == ForumAction::Hide || action == ForumAction::Delete) {
      forumManage_.confirm = index + 1;
      return true;
    }
    RunForumAction(action);
    return true;
  }
  return true;
}

bool App::HandleLoginMenuKey(Event event) {
  if (event == Event::Escape || event == Event::Character('q')) {
    mode_ = Mode::Browse;
    status_.clear();
    return true;
  }
  if (event == Event::ArrowUp || event == Event::Character('k') ||
      event == Event::ArrowDown || event == Event::Character('j')) {
    loginMenu_.selected = (loginMenu_.selected + 1) % 2;
    return true;
  }
  if (event == Event::Character('O')) {
    if (ForumLoggedIn()) {
      LogoutForum();
    } else if (LoggedIn()) {
      Logout();
    } else {
      SetStatus(T("尚未登录任何账号", "Not signed in"));
    }
    return true;
  }
  if (event == Event::Return || event == Event::Character(' ')) {
    if (loginMenu_.selected == 0) {
      if (!LoggedIn()) {
        StartLogin();
      } else {
        SetStatus(T("Spark ORG 已登录；按 O 登出",
                    "Signed in to Spark ORG; press O to sign out"));
      }
    } else if (!ForumLoggedIn()) {
      OpenForumLogin();
    } else {
      SetStatus(T("论坛已登录为 ", "Signed in to the forum as ") +
                ForumAccountLabel() + T("；按 O 退出", "; press O to sign out"));
    }
    return true;
  }
  return true;
}

bool App::HandleForumLoginKey(Event event) {
  if (event == Event::Escape) {
    CancelForumLogin();
    return true;
  }
  if (forumLogin_.busy) return true;
  if (event == Event::Tab || event == Event::ArrowUp ||
      event == Event::ArrowDown) {
    forumLogin_.field = forumLogin_.field == 0 ? 1 : 0;
    forumLogin_.cursor = forumLogin_.field == 0 ? forumLogin_.username.size()
                                                : forumLogin_.password.size();
    return true;
  }
  if (event == Event::Return) {
    SubmitForumLogin();
    return true;
  }

  std::string& field = forumLogin_.field == 0 ? forumLogin_.username
                                               : forumLogin_.password;
  if (event == Event::Home) {
    forumLogin_.cursor = 0;
    return true;
  }
  if (event == Event::End) {
    forumLogin_.cursor = field.size();
    return true;
  }
  if (event == Event::ArrowLeft) {
    forumLogin_.cursor = Utf8Prev(field, forumLogin_.cursor);
    return true;
  }
  if (event == Event::ArrowRight) {
    forumLogin_.cursor = Utf8Next(field, forumLogin_.cursor);
    return true;
  }
  if (event == Event::Backspace) {
    if (forumLogin_.cursor > 0) {
      const std::size_t previous = Utf8Prev(field, forumLogin_.cursor);
      field.erase(previous, forumLogin_.cursor - previous);
      forumLogin_.cursor = previous;
    }
    return true;
  }
  if (event == Event::Delete) {
    if (forumLogin_.cursor < field.size()) {
      const std::size_t next = Utf8Next(field, forumLogin_.cursor);
      field.erase(forumLogin_.cursor, next - forumLogin_.cursor);
    }
    return true;
  }
  if (event.is_character()) {
    const std::string character = event.character();
    if (!character.empty() &&
        static_cast<unsigned char>(character[0]) >= 32 &&
        character[0] != 127) {
      field.insert(forumLogin_.cursor, character);
      forumLogin_.cursor += character.size();
      return true;
    }
  }
  return true;
}

bool App::HandleSearchKey(Event event) {
  if (event == Event::Escape) {
    if (!searchQuery_.empty()) {
      searchQuery_.clear();
      searchCursor_ = 0;
      search_ = ListState{};
      searched_ = false;
    } else {
      SwitchTab(Tab::News);
    }
    return true;
  }
  if (event == Event::Return) {
    RunSearch();
    return true;
  }
  if (event == Event::Backspace) {
    if (searchCursor_ > 0) {
      const std::size_t previous = Utf8Prev(searchQuery_, searchCursor_);
      searchQuery_.erase(previous, searchCursor_ - previous);
      searchCursor_ = previous;
      searched_ = false;
    }
    return true;
  }
  if (event == Event::Delete) {
    if (searchCursor_ < searchQuery_.size()) {
      const std::size_t next = Utf8Next(searchQuery_, searchCursor_);
      searchQuery_.erase(searchCursor_, next - searchCursor_);
      searched_ = false;
    }
    return true;
  }
  if (event == Event::ArrowLeft) {
    searchCursor_ = Utf8Prev(searchQuery_, searchCursor_);
    return true;
  }
  if (event == Event::ArrowRight) {
    searchCursor_ = Utf8Next(searchQuery_, searchCursor_);
    return true;
  }
  if (event == Event::Home) {
    searchCursor_ = 0;
    return true;
  }
  if (event == Event::End) {
    searchCursor_ = searchQuery_.size();
    return true;
  }
  if (event == Event::ArrowUp) {
    MoveSelection(-1);
    return true;
  }
  if (event == Event::ArrowDown) {
    MoveSelection(1);
    return true;
  }
  if (event == Event::PageUp) {
    MoveSelection(-10);
    return true;
  }
  if (event == Event::PageDown) {
    MoveSelection(10);
    return true;
  }
  if (event == Event::Tab) {
    SwitchTab(static_cast<Tab>((static_cast<int>(tab_) + 1) % 7));
    return true;
  }
  if (event.is_character()) {
    const std::string character = event.character();
    if (character.size() == 1 &&
        static_cast<unsigned char>(character[0]) >= 32 &&
        character[0] != 127) {
      searchQuery_.insert(searchCursor_, character);
      searchCursor_ += character.size();
      searched_ = false;
      return true;
    }
  }
  return false;
}

}  // namespace spark
