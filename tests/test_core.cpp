#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "api/models.h"
#include "config.h"
#include "forum/models.h"
#include "forum/render.h"
#include "service_source.h"
#include "ui/render_detail.h"
#include "util.h"

namespace {

int g_failures = 0;

void Check(bool condition, const char* expression, int line) {
  if (condition) return;
  std::fprintf(stderr, "FAIL line %d: %s\n", line, expression);
  ++g_failures;
}

#define CHECK(expr) Check((expr), #expr, __LINE__)

std::string TempPath(const char* name) {
  return (std::filesystem::temp_directory_path() / name).string();
}

std::string ReadFile(const std::string& path) {
  std::ifstream in(path);
  return std::string(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
}

}  // namespace

int main() {
  using namespace spark;

  // UTF-8 helpers -----------------------------------------------------------
  {
    const std::string text = "a中b";
    char32_t cp = 0;
    CHECK(Utf8Decode(text, 0, cp) == 1 && cp == U'a');
    CHECK(Utf8Decode(text, 1, cp) == 3 && cp == U'中');
    CHECK(Utf8Decode(text, 4, cp) == 1 && cp == U'b');
    CHECK(CodePointWidth(U'中') == 2);
    CHECK(CodePointWidth(U'a') == 1);

    CHECK(Utf8Next(text, 0) == 1);
    CHECK(Utf8Next(text, 1) == 4);
    CHECK(Utf8Prev(text, 4) == 1);
    CHECK(Utf8Prev(text, 1) == 0);
  }

  // Wrapping ----------------------------------------------------------------
  {
    const auto lines = WrapText("一二三四五", 4);
    CHECK(lines.size() == 3);
    CHECK(lines[0] == "一二");
    CHECK(lines[1] == "三四");
    CHECK(lines[2] == "五");

    const auto explicit_lines = WrapText("a\nb", 10);
    CHECK(explicit_lines.size() == 2);
    CHECK(explicit_lines[0] == "a");
    CHECK(explicit_lines[1] == "b");
  }

  // Ellipsizing -------------------------------------------------------------
  {
    CHECK(Ellipsize("hello", 10) == "hello");
    CHECK(Ellipsize("hello world", 8) == "hello w…");
    CHECK(Ellipsize("你好世界", 5) == "你好…");
    CHECK(Ellipsize("abc", 0).empty());
  }

  // URL encoding ------------------------------------------------------------
  {
    CHECK(UrlEncode("a b") == "a%20b");
    CHECK(UrlEncode("a/b?c=d") == "a%2Fb%3Fc%3Dd");
    CHECK(UrlEncode("abc-_.~") == "abc-_.~");
  }

  // Date accuracy -----------------------------------------------------------
  {
    const std::string start = "2026-09-25 12:34:56";
    CHECK(FormatDateByAccuracy(start, "year") == "2026");
    CHECK(FormatDateByAccuracy(start, "month") == "2026-09");
    CHECK(FormatDateByAccuracy(start, "day") == "2026-09-25");
    CHECK(FormatDateByAccuracy(start, "hour") == "2026-09-25 12");
    CHECK(FormatDateByAccuracy(start, "minute") == "2026-09-25 12:34");
    CHECK(FormatDateByAccuracy(start, "second") == start);
    CHECK(FormatDateByAccuracy("2026-09-25T12:34:56", "day") == "2026-09-25");
    CHECK(FormatDateByAccuracy("2026-09-25T12:34:56", "hour") ==
          "2026-09-25 12");
    CHECK(FormatDateByAccuracy("2026", "day") == "2026");
    CHECK(FormatDateByAccuracy("", "day").empty());
    CHECK(FormatDateByAccuracy(start, "unknown") == start);
  }

  // URL normalization -------------------------------------------------------
  {
    CHECK(NormalizeUrl("").has_value() == false);    CHECK(NormalizeUrl("   ").has_value() == false);
    CHECK(NormalizeUrl("sparkorg.dpdns.org").value() ==
          "https://sparkorg.dpdns.org");
    CHECK(NormalizeUrl("  https://example.org/  ").value() ==
          "https://example.org");
    CHECK(NormalizeUrl("http://127.0.0.1:8096/").value() ==
          "http://127.0.0.1:8096");
    CHECK(NormalizeUrl("ftp://example.org").has_value() == false);
    CHECK(NormalizeUrl("https://").has_value() == false);
    CHECK(NormalizeUrl("bad host").has_value() == false);
    CHECK(NormalizeUrl("https://[::1]:8096").value() ==
          "https://[::1]:8096");
  }

  // Instance precedence -----------------------------------------------------
  {
    InstanceConfig instance;
    CHECK(instance.EffectiveMain() == kDefaultMainUrl);
    CHECK(instance.EffectiveStatus() == kDefaultStatusUrl);

    instance.autoMain = "https://auto.example";
    instance.autoStatus = "https://status-auto.example";
    instance.autoForum = "https://forum-auto.example";
    CHECK(instance.EffectiveMain() == "https://auto.example");
    CHECK(instance.EffectiveStatus() == "https://status-auto.example");
    CHECK(instance.EffectiveForum() == "https://forum-auto.example");

    instance.mainOverride = "https://manual.example";
    instance.statusOverride = "https://status-manual.example";
    instance.forumOverride = "https://forum-manual.example";
    CHECK(instance.EffectiveMain() == "https://manual.example");
    CHECK(instance.EffectiveStatus() == "https://status-manual.example");
    CHECK(instance.EffectiveForum() == "https://forum-manual.example");
    CHECK(instance.MainIsManual());
    CHECK(instance.StatusIsManual());
    CHECK(instance.ForumIsManual());
  }

  // Service source parsing --------------------------------------------------
  {
    const auto json = ParseServiceSource(R"({
      "home_domain": "sparkorg.dpdns.org",
      "services": {
        "HOME": "sparkorg.dpdns.org",
        "FORUM": "forum.sparkorg.dpdns.org",
        "STATUS": "status.sparkorg.dpdns.org"
      }
    })");
    CHECK(json.ok);
    CHECK(json.value.home.has_value());
    CHECK(json.value.home.value() == "https://sparkorg.dpdns.org");
    CHECK(json.value.status.has_value());
    CHECK(json.value.status.value() == "https://status.sparkorg.dpdns.org");
    CHECK(json.value.forum.has_value());
    CHECK(json.value.forum.value() == "https://forum.sparkorg.dpdns.org");

    const auto fallback_home = ParseServiceSource(R"({
      "services": {"HOME": "home.example", "FORUM": "forum.example", "STATUS": "status.example"}
    })");
    CHECK(fallback_home.ok);
    CHECK(fallback_home.value.home.value() == "https://home.example");
    CHECK(fallback_home.value.forum.value() == "https://forum.example");

    const auto plain = ParseServiceSource("\n\n  example.org  \nignored\n");
    CHECK(plain.ok);
    CHECK(plain.value.home.has_value());
    CHECK(plain.value.home.value() == "https://example.org");
    CHECK(plain.value.status.has_value() == false);
    CHECK(plain.value.forum.has_value() == false);

    CHECK(ParseServiceSource("").ok == false);
    CHECK(ParseServiceSource("   \n").ok == false);
    CHECK(ParseServiceSource("{\"foo\": 1}").ok == false);
    CHECK(ParseServiceSource("{not json").ok == false);
  }

  // API models --------------------------------------------------------------
  {
    const auto page = nlohmann::json::parse(R"({
      "count": 1, "total": 7, "limit": 1, "offset": 0, "hasMore": true,
      "items": [{
        "slug": "revert-domain",
        "date": {"start": "2026-09-25 00:00:00", "accuracy": "day"},
        "title": "标题",
        "url": "/news#news-revert-domain",
        "priority": -1,
        "active": true
      }]
    })");
    const ListPage parsed = ParseList(page, "news");
    CHECK(parsed.total == 7);
    CHECK(parsed.hasMore);
    CHECK(parsed.items.size() == 1);
    CHECK(parsed.items[0].kind == "news");
    CHECK(parsed.items[0].title == "标题");
    CHECK(parsed.items[0].subtitle == "2026-09-25");
  }

  {
    const auto search = nlohmann::json::parse(R"({
      "total": 2,
      "groups": {
        "news": {"count": 1, "items": [{"slug": "n", "title": "N", "url": "/n", "priority": 0, "active": true}]},
        "contact": {"count": 1, "items": [{"slug": "c", "name": "C", "title": "T", "url": "/c", "priority": 1}]}
      }
    })");
    const SearchResult parsed = ParseSearch(search);
    CHECK(parsed.total == 2);
    CHECK(parsed.items.size() == 2);
    CHECK(parsed.items[0].kind == "news");
    CHECK(parsed.items[0].subtitle.find("新闻") != std::string::npos);
    CHECK(parsed.items[1].kind == "contact");
    CHECK(parsed.items[1].title == "C");
  }

  {
    const auto device = nlohmann::json::parse(R"({
      "device_code": "dc", "user_code": "ABCD-EFGH",
      "verification_uri": "https://example/device",
      "verification_uri_complete": "https://example/device?code=ABCD-EFGH",
      "expires_in": 600, "interval": 5, "scopes": []
    })");
    const DeviceCode parsed = ParseDeviceCode(device);
    CHECK(parsed.deviceCode == "dc");
    CHECK(parsed.userCode == "ABCD-EFGH");
    CHECK(parsed.expiresIn == 600);
    CHECK(parsed.interval == 5);
  }

  // TOML config round-trip --------------------------------------------------
  {
    Config cfg;
    cfg.configPath = TempPath("spark-tui-test-config.toml");
    cfg.instance.syncUrl = "https://sync.example/services.json";
    cfg.instance.mainOverride = "https://127.0.0.1:8096";
    cfg.instance.statusOverride = "https://status.example";
    cfg.instance.forumOverride = "https://forum.example";
    cfg.instance.autoMain = "https://auto.example";
    cfg.instance.autoStatus = "https://status-auto.example";
    cfg.instance.autoForum = "https://forum-auto.example";
    cfg.language = "en-us";
    cfg.insecure = true;
    cfg.syncOnStart = false;
    cfg.showRawDetails = true;
    cfg.token.plaintext = "spark_test";
    cfg.token.id = "42";
    cfg.token.label = "test";
    cfg.token.username = "jianyin";
    cfg.forum.token = "flarum_test";
    cfg.forum.userId = "2";
    cfg.forum.username = "hoewllo";
    cfg.forum.displayName = "Jianyin Li";

    CHECK(cfg.Save());

    const std::string text = ReadFile(cfg.configPath);
    CHECK(text.find("[instance]") != std::string::npos);
    CHECK(text.find("sync_url") != std::string::npos);
    CHECK(text.find("auto_main") != std::string::npos);
    CHECK(text.find("[auth]") != std::string::npos);
    CHECK(text.find("[forum]") != std::string::npos);
    CHECK(text.find("flarum_test") != std::string::npos);

    const Config loaded = Config::Load(cfg.configPath);
    CHECK(loaded.configPath == cfg.configPath);
    CHECK(loaded.instance.syncUrl == cfg.instance.syncUrl);
    CHECK(loaded.instance.mainOverride == cfg.instance.mainOverride);
    CHECK(loaded.instance.statusOverride == cfg.instance.statusOverride);
    CHECK(loaded.instance.forumOverride == cfg.instance.forumOverride);
    CHECK(loaded.instance.autoMain == cfg.instance.autoMain);
    CHECK(loaded.instance.autoStatus == cfg.instance.autoStatus);
    CHECK(loaded.instance.autoForum == cfg.instance.autoForum);
    CHECK(loaded.EffectiveMain() == cfg.instance.mainOverride.value());
    CHECK(loaded.EffectiveForum() == cfg.instance.forumOverride.value());
    CHECK(loaded.language == cfg.language);
    CHECK(loaded.insecure);
    CHECK(loaded.syncOnStart == false);
    CHECK(loaded.showRawDetails);
    CHECK(loaded.token.plaintext == "spark_test");
    CHECK(loaded.token.id == "42");
    CHECK(loaded.token.label == "test");
    CHECK(loaded.token.username == "jianyin");
    CHECK(loaded.forum.token == "flarum_test");
    CHECK(loaded.forum.userId == "2");
    CHECK(loaded.forum.username == "hoewllo");
    CHECK(loaded.forum.displayName == "Jianyin Li");

    std::remove(cfg.configPath.c_str());
  }

  // Legacy JSON migration ---------------------------------------------------
  {
    const std::string jsonPath = TempPath("spark-tui-legacy.json");
    const std::string tomlPath = TempPath("spark-tui-legacy.toml");
    {
      std::ofstream out(jsonPath, std::ios::trunc);
      out << R"({
        "baseUrl": "https://127.0.0.1:8096",
        "language": "zh-cn",
        "insecure": true,
        "token": {
          "plaintext": "spark_legacy",
          "id": 7,
          "label": "old",
          "username": "olduser"
        }
      })";
    }

    const Config migrated = Config::Load(tomlPath);
    CHECK(migrated.configPath == tomlPath);
    CHECK(migrated.instance.mainOverride.has_value());
    CHECK(migrated.instance.mainOverride.value() == "https://127.0.0.1:8096");
    CHECK(migrated.language == "zh-cn");
    CHECK(migrated.insecure);
    CHECK(migrated.token.plaintext == "spark_legacy");
    CHECK(migrated.token.id == "7");
    CHECK(migrated.token.label == "old");
    CHECK(migrated.token.username == "olduser");

    // Saving writes TOML at the new path.
    CHECK(migrated.Save());
    CHECK(ReadFile(tomlPath).find("[instance]") != std::string::npos);

    std::remove(jsonPath.c_str());
    std::remove(tomlPath.c_str());
  }

  // Detail rendering --------------------------------------------------------
  {
    const auto news = nlohmann::json::parse(R"({
      "slug": "revert-domain",
      "date": {"start": "2026-09-25 00:00:00", "accuracy": "day"},
      "title": "标题",
      "desc": "<p>第一段</p><p>第二段 &amp; 更多</p>",
      "url": "/news#news-revert-domain",
      "priority": -1,
      "active": false,
      "place": {"desc": "北京", "geo": "39.9,116.4"},
      "images": [{"url": "https://example/image.png", "alt": "配图"}]
    })");

    const auto rendered = RenderDetailLines("news", news, 60, false);
    std::string joined;
    for (const auto& line : rendered) joined += line + "\n";
    CHECK(joined.find("日期: 2026-09-25") != std::string::npos);
    CHECK(joined.find("地点: 北京") != std::string::npos);
    CHECK(joined.find("停用") == std::string::npos);
    CHECK(joined.find("正文:") != std::string::npos);
    CHECK(joined.find("第一段") != std::string::npos);
    CHECK(joined.find("第二段 & 更多") != std::string::npos);
    CHECK(joined.find("图片:") != std::string::npos);
    CHECK(joined.find("配图: https://example/image.png") != std::string::npos);
    CHECK(joined.find("原文: /news#news-revert-domain") != std::string::npos);
    CHECK(joined.find("slug") == std::string::npos);
    CHECK(joined.find("priority") == std::string::npos);

    const auto raw = RenderDetailLines("news", news, 60, true);
    std::string raw_joined;
    for (const auto& line : raw) raw_joined += line + "\n";
    CHECK(raw_joined.find("slug: revert-domain") != std::string::npos);
    CHECK(raw_joined.find("priority: -1") != std::string::npos);
  }

  {
    const auto contact = nlohmann::json::parse(R"({
      "slug": "jianyin",
      "name": "Jianyin Li",
      "title": "技术首席",
      "tel": ["+86 000"],
      "email": ["a@example.org"],
      "homepage": [{"name": "主页", "url": "https://example.org"}],
      "pgpFingerprint": "ABCD1234",
      "pgpKey": "-----BEGIN PGP PUBLIC KEY BLOCK-----"
    })");

    const auto rendered = RenderDetailLines("contact", contact, 60, false);
    std::string joined;
    for (const auto& line : rendered) joined += line + "\n";
    CHECK(joined.find("姓名: Jianyin Li") != std::string::npos);
    CHECK(joined.find("职务: 技术首席") != std::string::npos);
    CHECK(joined.find("电话:") != std::string::npos);
    CHECK(joined.find("邮箱:") != std::string::npos);
    CHECK(joined.find("- 主页: https://example.org") != std::string::npos);
    CHECK(joined.find("PGP 指纹: ABCD1234") != std::string::npos);
    CHECK(joined.find("PGP 公钥: 已省略") != std::string::npos);
    CHECK(joined.find("BEGIN PGP") == std::string::npos);
  }

  {
    const auto platform = nlohmann::json::parse(R"({
      "slug": "forum",
      "name": "论坛",
      "desc": "社区交流",
      "button": "进入",
      "url": "https://forum.example",
      "pageUrl": "/platform#platform-forum",
      "priority": 1
    })");

    const auto rendered = RenderDetailLines("platform", platform, 60, false);
    std::string joined;
    for (const auto& line : rendered) joined += line + "\n";
    CHECK(joined.find("名称: 论坛") != std::string::npos);
    CHECK(joined.find("简介:") != std::string::npos);
    CHECK(joined.find("按钮: 进入") != std::string::npos);
    CHECK(joined.find("地址: https://forum.example") != std::string::npos);
    CHECK(joined.find("站内页: /platform#platform-forum") !=
          std::string::npos);
  }

  {
    const auto me = nlohmann::json::parse(R"({
      "self": true,
      "user": {
        "username": "jianyin",
        "fullName": "Jianyin Li",
        "role": "admin",
        "lastSeenAt": "2026-11-11 00:00:00",
        "avatar": null,
        "displayEmail": "a@example.org",
        "emails": ["a@example.org"],
        "pgpFingerprint": "ABCD1234",
        "pgpAlgorithm": "Ed25519",
        "pgpPublicKey": "-----BEGIN PGP PUBLIC KEY BLOCK-----",
        "pgpRevoked": false
      }
    })");

    const auto rendered = RenderMeLines(me, 60, false);
    std::string joined;
    for (const auto& line : rendered) joined += line + "\n";
    CHECK(joined.find("用户名: jianyin") != std::string::npos);
    CHECK(joined.find("显示名: Jianyin Li") != std::string::npos);
    CHECK(joined.find("角色: admin") != std::string::npos);
    CHECK(joined.find("头像: 无") != std::string::npos);
    CHECK(joined.find("PGP 指纹: ABCD1234") != std::string::npos);
    CHECK(joined.find("PGP 公钥: 已省略") != std::string::npos);
    CHECK(joined.find("BEGIN PGP") == std::string::npos);

    const auto raw = RenderMeLines(me, 60, true);
    std::string raw_joined;
    for (const auto& line : raw) raw_joined += line + "\n";
    CHECK(raw_joined.find("pgpPublicKey") != std::string::npos);

    // The live /me.json uses display_name; fullName remains supported.
    const auto snake = nlohmann::json::parse(R"({
      "user": {"username": "admin", "display_name": "测试"}
    })");
    const auto snake_rendered = RenderMeLines(snake, 60, false);
    std::string snake_joined;
    for (const auto& line : snake_rendered) snake_joined += line + "\n";
    CHECK(snake_joined.find("显示名: 测试") != std::string::npos);
  }

  // Forum / Flarum JSON:API parsing -----------------------------------------
  {
    const auto list = nlohmann::json::parse(R"({
      "links": {
        "next": "https://forum.example/api/discussions?page[offset]=2&page[limit]=2&sort=-lastPostedAt"
      },
      "data": [
        {
          "type": "discussions",
          "id": "11",
          "attributes": {
            "title": "新站点",
            "slug": "11-xin-zhan-dian",
            "commentCount": 3,
            "createdAt": "2026-10-07T14:01:20+00:00",
            "lastPostedAt": "2026-10-08T09:30:00+00:00",
            "isApproved": true,
            "isSticky": false,
            "isLocked": false
          },
          "relationships": {
            "lastPostedUser": {"data": {"type": "users", "id": "2"}},
            "firstPost": {"data": {"type": "posts", "id": "34"}},
            "tags": {"data": [
              {"type": "tags", "id": "1"},
              {"type": "tags", "id": "2"}
            ]}
          }
        },
        {
          "type": "discussions",
          "id": "8",
          "attributes": {
            "title": "公告",
            "slug": "8-gong-gao",
            "commentCount": 1,
            "createdAt": "2026-06-20T09:00:34+00:00",
            "lastPostedAt": "2026-06-20T09:07:00+00:00",
            "isApproved": true,
            "isSticky": true,
            "isLocked": true
          },
          "relationships": {
            "user": {"data": {"type": "users", "id": "2"}},
            "tags": {"data": []}
          }
        }
      ],
      "included": [
        {
          "type": "users",
          "id": "2",
          "attributes": {"username": "hoewllo", "displayName": "Jianyin Li"}
        },
        {
          "type": "tags",
          "id": "1",
          "attributes": {"name": "General", "slug": "general", "position": 0}
        },
        {
          "type": "tags",
          "id": "2",
          "attributes": {"name": "Service", "slug": "service", "position": null}
        }
      ]
    })");

    const ForumPage page = ParseForumDiscussions(list);
    CHECK(page.items.size() == 2);
    CHECK(page.hasMore);
    CHECK(page.next.find("page[offset]=2") != std::string::npos);
    CHECK(page.items[0].title == "新站点");
    CHECK(page.items[0].author == "Jianyin Li");
    CHECK(page.items[0].commentCount == 3);
    CHECK(page.items[0].tagNames.size() == 2);
    CHECK(page.items[0].tagNames[0] == "General");
    CHECK(page.items[0].firstPostId == "34");
    CHECK(!page.items[0].sticky);
    CHECK(page.items[1].sticky);
    CHECK(page.items[1].locked);
    CHECK(FormatForumDate(page.items[0].lastPostedAt) == "2026-10-08 09:30");

    const auto thread_json = nlohmann::json::parse(R"({
      "data": {
        "type": "discussions",
        "id": "11",
        "attributes": {
          "title": "新站点",
          "slug": "11-xin-zhan-dian",
          "commentCount": 1,
          "createdAt": "2026-10-07T14:01:20+00:00",
          "isApproved": true,
          "isSticky": false,
          "isLocked": false
        },
        "relationships": {
          "posts": {"data": [
            {"type": "posts", "id": "34"},
            {"type": "posts", "id": "35"}
          ]},
          "tags": {"data": [
            {"type": "tags", "id": "2"},
            {"type": "tags", "id": "1"}
          ]}
        }
      },
      "included": [
        {"type": "users", "id": "2",
         "attributes": {"username": "hoewllo", "displayName": "Jianyin Li"}},
        {
          "type": "posts",
          "id": "34",
          "attributes": {
            "number": 1,
            "createdAt": "2026-10-07T14:01:20+00:00",
            "contentType": "comment",
            "contentHtml": "<p><a href=\"https://new.example\">New Site</a></p><p>欢迎探索</p>"
          },
          "relationships": {"user": {"data": {"type": "users", "id": "2"}}}
        },
        {
          "type": "posts",
          "id": "35",
          "attributes": {
            "number": 2,
            "createdAt": "2026-10-07T14:02:20+00:00",
            "contentType": "discussionTagged",
            "content": [[], ["1"]]
          },
          "relationships": {"user": {"data": {"type": "users", "id": "2"}}}
        },
        {"type": "tags", "id": "1",
         "attributes": {"name": "General", "position": 0}},
        {"type": "tags", "id": "2",
         "attributes": {"name": "Service", "position": null}}
      ]
    })");

    const ForumThread thread = ParseForumThread(thread_json);
    CHECK(thread.discussion.title == "新站点");
    CHECK(thread.tags.size() == 2);
    CHECK(thread.tags[0].name == "General");
    CHECK(thread.posts.size() == 2);
    CHECK(thread.posts[0].number == 1);
    CHECK(thread.posts[0].author == "Jianyin Li");
    CHECK(thread.posts[1].contentType == "discussionTagged");

    const auto lines = RenderForumThreadLines(thread, 60, "zh-cn");
    std::string joined;
    for (const auto& line : lines) joined += line + "\n";
    CHECK(joined.find("新站点") != std::string::npos);
    CHECK(joined.find("Jianyin Li") != std::string::npos);
    CHECK(joined.find("New Site") != std::string::npos);
    CHECK(joined.find("欢迎探索") != std::string::npos);
    CHECK(joined.find("<p>") == std::string::npos);
    CHECK(joined.find("discussionTagged") == std::string::npos);

    const auto auth = ParseForumAuth(
        nlohmann::json::parse(R"({"token": "flarum_token", "userId": 2})"));
    CHECK(auth.token == "flarum_token");
    CHECK(auth.userId == "2");

    const auto user = ParseForumUser(nlohmann::json::parse(R"({
      "data": {"type": "users", "id": "2",
               "attributes": {"username": "hoewllo",
                              "displayName": "Jianyin Li"}}
    })"));
    CHECK(user.id == "2");
    CHECK(ForumDisplayName(user) == "Jianyin Li");
  }

  // HTML stripping ----------------------------------------------------------
  {
    CHECK(StripHtml("a<br>b") == "a\nb");
    CHECK(StripHtml("<p>Hello &amp; <b>world</b></p><p>Second</p>") ==
          "Hello & world\n\nSecond");
    CHECK(StripHtml("&lt;tag&gt; &quot;x&quot; &#39;y&#39;") ==
          "<tag> \"x\" 'y'");
  }

  if (g_failures != 0) {
    std::fprintf(stderr, "%d test(s) failed\n", g_failures);
    return EXIT_FAILURE;
  }
  std::printf("all tests passed\n");
  return EXIT_SUCCESS;
}
