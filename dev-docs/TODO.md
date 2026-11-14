# 论坛功能计划（Flarum）

> 状态：阶段 1–3 已完成（匿名只读浏览 + 论坛登录）；写操作待实现
>
> 目标实例：`services.FORUM`，默认 `https://forum.sparkorg.dpdns.org`
>
> 相关配置：`InstanceConfig::forumOverride` / `autoForum` / `EffectiveForum()`
>
> 接口探测报告：`~/Projects/flarum-api.txt`（2026-11-14）

## 1. 背景

服务源 `services.json` 已经提供论坛地址：

```json
{
  "home_domain": "sparkorg.dpdns.org",
  "services": {
    "HOME": "sparkorg.dpdns.org",
    "FORUM": "forum.sparkorg.dpdns.org",
    "STATUS": "status.sparkorg.dpdns.org"
  }
}
```

spark-tui 已经把论坛地址纳入实例配置：

- 手动覆盖：`[instance] forum = "..."`
- 自动缓存：`[instance] auto_forum = "..."`
- 优先级：手动 → 自动 → 内置默认
- 设置页 `s` →「服务源与实例地址」可编辑，`--forum-url` / `SPARK_TUI_FORUM_URL` 可覆盖

本阶段只做地址管理，不发起 Flarum 请求。论坛功能按本文档分阶段实现。

论坛已于 2026-11-14 恢复，`https://forum.sparkorg.dpdns.org/api`
匿名请求返回 `200 application/vnd.api+json`。实现仍然坚持：

- 论坛失败隔离，不影响新闻 / 联系 / 平台；
- 用可注入的 base URL 和单元测试 fixture 覆盖解析层；
- 真实联调覆盖列表、详情、分页、登录错误路径。

## 2. 设计原则

1. **论坛是独立后端**：Flarum 有自己的用户、会话、权限和 API，不复用
   Spark ORG API v2 的 `spark_...` 个人令牌。
2. **地址可切换**：所有请求都从 `cfg.instance.EffectiveForum()` 取 base URL，
   不硬编码 `forum.sparkorg.dpdns.org`。
3. **只读优先**：先实现浏览（讨论列表、详情、帖子、标签、搜索），
   再考虑发帖 / 回复 / 编辑。
4. **失败隔离**：论坛请求失败只影响论坛页面，不阻塞新闻、联系、平台。
5. **安全**：论坛令牌单独存储，文件权限 `0600`，不写日志，不保存密码。
6. **可测试**：JSON:API 解析、分页、错误映射都用 fixture 做单元测试。

## 3. Flarum API 速查

Flarum 使用 JSON:API（`application/vnd.api+json`）。常用端点：

| 方法 | 路径 | 用途 |
| --- | --- | --- |
| GET | `/api` | 论坛基本信息（标题、baseUrl、版本等） |
| GET | `/api/discussions` | 讨论列表 |
| GET | `/api/discussions/{id}` | 单个讨论 |
| GET | `/api/discussions/{id}/posts` | 讨论内帖子 |
| GET | `/api/tags` | 标签列表 |
| GET | `/api/users/{id}` | 用户资料 |
| GET | `/api/discussions?filter[q]=关键词` | 搜索讨论 |
| POST | `/api/token` | 用户名/邮箱 + 密码换取令牌 |
| POST | `/api/discussions` | 创建讨论（后续） |
| POST | `/api/posts` | 回复（后续） |

分页参数：

```text
?page[limit]=20&page[offset]=0
```

响应里优先使用 `links.next` 判断是否还有下一页。排序示例：

```text
?sort=-lastPostedAt
?sort=-createdAt
```

鉴权：

```http
Authorization: Token <flarum-token>
```

`POST /api/token` 请求体示例：

```json
{
  "identification": "username-or-email",
  "password": "……",
  "remember": true
}
```

响应中取 `token`。令牌只存本地，不写日志；退出时删除本地令牌即可
（Flarum 是否支持服务端吊销取决于版本 / 扩展，不能假设有 revoke 端点）。

## 4. 分阶段实现

### 阶段 0：配置与服务源（已完成）

- [x] `InstanceConfig` 增加 `forumOverride` / `autoForum` / `EffectiveForum()`
- [x] TOML 读写 `forum` / `auto_forum`
- [x] 服务源解析 `services.FORUM`
- [x] 设置页 →「服务源与实例地址」中的第四个地址字段
- [x] `--forum-url` / `SPARK_TUI_FORUM_URL`
- [x] 单元测试（解析、优先级、配置往返）

### 阶段 1：Flarum 只读客户端

新增 `src/forum/`：

```text
src/forum/
├── models.h/.cpp     # ForumInfo, Discussion, Post, Tag, User
├── client.h/.cpp     # ForumClient（线程安全，base URL 可切换）
└── json.cpp          # JSON:API 解析（可单测）
```

`ForumClient` 与 `ApiClient` 分离：

```cpp
class ForumClient {
 public:
  ForumClient(std::string baseUrl, bool insecure,
              std::string token = {});
  void SetBaseUrl(std::string baseUrl);
  void SetToken(std::string token);

  Result<ForumInfo> Info();
  Result<DiscussionPage> Discussions(int limit, int offset,
                                     const std::string& sort,
                                     const std::string& query);
  Result<Discussion> Discussion(const std::string& id);
  Result<PostPage> Posts(const std::string& discussionId,
                         int limit, int offset);
  Result<std::vector<Tag>> Tags();
};
```

实现要点：

- 复用 `cpp-httplib` + OpenSSL；`Accept: application/vnd.api+json`；
- 每次请求构造独立连接，base URL / token 用互斥量保护（参考 `ApiClient`）；
- 解析 `data` / `included` / `links` / `meta`；
- 讨论列表的 `relationships` 里通常有 `user`、`lastPostedUser`、`tags`；
  用 `included` 建立 id → 对象索引；
- 帖子内容优先取 `attributes.content`（Markdown），同时保留
  `contentHtml` 备用；
- 错误映射：HTTP 401/403 → 需要登录 / 无权限；404 → 不存在；
  5xx / 网络错误 → 论坛不可达；JSON 解析失败 → `parse_error`。

### 阶段 2：论坛浏览 UI

实际标签顺序（`Me` 保持在最后）：

```text
1 主页  2 新闻  3 联系  4 平台  5 搜索  6 论坛  7 我的
```

已实现：

- `Tab` 枚举新增 `Forum`，`7 我的` 取代原来的 `6 我的`；
- `TabBar` / 数字键 / `Tab` / `TabReverse` 使用 7 项；
- 论坛列表分页（`n`）、刷新（`r`）、`o` 打开原帖；
- 详情页展示首帖 + 回复，支持滚动、`o`、`r`、`Esc`；
- 论坛不可达时显示清晰状态，不阻塞其它标签；
- 标签作为列表中的 `#标签` 展示；论坛内搜索 / 标签筛选后续再做。

渲染建议：

- 标题、作者、时间、标签、回复数、浏览数；
- 帖子正文用简单 Markdown 子集渲染（标题、粗体、斜体、行内代码、代码块、
  链接、引用、列表）；图片先显示链接；
- 长文按现有 `WrapText` 处理 CJK 宽度；
- `contentHtml` 只在 Markdown 缺失时使用，并做最小 HTML 去标签。

### 阶段 3：论坛登录

配置增加独立表（不要混用 `[auth]`）：

```toml
[forum]
token = '...'
username = '...'
user_id = '...'
display_name = '...'
```

已实现：

- `L` 打开登录菜单，选择「论坛账号」进入 Flarum 登录；
- 用户名/邮箱 + 密码 → `POST /api/token`；
- 只保存返回的 token，绝不保存密码；
- 令牌写入配置文件，文件保持 `chmod 0600`；
- 论坛页头部显示已登录账号或「匿名浏览」；
- 退出论坛登录只清本地 token；
- 登录成功后用 `GET /api/users/{id}` 读取显示名；
- 错误提示：401 / 403 / 网络错误均落在状态栏，表单保留以便重试。

### 阶段 4：写操作（后续）

按优先级：

1. 回复帖子（`POST /api/posts`）；
2. 创建讨论（`POST /api/discussions`，需要标题、内容、标签）；
3. 编辑 / 删除自己的帖子；
4. 点赞 / 举报等扩展功能（取决于论坛启用的扩展）。

写操作前必须确认：

- 当前用户有权限；
- 标签是必填还是可选；
- 是否需要 CSRF / 额外头（Flarum 令牌鉴权通常不需要）；
- 失败时不要丢失用户已输入的内容。

## 5. 测试计划

单元测试（不依赖网络）：

- [x] `services.json` 中 `services.FORUM` 解析
- [x] 论坛地址优先级 / TOML 往返
- [x] 讨论列表解析（`data` + `included` + `links.next`）
- [x] 讨论详情 / 帖子解析（含系统事件帖过滤）
- [x] 标签 / 用户解析
- [x] 论坛令牌配置往返
- [x] HTML 去标签与文本渲染
- [ ] 错误映射的独立单测（401 / 403 / 404 / 5xx）
- [ ] 论坛内搜索 / 标签筛选

联调：

- [x] 讨论列表加载
- [x] 打开讨论、加载回复
- [x] 匿名浏览状态
- [x] 论坛登录表单 / 错误提示
- [x] 论坛不可达时其它标签仍正常
- [ ] 真实论坛账号登录成功路径
- [ ] 讨论列表分页（当前数据不足一页）
- [ ] 论坛地址切换到备用实例

## 6. 验收标准

- 论坛地址可通过服务源自动更新，也可手动覆盖；
- 不配置论坛地址时使用默认 `https://forum.sparkorg.dpdns.org`；
- 论坛不可达时 UI 有明确提示，且新闻等功能不受影响；
- 只读浏览完整可用：列表、分页、详情、回复、搜索、标签；
- 论坛令牌与 Spark 令牌完全隔离；
- 单元测试覆盖解析和错误路径；
- `.deb` 仍能正常构建和安装。

## 7. 待确认问题

- 论坛 Flarum 版本和启用的扩展（标签、语言、Markdown 渲染器）；
- 是否允许游客浏览；哪些板块需要登录；
- 搜索使用 Flarum 原生 `filter[q]` 还是本地过滤；
- 是否需要支持多论坛实例快速切换；
- 写操作的范围和权限模型；
- 论坛当前 `HTTP 530` 的恢复时间，联调前先确认源站状态。
