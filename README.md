# spark-client

Spark ORG 客户端，包含终端前端 `spark-tui`（基于
[FTXUI](https://github.com/ArthurSonzogni/FTXUI)）和 GTK4 / libadwaita 前端
`spark-gui`，共享同一套 `spark_core`，使用 C++20。

## 功能

- 启动主页：站点名称与最近新闻预览；服务地址等技术信息收进设置
- 浏览新闻 / 联系 / 平台（分页、详情、搜索）
- 论坛只读浏览：主题列表、主题详情 / 回复、标签、分页
- 管理工具：
  - 主页置顶：候选新闻、置顶 / 取消、顺序调整（需要 `news.edit`）
  - 新闻：新建 / 编辑底层 JSON / 删除 / 历史回滚（需要 `news.edit`）
  - 论坛主题：置顶、锁定、隐藏、重命名、删除、标签编辑
  - 论坛内容：回复主题、发新主题
  - 需要管理权限；富文本 / JSON 编辑通过 `$VISUAL` / `$EDITOR` 完成
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
./build/spark-tui       # 只跑 TUI
./build/spark-client    # 按会话选择 TUI / GUI（Linux）
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

默认配置文件：`~/.config/spark-tui/config.toml`（可用 `--config` 或
`SPARK_TUI_CONFIG` 覆盖）。

Windows 下使用 `%APPDATA%\spark-tui\config.toml`；外部编辑器默认
`notepad`，可用 `EDITOR` / `VISUAL` 覆盖。导入 PGP 公钥需要 `gpg`
在 `PATH` 中。

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
- 自动同步时如果主服务器或论坛服务器发生变化，客户端会先用现有令牌
  探测新地址：接受则切换；被拒绝或不可达则保留旧地址，不会静默清除
  登录。需要切换时请到设置页手动确认，并先退出失效的登录；
- 状态服务器目前没有对应的 API 客户端，只做缓存和展示。
- 论坛服务器指向 Flarum 实例（`services.FORUM`）。论坛使用独立的
  Flarum 令牌，不复用 Spark ORG 的 `spark_…` 令牌；匿名即可只读浏览，
  登录后可读取账号相关内容。论坛不可达不会影响新闻 / 联系 / 平台。

按 `s` 打开设置页，选中「服务源与实例地址」后按 `Enter` 进入，
可编辑四个地址（同步源 / 主服务器 / 状态服务器 / 论坛服务器）：

- `↑` `↓` / `j` `k`：选择设置条目
- `Enter`：进入条目
- 条目内 `Tab` / `↑` `↓`：切换字段
- 条目内 `Enter`（或 `Ctrl-S`）：核对变更
- 条目内 `F5`（或 `Ctrl-R`）：立即同步服务源
- `Esc`：返回上一级；设置首页 `Esc` / `q` 关闭

按 `Enter` 后不会立即保存，而是进入「确认服务源变更」页面：

1. 逐条列出旧值 → 新值（主服务器 / 状态服务器 / 论坛服务器 / 同步源）；
2. 如果主服务器或论坛服务器发生变化，且本地已有对应令牌，自动用该令牌
   请求新地址：Spark ORG 使用 `GET /api/v2/me.json`，论坛使用
   `GET /api`；
3. 令牌可用（或本地本来没有登录）时按 `Enter` 保存并应用；
4. 令牌被拒绝时应用会被阻止，按 `O` 退出失效的登录，再按 `Enter`
   保存；保存后会直接进入登录管理，提示重新登录；
5. 新地址不可达时同样阻止应用，可修复网络后按 `r` 重新检查，或按
   `Esc` 取消且不写入任何配置。

取消（`Esc` / `q`）不会修改配置文件。

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
| `--ui` | `SPARK_UI` | 前端：`auto` / `tui` / `gtk`，由 `spark-client` 处理 |

### 启动入口

| 命令 | 所属包 | 行为 |
| --- | --- | --- |
| `spark-client` | `sparkapp-core` | 检测会话，`exec` 到 `spark-gui` 或 `spark-tui` |
| `spark-tui` | `sparkapp-tui` | 只运行终端界面，不做任何 GUI 判断 |
| `spark-gui` | `sparkapp-gui` | 只运行 GTK 界面，不做任何终端判断 |

`spark-client` 的判定顺序与 `--ui` / `SPARK_UI` 一致：

1. `--ui tui|gtk` 或 `SPARK_UI` 显式指定；
2. 从 `.desktop` / GIO 启动 → GUI；
3. SSH 会话 → TUI；
4. stdin/stdout 是 TTY → TUI；
5. 有图形会话 → GUI；
6. 都没有 → 报错并提示安装哪个包。

前端可执行文件优先在 `spark-client` 同目录查找，其次 `PATH`。

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
| `m` | 主页：管理置顶；新闻 / 论坛：管理选中条目 |
| `a` | 新闻：新建；论坛主题：回复 |
| `c` | 论坛：发新主题 |
| `A` | 论坛 / 主题详情：论坛 API 工具 |
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

## 管理工具

管理入口是上下文相关的，不额外占用标签页：

- 主页按 `m`：打开主页置顶管理。`Enter` / `Space` 置顶或取消，
  `J` / `K` 调整置顶顺序，`r` 刷新候选列表，`Esc` 返回主页。
  该功能使用 Spark ORG 令牌，需要 `news.edit` 权限。
- 新闻列表按 `a`：新建新闻；按 `m`：编辑底层 JSON、查看历史并回滚、删除。
  编辑内容会写入临时文件并调用 `$VISUAL` / `$EDITOR`（默认 `vi`）。
- 论坛列表或主题详情按 `m`：打开论坛主题管理。根据论坛返回的
  `canSticky` / `canLock` / `canHide` / `canTag` / `canRename` /
  `canDelete` 显示可用操作；隐藏和删除需要 `y` 二次确认。
- 论坛主题详情按 `a`：回复主题；论坛列表按 `c`：发新主题。
  回复正文、发主题和标签编辑同样通过外部编辑器完成。
- 新闻删除、历史回滚、论坛隐藏 / 删除都有二次确认。
- 论坛列表或主题详情按 `A`：打开论坛 API 工具，直接调用 Flarum 扩展的
  JSON:API。`Tab` 切换 GET / POST / PATCH / DELETE，`Enter`（或
  `Ctrl-R`）发送，`Ctrl-E` 用外部编辑器修改 JSON 请求体，`Ctrl-P`
  载入常用接口预设，`Ctrl-U` 清空路径；响应可用 `↑` `↓` / `PgUp`
  `PgDn` 滚动。请求会带上论坛令牌（如果已登录），可用于排查扩展接口
  或执行尚未内置的批量操作。

权限不足时服务端返回 403，客户端会在界面/状态栏显示错误，不会假装成功。
Flarum 2.x 的置顶 / 锁定 / 隐藏 / 重命名 / 标签通过
`PATCH /api/discussions/{id}` 修改，删除使用
`DELETE /api/discussions/{id}`，回复使用 `POST /api/posts`，
发主题使用 `POST /api/discussions`。

## CI 与发布（不需要自己的服务器）

项目是桌面 / 终端客户端，发布物是 Linux `.deb` / 独立可执行文件和 Windows `.exe`，不需要“目标服务器”做持续部署。
推荐把 **GitHub Releases 当作分发 CDN**，CI 只负责构建、测试和上传产物：

1. 在 GitHub 建一个公开仓库（例如 `Spark-Org-IM/spark-client`）；
2. 当前本地 remote 约定：
   - `origin` = GitHub SSH
   - `gitea` = 自建 Git

   ```bash
   git push origin <branch>
   ```
3. `.github/workflows/ci.yml` 在 push / PR 时只做**构建和单元测试**，方便合并前
   快速检查；打包和上传产物只在 `v*` tag 上执行：
   - Linux amd64 / arm64：`sparkapp-core`、`sparkapp-tui`、`sparkapp-gui`
     三个 `.deb`；
   - Windows amd64 / arm64：`.zip`（静态链接 OpenSSL，单 exe）；
   - Debian 源码包（native）：`.dsc` + `.tar.xz` + `.changes` + `.buildinfo`；
   - GTK 前端 `spark-gui` 在独立的 `gtk-framework` 任务里做分支 / PR 的快速
     构建检查（仅当 `src/gtk/` 存在）；tag 构建时由 Linux 任务一并编译并生成
     `sparkapp-gui` 的 `.deb`。

   Release 页面按下面方式发布：

   - `spark-client_<version>_linux-amd64.tar.gz` /
     `spark-client_<version>_linux-arm64.tar.gz`：只包含该架构的 core / tui /
     gui 三个 `.deb`，以及内部 `SHA256SUMS`；
   - `spark-client_<version>_all.tar.gz`：两个架构的 `.deb`、源码包
     `.dsc` / `.tar.xz` / `.changes` / `.buildinfo`，以及 README、LICENSE、
     `SHA256SUMS`，所有文件平铺在 tar 根目录，便于直接导入 deb 源；
   - `spark-client_<version>_windows-amd64.zip` /
     `spark-client_<version>_windows-arm64.zip`。
4. 发布版本只需要推 tag：
   ```bash
   git tag v0.4.1
   git push origin v0.4.1
   ```
   同一个 workflow 会创建 GitHub Release，并附上各架构的 Debian 包 tar、
   源码 all tar 和 Windows zip。
5. 用户从 Release 页面下载后校验并安装：
   ```bash
   sha256sum -c SHA256SUMS

   # Debian / Ubuntu：解压对应架构的 tar，按需安装
   #   - core：共享库 + spark-client，不依赖 GTK
   #   - tui ：终端前端，不依赖 GTK
   #   - gui ：GTK 前端，依赖 core + libgtk-4-1 + libadwaita-1-0
   tar -xzf spark-client_0.4.1_linux-amd64.tar.gz
   cd spark-client-0.4.1-linux-amd64
   sha256sum -c SHA256SUMS
   sudo apt install ./sparkapp-core_0.4.1_amd64.deb \
                    ./sparkapp-tui_0.4.1_amd64.deb
   sudo apt install ./sparkapp-core_0.4.1_amd64.deb \
                    ./sparkapp-gui_0.4.1_amd64.deb

   # 统一入口：按会话自动启动 spark-gui 或 spark-tui
   spark-client
   ```
   Windows 解压 `spark-client_0.4.1_windows-amd64.zip` 或
   `spark-client_0.4.1_windows-arm64.zip` 后运行 `spark-tui.exe`。

   `spark-client_0.4.1_all.tar.gz` 解压后文件平铺在当前目录，包含两个
   架构的 `.deb`、源码包和校验和，可以直接作为手动导入 deb 源的目录。

   二进制包拆分为：

   - `sparkapp-core`：共享 `libsparkapp-core.so`、配置、API 逻辑、统一入口
     `spark-client`，以及两个前端共用的桌面图标；不依赖 GTK / FTXUI；
   - `sparkapp-tui`：`spark-tui` 可执行文件，依赖 `sparkapp-core`，不依赖 GTK；
   - `sparkapp-gui`：`spark-gui` 可执行文件和桌面项，依赖 `sparkapp-core`、
     `libgtk-4-1`、`libadwaita-1-0`，不依赖 FTXUI。

   三个包共用同一个配置目录，可以只装 TUI、只装 GUI，或两个前端都装。
   默认 `.desktop` 也可以写 `spark-client`，由统一入口按会话选择前端。

   `spark-client` 只负责选择前端，不做界面：图形会话启动 `spark-gui`，
   终端 / SSH 启动 `spark-tui`；如果只装了 `sparkapp-core`，它会直接提示
   缺少哪个前端（图形会话下用 zenity/kdialog 弹窗），不会静默失败。

   项目使用 Debian native 源码包（`debian/source/format` 为
   `3.0 (native)`），所以只有一个 `.tar.xz`，`debian/` 也在里面：
   ```bash
   dpkg-source -x spark-client_0.4.1.dsc
   cd spark-client-0.4.1
   dpkg-buildpackage -us -uc
   ```

   CI 生成源码包时使用 `dpkg-buildpackage -S -us -uc -d -nc`。

版本号遵循 SemVer（`MAJOR.MINOR.PATCH`，例如 `0.4.1`），不带 Debian
修订后缀；重要测试节点直接打对应 tag（如 `v0.4.1`），日常测试不推 tag，
直接使用 Actions artifact 即可。

GitHub Actions 对公开仓库的标准 Linux runner **不计分钟数**；私有仓库
免费额度是每月 2,000 分钟。当前项目一次完整构建通常几分钟，公开仓库
完全够用。

> 首次推送后如果 Release 步骤报 403，到仓库
> `Settings → Actions → General → Workflow permissions`，选择
> “Read and write permissions” 即可；workflow 本身已经声明了
> `contents: write`。

GitHub Actions 是唯一的 CI。极狐 GitLab 如果只作为国内镜像，就不要放
`.gitlab-ci.yml`，否则它的共享 runner 会重复跑同一套构建；之前已经因为
`job_execution_timeout` 失败过一次，所以仓库里不再保留 GitLab CI 配置。

自建 Gitea 只做代码托管。仓库里的 `.gitea/workflows/README.md` 会让
Gitea 忽略 `.github/workflows`，避免在没有 runner 时出现
`Waiting to run` 队列；如果仍然排队，到仓库设置关闭 Actions 单元并取消
旧任务。

> 如果以后需要 `apt upgrade` 自动更新，可以在 GitHub Pages 上放一个
> 用 `reprepro` / `aptly` 生成的静态 APT 仓库；当前阶段用 Releases +
> 手动安装 `.deb` 更简单，也不需要维护服务器。

## 版本号

根目录的 `VERSION` 是唯一版本源，格式为 SemVer：

```text
MAJOR.MINOR.PATCH
0.4.1
```

CMake 从这里生成：

- `SPARK_TUI_PROJECT_VERSION`：`0.4.1`
- `SPARK_TUI_PACKAGE_VERSION`：`0.4.1`
- 程序里的 `SPARK_TUI_VERSION`、`spark-tui --version`
- `.deb` 文件名、Linux tar / Windows zip 文件名
- Debian 源码包版本

修改版本时只改 `VERSION`，然后同步派生文件：

```bash
packaging/sync-version.sh
```

它会更新 `debian/changelog` 和 `vcpkg.json`。CI 会运行
`packaging/sync-version.sh --check`，并校验 tag 必须等于 `v${VERSION}`，
例如 `VERSION=0.4.1` 只能打 `v0.4.1`。

## 打包 .deb

只装终端前端（不需要 GTK 开发包）：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
cd build && cpack -G DEB
```

产物：

- `sparkapp-core_<version>_<arch>.deb`
- `sparkapp-tui_<version>_<arch>.deb`

同时生成 GTK 前端包（需要 `libgtk-4-dev`、`libadwaita-1-dev`、`pkg-config`）：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSPARK_TUI_BUILD_GTK=ON
cmake --build build
cd build && cpack -G DEB
```

产物额外包含 `sparkapp-gui_<version>_<arch>.deb`，依赖 `sparkapp-core`、
`libgtk-4-1` 和 `libadwaita-1-0`。

安装（三个包互相独立）：

```bash
# 终端
sudo apt install ./sparkapp-core_*.deb ./sparkapp-tui_*.deb
# 图形
sudo apt install ./sparkapp-core_*.deb ./sparkapp-gui_*.deb
# 可选插件由独立项目构建，不随本项目的发布包提供
```

插件**不在本项目里构建**。官方插件（新闻管理、论坛管理）在独立项目
`spark-client-plugins` 中，第三方插件也各自单独构建；主项目只安装 SDK 头文件
并提供 `spark-plugin-abi-<N>` 虚拟包。构建方法见
[`dev-docs/PLUGINS.md`](dev-docs/PLUGINS.md#6-构建与打包)。

依赖计算：

- `sparkapp-core` 用 `dpkg-shlibdeps` 自动计算；
- `sparkapp-tui` / `sparkapp-gui` 使用显式依赖（core + 各自的 UI 库），
  这样 TUI 不会带进 GTK、GUI 不会带进 FTXUI；
- 需要完全手写依赖时可以加 `-DSPARK_TUI_DEB_SHLIBDEPS=OFF`。

## 目录结构

```
.
├── CMakeLists.txt
├── src/
│   ├── api/          # Spark ORG API 客户端与模型
│   ├── forum/        # Flarum JSON:API 客户端、解析与文本渲染
│   ├── ui/           # FTXUI 界面与线程池
│   ├── config.*      # TOML 配置与令牌存储
│   ├── topbar.*      # 顶栏条目的合并、顺序与持久化
│   ├── plugin/       # 可选插件的 C ABI、SDK 与加载器
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
`worker.*`、`ui/render_detail.*`）不依赖 FTXUI，是可以被其它前端复用的
业务层。TUI 只是其中一种前端；GTK 前端 `spark-gui` 同样直接链接
`spark_core`，不要把 API / 配置 / 服务源逻辑复制到界面层。

GTK / GNOME 前端设计、GUI/TUI 启动检测、libadwaita 主题和打包方式见
[`dev-docs/GTK.md`](dev-docs/GTK.md)。

可选插件（新闻管理、论坛管理等）以 `sparkapp-plugin-*` 独立包安装，运行时
由 `spark_core` 的 C ABI 动态加载；前端包不依赖任何插件。TUI 中按 `P` 打开
插件动作列表；GTK 在至少一个动作可用时显示“插件”页。插件动作可以固定到
顶栏（设置 → 顶栏元素）。插件接口、动作模型和安全边界见
[`dev-docs/PLUGINS.md`](dev-docs/PLUGINS.md)，顶栏配置与两端交互见
[`dev-docs/TOPBAR.md`](dev-docs/TOPBAR.md)。

## API 文档

`docs/api-v2/` 保存了本机 8096 抓取的：

- `site.json` — 站点信息与端点表
- `schema.json` — JSON Schema（机器可读）
- `openapi.json` — OpenAPI 3.1

在线文档：`/docs/api-guide.html`。
