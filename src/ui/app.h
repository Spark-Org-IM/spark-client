#pragma once

#include <array>
#include <atomic>
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
    LoginMenu,    // choose Spark ORG or forum sign-in
    Login,        // Spark ORG device-code flow
    ForumLogin,   // Flarum username / password form
    Settings,          // settings menu
    InstanceSettings,  // service sources / instance addresses
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

  // Bumped whenever the user saves instance settings; a startup sync that
  // completes afterwards is discarded so it cannot overwrite newer values.
  int instanceGeneration_ = 0;

  SettingsState settings_;

  std::string status_;
};

}  // namespace spark
