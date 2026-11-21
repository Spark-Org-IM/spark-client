# Changelog

## 0.4.1

### 新增

- 新增 GTK4 / libadwaita 前端 `spark-gui`，并作为独立 `sparkapp-gui` 包发布：
  - 窗口级 `AdwNavigationView` + 侧边栏 + 内容栈，详情页作为新页面推入；
  - 主页置顶新闻、新闻 / 联系 / 平台列表（分页）、论坛讨论列表、账号页；
  - 详情页异步加载：新闻、联系人、平台、论坛主题、账号信息，支持导入 PGP 公钥；
  - 跟随 GNOME 深色模式、强调色和高对比度，不覆盖系统主题；
  - 网络请求在 `spark::Worker` 线程池执行，结果经 `g_idle_add` 回到 GTK 主循环；
  - 线程池 `Worker` 从 TUI 移入 `spark_core`，TUI 与 GTK 共用；
  - GTK 登录界面尚未接入，登录仍通过 `spark-tui`，两者共享同一份配置。
- TUI 和 GTK 增加可配置顶栏：内置页面和插件动作都可以设置显示、折叠和顺序；
  TUI 使用单行滑动条，GTK 使用侧栏左滑进入折叠面板并提供返回按钮。

### 构建

- 版本号改为 SemVer（当前 `0.3.0`），去掉 Debian `-1` 修订后缀；`VERSION` 仍是
  唯一来源，`debian/changelog` 和 `vcpkg.json` 由 `packaging/sync-version.sh`
  同步；
- GitHub Actions 多平台矩阵：Linux amd64 / arm64 生成 `.deb` 和独立可执行文件，
  Windows amd64 / arm64 生成单 exe 的 `.zip`；产物名统一为 `spark-client_*`；
- Debian 源码包改为 native 格式：`.dsc` + `.tar.xz` + `.changes` + `.buildinfo`
  （`debian/` 一并打进源码 tar）；
- 二进制包拆分为 `sparkapp-core` / `sparkapp-tui` / `sparkapp-gui`，三个包可独立安装；
  TUI 不依赖 GTK，GUI 依赖 `sparkapp-core` + GTK4 / libadwaita，不依赖 FTXUI；
- `sparkapp-core` 新增统一入口 `spark-client`，按会话启动 TUI 或 GTK 前端；
- Linux 独立运行包内含 `libsparkapp-core.so`，rpath 为 `$ORIGIN`，解压即可运行；
- push / PR 只构建并运行测试，方便合并前检查；`v*` tag 才打包、上传
  artifact 并汇总发布 Release；
- Release 按架构打 Debian 包 tar：`spark-client_<version>_linux-amd64.tar.gz`
  和 `..._linux-arm64.tar.gz` 只含 core / tui / gui 的 `.deb`；
- 源码包和元数据单独放在 `spark-client_<version>_all.tar.gz`；Windows 每个
  架构一个 zip；
- GTK 前端在独立的 `gtk-framework` 任务中做分支 / PR 构建检查；tag 时由
  Linux 任务生成 `sparkapp-gui` 的 `.deb`，但“解压即用”的 Linux tar 仍只带
  core + TUI；
- 插件（包括官方插件）不再在主项目中构建、安装或打包；主项目只安装插件 SDK，
  `sparkapp-core` 提供 `spark-plugin-abi-<N>` 虚拟包，官方插件移到独立项目
  `spark-client-plugins`。

## 0.2.0

### 新增

- 论坛管理：置顶、锁定、隐藏、重命名、标签编辑、删除；
- 论坛写作：回复主题、发新主题；
- 论坛 API 工具：原始 JSON:API 请求、常用接口预设、外部编辑器修改请求体；
- 新闻管理：新建、编辑底层 JSON、查看历史、回滚、删除；
- 主页置顶管理：选择、移除、调整顺序；
- 服务源变更确认：列出旧值 → 新值，探测 Spark / 论坛令牌；
- 令牌被拒绝时阻止应用，退出失效登录后引导重新登录；
- 自动同步遇到新源拒绝令牌时保留旧地址，不再静默登出；
- GitHub Actions：构建、测试、生成 `.deb`。

### 已知限制

- 论坛管理和写作接口只在本地假 Flarum 服务上验证，尚未用真实管理员账号联调；
- 服务源切换的令牌探测使用假服务器验证，真实环境仍需一次人工确认；
- 当前只提供 Linux amd64 `.deb`；
- 论坛扩展 API 没有 OpenAPI 描述，API 工具使用原始 JSON:API，需要使用者了解 Flarum schema。

### 升级说明

- 配置文件格式保持兼容；
- 手动服务源修改现在会先进入确认页，按 `Enter` 才写入；
- 论坛令牌与 Spark ORG 令牌仍分开保存，配置权限保持 `0600`。
