# spark-tui

Spark ORG API v2 的终端客户端，基于 [FTXUI](https://github.com/ArthurSonzogni/FTXUI)
构建，使用 C++20。

## 功能

- 启动主页：站点名称与最近新闻预览；服务地址等技术信息收进设置
- 浏览新闻 / 联系 / 平台（分页、详情、搜索）
- 论坛只读浏览：主题列表、主题详情 / 回复、标签、分页
- 详情默认渲染为可读内容；按 `x` 临时切换底层 JSON
- 新闻日期按接口声明的精度显示（年 / 月 / 日 / 时 / 分 / 秒）
- 界面语言：跟随系统 / 中文 / English（设置页持久化；`l` 临时切换不保存）
- 联系人详情按 `i` 导入联系人的 PGP 公钥（`gpg --import`）
- TOML 配置文件；可从旧版 `config.json` 自动迁移
- 服务源管理：自动从 `services.json` 同步主服务器 / 状态服务器 / 论坛服务器
  地址，支持手动覆盖（`s` 打开设置页）
- Spark ORG 设备码登录 + 独立的 Flarum 论坛账号登录（用户名 / 密码只用于换取令牌）
- 令牌保存在 `~/.config/spark-tui/config.toml`（权限 `0600`）
- 可选在浏览器中打开当前条目（`xdg-open`）
- CPack 生成 `.deb` 包

## 依赖

构建时：

- CMake ≥ 3.20
- 支持 C++20 的编译器（GCC 11+ / Clang 14+）
- OpenSSL 开发头文件（`libssl-dev`）
- FTXUI：优先使用系统包（Debian 的 `libftxui-dev`）；找不到时通过
  `FetchContent` 拉取 v6.1.9。强制要求系统包：`-DSPARK_TUI_USE_SYSTEM_FTXUI=ON`。

`nlohmann/json`、`cpp-httplib` 与 `toml++` 以头文件形式放在
`third_party/`，不需要额外安装。

Debian/Ubuntu 安装构建依赖：

```bash
sudo apt install build-essential cmake libssl-dev
# 可选：使用系统 FTXUI
sudo apt install libftxui-dev
```

## 构建

Debian/Ubuntu 推荐直接使用系统 FTXUI：

```bash
sudo apt install libftxui-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DSPARK_TUI_USE_SYSTEM_FTXUI=ON
cmake --build build
```

CMake 默认也会自动检测系统 FTXUI；检测不到时才通过 FetchContent 联网拉取
v6.1.9。加 `-DSPARK_TUI_USE_SYSTEM_FTXUI=ON` 可以强制使用系统包，避免联网。

运行：

```bash
./build/spark-tui
```

## 本机开发（Service-Newsite，端口 8096）

8096 是 HTTPS（自签名证书），需要 `-k`。使用 `--no-sync` 可以避免启动时
从服务源切换走本机地址：

```bash
./build/spark-tui --base-url https://127.0.0.1:8096 --insecure --no-sync
```

或使用环境变量：

```bash
SPARK_TUI_BASE_URL=https://127.0.0.1:8096 \
SPARK_TUI_INSECURE=1 \
SPARK_TUI_NO_SYNC=1 \
./build/spark-tui
```

## 配置

默认配置文件：`~/.config/spark-tui/config.toml`
（可用 `--config` 或 `SPARK_TUI_CONFIG` 覆盖）。

```toml
[instance]
# 服务源：JSON（home_domain + services.STATUS）或纯文本裸域名
sync_url = 'https://spark-org-im.github.io/api/services.json'
# 启动时自动同步服务源
sync_on_start = true

# 手动覆盖；省略 / 留空表示自动（按优先级回退）
# main = 'https://sparkorg.dpdns.org'
# status = 'https://status.sparkorg.dpdns.org'
# forum = 'https://forum.sparkorg.dpdns.org'

# 上次成功同步的缓存，由客户端维护
auto_main = 'https://sparkorg.dpdns.org'
auto_status = 'https://status.sparkorg.dpdns.org'
auto_forum = 'https://forum.sparkorg.dpdns.org'

[ui]
language = 'zh-cn'
insecure = false
# 详情默认显示底层 JSON；false = 渲染（默认）
show_raw_details = false

[auth]
token = 'spark_…'
token_id = '12'
token_label = 'Spark ORG Android'
username = 'jianyin'

# Flarum 论坛账号：登录成功后自动写入，退出论坛登录时删除。
# 密码不会保存。
[forum]
token = 'flarum_…'
user_id = '2'
username = 'hoewllo'
display_name = 'Jianyin Li'
```

### 服务源与地址优先级

与 Android 客户端一致，三个地址分别是：

| 地址 | 默认值 | 说明 |
| --- | --- | --- |
| 同步源 | `https://spark-org-im.github.io/api/services.json` | 必填，读取主站 / 状态页 / 论坛地址 |
| 主服务器 | `https://sparkorg.dpdns.org` | 业务 API，可手动覆盖 |
| 状态服务器 | `https://status.sparkorg.dpdns.org` | 当前仅缓存 / 展示 |
| 论坛服务器 | `https://forum.sparkorg.dpdns.org` | Flarum 实例，支持匿名只读浏览 |

生效顺序：**手动覆盖 → 上次同步值 → 内置默认**。

- 自动同步只更新 `auto_main` / `auto_status` / `auto_forum`，不会覆盖手动值；
- 如果主服务器发生变化且本地已登录，客户端会先用现有令牌请求新服务器
  的 `/api/v2/me.json`：接受则保留登录，401/403 则吊销旧令牌并登出，
  网络不可达则暂缓切换并保留旧地址；
- 状态服务器目前没有对应的 API 客户端，只做缓存和展示。
- 论坛服务器指向 Flarum 实例（`services.FORUM`）。论坛使用独立的
  Flarum 令牌，不复用 Spark ORG 的 `spark_…` 令牌；匿名即可只读浏览，
  登录后可读取账号相关内容。论坛不可达不会影响新闻 / 联系 / 平台。

按 `s` 打开设置页，选中「服务源与实例地址」后按 `Enter` 进入，
可编辑四个地址（同步源 / 主服务器 / 状态服务器 / 论坛服务器）：

- `↑` `↓` / `j` `k`：选择设置条目
- `Enter`：进入条目
- 条目内 `Tab` / `↑` `↓`：切换字段
- 条目内 `Enter`（或 `Ctrl-S`）：保存
- 条目内 `F5`（或 `Ctrl-R`）：立即同步服务源
- `Esc`：返回上一级；设置首页 `Esc` / `q` 关闭

设置首页第二项「详情显示底层数据」控制详情 / 我的页面的默认视图：

- `关`（默认）：渲染为标题、日期、正文、联系方式等；
- `开`：直接显示完整 JSON；
- 详情页按 `x` 可临时切换，不改变设置项。

设置首页第三项「界面语言」在「跟随系统 → 站点可用语言」之间循环，并写入
`[ui] language`。`l` 只临时切换当前运行的语言，不会写入配置。

旧版的 `~/.config/spark-tui/config.json` 会在首次启动时自动迁移为
`config.toml`（`baseUrl` 变成 `instance.main`）。

令牌只保存在本机，文件权限为 `0600`。登出（`O`）会调用
`POST /api/v2/me/tokens/{id}/revoke` 并删除本地令牌。

### 命令行 / 环境变量

| 选项 | 环境变量 | 说明 |
| --- | --- | --- |
| `-u`, `--base-url` | `SPARK_TUI_BASE_URL` | 手动主服务器（写入 `instance.main`） |
| `--sync-url` | `SPARK_TUI_SYNC_URL` | 服务源地址 |
| `--status-url` | `SPARK_TUI_STATUS_URL` | 手动状态服务器 |
| `--forum-url` | `SPARK_TUI_FORUM_URL` | 手动论坛服务器（Flarum） |
| `--no-sync` | `SPARK_TUI_NO_SYNC` | 启动时不自动同步 |
| `-l`, `--lang` | `SPARK_TUI_LANG` | `zh-cn` / `en-us` |
| `--config` | `SPARK_TUI_CONFIG` | 配置文件路径 |
| `-k`, `--insecure` | `SPARK_TUI_INSECURE` | 跳过 TLS 校验 |

## 快捷键

底部状态栏只显示 `? 帮助`；完整快捷键和分场景说明在帮助页（按 `?` 打开，支持上下滚动）。

| 按键 | 作用 |
| --- | --- |
| `1`–`7` | 主页 / 新闻 / 联系 / 平台 / 搜索 / 论坛 / 我的 |
| `↑` `↓` / `j` `k` | 选择 |
| `Enter` | 查看详情 |
| `n` | 加载下一页 |
| `r` | 刷新 |
| `/` | 搜索 |
| `l` | 临时切换语言（不保存） |
| `s` | 设置（服务源 / 实例地址 / 底层数据 / 界面语言） |
| `x` | 详情 / 我的：渲染 ↔ 底层数据 |
| `i` | 联系详情：导入联系人的 PGP 公钥 |
| `L` | 登录菜单（Spark ORG 设备码 / 论坛账号） |
| `O` | 登出（论坛页优先退出论坛登录） |
| `o` | 在浏览器打开当前条目 |
| `?` | 帮助 |
| `Esc` | 返回 / 取消 |
| `q` | 退出确认（`y` 确认 / `n` 取消；`Ctrl-C` 立即退出） |

## 登录流程

按 `L` 打开登录菜单，可选择 Spark ORG 或论坛账号。

### Spark ORG 设备码

1. 选择「Spark ORG 账号」后，客户端调用 `POST /api/v2/auth/device/code`；
2. 终端显示 `user_code` 与 `verification_uri_complete`；
3. 在浏览器打开链接并批准（需要已登录的浏览器会话）；
4. 客户端每 5 秒轮询 `POST /api/v2/auth/device/token`；
5. 成功后保存返回的 `spark_…` 个人令牌，之后请求带
   `Authorization: Bearer spark_…`。

### 论坛账号

1. 选择「论坛账号」，输入用户名 / 邮箱和密码；
2. 客户端调用 Flarum 的 `POST /api/token`；
3. 成功后只保存返回的 Flarum 令牌，之后请求带
   `Authorization: Token <flarum-token>`；
4. 密码只用于本次换取令牌，不会写入配置；
5. 论坛令牌与 Spark ORG 令牌完全独立，退出论坛不影响主站登录。

## 打包 .deb

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
cd build && cpack -G DEB
```

产物：`build/spark-tui_<version>_<arch>.deb`。

默认用 `dpkg-shlibdeps` 自动计算运行时依赖（需要系统已安装对应的
`libftxui-*`）。如果 FTXUI 来自一个私有/解压出来的前缀，可以关闭它并
使用显式依赖：

```bash
cmake -S . -B build -DSPARK_TUI_DEB_SHLIBDEPS=OFF
```

安装：

```bash
sudo dpkg -i build/spark-tui_*.deb
```

使用系统 FTXUI 构建时，包会依赖 `libftxui-*`；使用 FetchContent
构建时，FTXUI 会静态编入可执行文件，运行时只依赖 OpenSSL / libstdc++。

## 目录结构

```
.
├── CMakeLists.txt
├── src/
│   ├── api/          # Spark ORG API 客户端与模型
│   ├── forum/        # Flarum JSON:API 客户端、解析与文本渲染
│   ├── ui/           # FTXUI 界面与线程池
│   ├── config.*      # TOML 配置与令牌存储
│   ├── service_source.*  # 服务源抓取 / 解析 / 地址同步
│   ├── options.*     # 命令行参数
│   ├── util.*        # UTF-8 / 换行 / URL 工具
│   └── main.cpp
├── third_party/      # httplib.h, nlohmann/json, toml++
├── docs/api-v2/      # 从站点抓取的 site/schema/openapi 配置
├── packaging/        # .desktop 与图标
└── tests/
```

`spark_core`（`api/`、`forum/`、`config.*`、`service_source.*`、`util.*`、
`ui/render_detail.*`）不依赖 FTXUI，是可以被其它前端复用的业务层。当前的
TUI 只是其中一种前端；以后开发 GTK GUI 时直接链接 `spark_core`，不要把
API / 配置 / 服务源逻辑复制到界面层。

## API 文档

`docs/api-v2/` 保存了本机 8096 抓取的：

- `site.json` — 站点信息与端点表
- `schema.json` — JSON Schema（机器可读）
- `openapi.json` — OpenAPI 3.1

在线文档：`/docs/api-guide.html`。
