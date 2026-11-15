#pragma once

#include <array>
#include <atomic>
#include <optional>
#include <string>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <nlohmann/json.hpp>

#include "api/client.h"
#include "api/models.h"
#include "config.h"
#include "forum/client.h"
#include "forum/models.h"
#include "options.h"
#include "ui/worker.h"

namespace spark {

class App {
 public:
  App(Options opts, Config cfg);
  int Run();

 private:
  enum class Tab {
    Home = 0,
    News = 1,
    Contact = 2,
    Platform = 3,
    Search = 4,
    Forum = 5,
    Me = 6,
  };
  enum class Mode {
    Browse,
    Detail,
    ForumDetail,
    HomeManage,   // home-news picks
    ForumManage,  // Flarum moderation menu
    ForumApi,     // raw JSON:API console for extension endpoints
    NewsMenu,     // news edit / history / delete
    NewsHistory,  // news revision list
    LoginMenu,    // choose Spark ORG or forum sign-in
    Login,        // Spark ORG device-code flow
    ForumLogin,   // Flarum username / password form
    Settings,          // settings menu
    InstanceSettings,  // service sources / instance addresses
    SourceReview,      // review source changes + token probes
  };

  enum class ForumAction {
    Sticky,
    Lock,
    Hide,
    Tags,
    Rename,
    Delete,
  };

  struct ListState {
    std::vector<ListItem> items;
    int selected = 0;
    int total = 0;
    bool hasMore = false;
    bool loaded = false;
    bool loading = false;
    int generation = 0;
  };

  struct LoginState {
    bool active = false;
    std::string status;
    DeviceCode code;
  };

  struct LoginMenuState {
    int selected = 0;
  };

  struct ForumLoginState {
    int field = 0;  // 0 username / email, 1 password
    std::size_t cursor = 0;
    std::string username;
    std::string password;
    std::string status;
    bool busy = false;
  };

  struct ForumState {
    std::vector<ForumDiscussion> items;
    int selected = 0;
    bool loaded = false;
    bool loading = false;
    bool hasMore = false;
    std::string next;
    std::string error;
    int generation = 0;
  };

  struct ForumThreadState {
    ForumThread thread;
    bool loading = false;
    std::string error;
    int scroll = 0;
    int generation = 0;
  };

  struct HomePickEntry {
    std::string slug;
    std::string title;
    bool picked = false;
  };

  struct HomeManageState {
    std::vector<HomePickEntry> items;
    int selected = 0;
    int max = 5;
    bool loading = false;
    bool busy = false;
    std::string error;
    std::string message;
    int generation = 0;
  };

  struct ForumManageState {
    ForumDiscussion discussion;
    bool from_detail = false;
    int selected = 0;
    bool busy = false;
    std::string message;
    bool renaming = false;
    std::string title;
    std::size_t title_cursor = 0;
    int confirm = 0;  // 0 = none, otherwise ForumAction index in the menu
    int generation = 0;
  };

  struct NewsManageState {
    ListItem item;
    int selected = 0;
    bool busy = false;
    std::string message;
    int confirm = 0;  // 1 = delete confirmation
    int generation = 0;
  };

  struct HistoryEntry {
    std::string id;
    std::string op;
    std::string actor;
    std::string note;
    std::string createdAt;
    nlohmann::json payload;
  };

  struct NewsHistoryState {
    std::string slug;
    std::string title;
    std::vector<HistoryEntry> entries;
    int selected = 0;
    bool loading = false;
    std::string error;
    std::string message;
    int confirm = 0;
    int generation = 0;
  };

  struct ForumApiPreset {
    std::string name;
    std::string method;
    std::string path;
    std::string body;
  };

  struct SourceReviewState {
    Config pending;
    std::vector<std::string> changes;
    int spark_probe = 2;  // 2 = not needed, 1 ok, 0 rejected, -1 unreachable
    int forum_probe = 2;
    bool busy = false;
    bool logged_out_spark = false;
    bool logged_out_forum = false;
    int generation = 0;
    int base_generation = 0;
    std::string message;
  };

  struct ForumApiState {
    int method = 0;  // GET / POST / PATCH / DELETE
    std::string path = "/api/discussions";
    std::string body = "{}";
    std::string response;
    std::string status;
    std::size_t cursor = path.size();
    int scroll = 0;
    bool busy = false;
    bool from_detail = false;
    bool show_presets = false;
    int preset_selected = 0;
    int generation = 0;
  };

  struct SettingsState {
    int selected = 0;  // settings menu entry
    int field = 0;     // 0 sync, 1 main, 2 status, 3 forum
    std::string sync;
    std::string main;
    std::string status;
    std::string forum;
    std::size_t cursor = 0;
    std::string message;
    bool syncing = false;
  };

  struct HomeState {
    std::vector<ListItem> news;
    int total = 0;
    bool loaded = false;
    bool loading = false;
    int generation = 0;
    std::string error;
  };

  // Data loading ------------------------------------------------------------
  void LoadSiteSync();
  void LoadHome();
  void LoadList(Tab tab, bool append);
  void LoadMe();
  void LoadForum(bool append);
  void OpenForumDiscussion();
  void OpenForumThread(const ForumDiscussion& discussion);
  void OpenHomeManage();
  void LoadHomeManage();
  void ToggleHomePick();
  void ReorderHomePicks(int direction);
  void OpenForumManage(bool from_detail);
  void RunForumAction(ForumAction action);
  void SubmitForumRename();
  std::vector<ForumAction> ForumActions() const;
  void StartForumReply();
  void StartForumCreate();
  void StartForumTagEdit();
  void OpenForumApi();
  void SendForumApiRequest();
  std::vector<ForumApiPreset> ForumApiPresets() const;
  void StartSourceReview(Config pending, std::vector<std::string> changes);
  void ProbeSourceReview();
  void ApplySourceReview();
  bool SourceReviewBlocked() const;
  void OpenNewsMenu();
  void StartNewsEdit(bool is_new);
  void DeleteSelectedNews();
  void StartNewsHistory();
  void RevertSelectedHistory();
  void OpenNewsEditor(const nlohmann::json& body, bool is_new);
  std::optional<nlohmann::json> EditJsonDocument(
      const nlohmann::json& initial, const std::string& prefix,
      std::string& error);
  std::optional<std::string> EditTextDocument(
      const std::string& initial, const std::string& prefix,
      std::string& error);
  void RunSearch();
  void OpenDetail();
  void StartLogin();
  void PollLogin(DeviceCode code, int generation);
  void OnLoginSuccess(const DeviceToken& token);
  void CancelLogin();
  void OpenLoginMenu();
  void OpenForumLogin();
  void SubmitForumLogin();
  void OnForumLoginSuccess(const ForumAuth& auth,
                           const std::string& submitted_username);
  void CancelForumLogin();
  void Logout();
  void LogoutForum();
  void SwitchTab(Tab tab);
  void CycleLanguage();
  void CycleUiLanguage();
  void RefreshCurrent();
  void OpenCurrent();
  void ImportContactPgp();
  void StartStartupSync();
  void OpenSettings();
  void OpenInstanceSettings();
  bool SaveSettings();
  void SyncNow();
  void ApplyInstanceChange();
  void ResetContent();

  // Helpers -----------------------------------------------------------------
  ListState& ListFor(Tab tab);
  ListState& CurrentList();
  bool LoggedIn() const;
  bool ForumLoggedIn() const { return !cfg_.forum.token.empty(); }
  std::string ForumAccountLabel() const;
  std::string T(const std::string& zh, const std::string& en) const;
  std::string UiLanguagePreference() const;
  std::string UiLanguageDisplay() const;
  void SaveConfig();
  void SetStatus(std::string status);
  std::string Hints() const;
  std::string ItemUrl(const ListItem& item) const;
  void MoveSelection(int delta);

  // Rendering ---------------------------------------------------------------
  ftxui::Element Render();
  ftxui::Element Header();
  ftxui::Element TabBar();
  ftxui::Element Content();
  ftxui::Element HomeView();
  ftxui::Element ListView(ListState& list, bool showKind);
  ftxui::Element DetailView();
  ftxui::Element SearchView();
  ftxui::Element ForumView();
  ftxui::Element ForumDetailView();
  ftxui::Element HomeManageView();
  ftxui::Element ForumManageView();
  ftxui::Element ForumApiView();
  ftxui::Element SourceReviewView();
  ftxui::Element NewsMenuView();
  ftxui::Element NewsHistoryView();
  ftxui::Element MeView();
  ftxui::Element LoginMenuView();
  ftxui::Element LoginView();
  ftxui::Element ForumLoginView();
  ftxui::Element SettingsMenuView();
  ftxui::Element InstanceSettingsView();
  ftxui::Element HelpView();

  // Events ------------------------------------------------------------------
  bool HandleEvent(ftxui::Event event);
  bool HandleSearchKey(ftxui::Event event);
  bool HandleHomeManageKey(ftxui::Event event);
  bool HandleForumManageKey(ftxui::Event event);
  bool HandleForumApiKey(ftxui::Event event);
  bool HandleSourceReviewKey(ftxui::Event event);
  bool HandleNewsMenuKey(ftxui::Event event);
  bool HandleNewsHistoryKey(ftxui::Event event);
  bool HandleLoginMenuKey(ftxui::Event event);
  bool HandleForumLoginKey(ftxui::Event event);
  bool HandleSettingsMenuKey(ftxui::Event event);
  bool HandleInstanceSettingsKey(ftxui::Event event);
  std::string& SettingsField();

  Options opts_;
  Config cfg_;
  ApiClient api_;
  ForumClient forumApi_;
  ftxui::ScreenInteractive screen_ =
      ftxui::ScreenInteractive::Fullscreen();
  Worker worker_;

  SiteInfo site_;
  std::string lang_;
  Tab tab_ = Tab::News;
  Mode mode_ = Mode::Browse;
  bool help_ = false;
  int helpScroll_ = 0;
  bool quitConfirm_ = false;

  std::array<ListState, 3> lists_;  // news / contact / platform
  ListState search_;
  std::string searchQuery_;
  std::size_t searchCursor_ = 0;
  bool searched_ = false;

  HomeState home_;

  nlohmann::json detail_;
  std::string detailTitle_;
  std::string detailKind_;
  bool detailRaw_ = false;
  bool detailLoading_ = false;
  int detailScroll_ = 0;

  nlohmann::json me_;
  bool meRaw_ = false;
  bool meLoading_ = false;
  int meScroll_ = 0;

  LoginState login_;
  LoginMenuState loginMenu_;
  ForumLoginState forumLogin_;
  std::atomic<int> loginGeneration_{0};
  std::atomic<int> forumLoginGeneration_{0};

  ForumState forum_;
  ForumThreadState forumThread_;
  HomeManageState homeManage_;
  ForumManageState forumManage_;
  NewsManageState newsManage_;
  NewsHistoryState newsHistory_;
  ForumApiState forumApiTool_;
  SourceReviewState sourceReview_;

  // Bumped whenever the user saves instance settings; a startup sync that
  // completes afterwards is discarded so it cannot overwrite newer values.
  int instanceGeneration_ = 0;

  SettingsState settings_;

  std::string status_;
};

}  // namespace spark
