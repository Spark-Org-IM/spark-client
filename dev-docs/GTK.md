# GTK 前端开发指南

本文说明如何在 `spark-tui` 的 `spark_core` 之上增加一个 GTK4 / libadwaita
前端，并让 TUI 与 GUI 能正确判断自己是从终端、桌面环境还是应用启动器启动的。
界面风格以 **GNOME / libadwaita** 为准，不另做一套自定义主题。

当前仓库的边界：

- `spark_core`：配置、服务源、Spark API、Flarum API、渲染无关的模型，**不依赖 FTXUI**；
- `spark-tui`：FTXUI 终端前端；
- 未来的 `spark-gui`：GTK4 + libadwaita 前端，只链接 `spark_core`，不链接 FTXUI。

```text
spark_core
   ├── spark-tui   (FTXUI)
   └── spark-gui   (GTK4 / libadwaita)
```

不要在 GTK 代码里直接调用 `src/ui/app.cpp` 的 `App` 类。管理新闻、论坛、
服务源确认等界面逻辑如果还需要复用，应先把状态机提取到 `spark_core` 或
独立的 controller 层。

## 2. 推荐目录结构

```text
src/
├── api/                 # Spark API
├── forum/               # Flarum API
├── config.*             # TOML 配置
├── service_source.*     # 服务源同步 / 令牌探测
├── platform/
│   ├── session.h        # 是否终端 / 是否有图形会话
│   └── session.cpp
├── gtk/
│   ├── main.cpp         # UI 模式判断 + AdwApplication
│   ├── application.*    # 应用生命周期、DBus 激活
│   ├── window.*         # AdwApplicationWindow
│   ├── pages/           # 主页 / 新闻 / 论坛 / 我的
│   ├── async.*          # 后台任务 -> GTK 主循环
│   └── theme.*          # libadwaita 样式与 CSS
└── ui/                  # 现有 FTXUI 前端，保持不动
```

二进制名建议直接分开：

- `spark-tui`：终端前端，行为不变；
- `spark-gui`：GTK 前端；
- 可选 `spark`：只做 `auto` 判断并转发到上面两个之一。

分开二进制比“一个 exe 同时塞 FTXUI 和 GTK”更容易打包、测试和排查问题。

## 3. CMake 集成

GTK 前端作为可选目标，默认关闭，避免没有 GTK 开发包的机器构建失败：

```cmake
option(SPARK_TUI_BUILD_GTK "Build the GTK4/libadwaita frontend" OFF)

if(SPARK_TUI_BUILD_GTK)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
  pkg_check_modules(ADWAITA REQUIRED IMPORTED_TARGET libadwaita-1)

  add_executable(spark-gui
    src/gtk/main.cpp
    src/gtk/application.cpp
    src/gtk/window.cpp
    src/gtk/async.cpp
    src/platform/session.cpp
  )
  target_link_libraries(spark-gui PRIVATE
    spark_core
    PkgConfig::GTK4
    PkgConfig::ADWAITA
  )
  target_compile_options(spark-gui PRIVATE ${SPARK_TUI_WARNING_FLAGS})

  install(TARGETS spark-gui RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
  install(FILES packaging/spark-gui.desktop
          DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
  install(FILES packaging/spark-gui.metainfo.xml
          DESTINATION "${CMAKE_INSTALL_DATADIR}/metainfo")
endif()
```

Debian 构建依赖：

```text
libgtk-4-dev
libadwaita-1-dev
libglib2.0-dev
pkg-config
```

如果使用 gtkmm-4.0，则改为 `libgtkmm-4.0-dev`；libadwaita 目前没有官方
C++ 绑定，通常还是在 C++ 中直接调用 libadwaita 的 C API。为了和 GNOME
组件保持一致，建议 **GTK4 C API + libadwaita C API**，C++ 只负责业务和封装。

## 4. GTK / GNOME 应用骨架

应用 ID 建议固定为：

```text
org.sparkorg.Spark
```

使用 `AdwApplication`，而不是裸 `GtkApplication`：

```cpp
#include <adwaita.h>

static void activate(GtkApplication* app, gpointer) {
  GtkWidget* window = adw_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(window), "Spark ORG");
  gtk_window_set_default_size(GTK_WINDOW(window), 1100, 760);

  GtkWidget* header = adw_header_bar_new();
  adw_application_window_set_content(
      ADW_APPLICATION_WINDOW(window), header);
  gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char** argv) {
  AdwApplication* app = adw_application_new(
      "org.sparkorg.Spark", G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "activate", G_CALLBACK(activate), nullptr);
  const int status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  return status;
}
```

建议组件：

- `AdwApplicationWindow`：主窗口；
- `AdwHeaderBar`：标题栏，不使用自绘标题栏；
- `AdwNavigationSplitView`：左侧列表 + 右侧详情（新闻、论坛、搜索都适用）；
- `AdwViewSwitcher` / `AdwViewStack`：主页、新闻、联系、平台、论坛、我的；
- `AdwPreferencesWindow`：设置；
- `AdwStatusPage`：空状态、加载失败、未登录；
- `AdwToastOverlay`：短提示；
- `AdwAboutWindow`：关于页；
- `GNotification`：后台同步、回复成功等通知。

窗口和列表要做窄窗口适配：`AdwNavigationSplitView` 在窄宽度会自动折叠，
不要写死三栏布局。

## 5. GUI / TUI 启动识别

不要只靠 `DISPLAY` 判断。终端里也可能有 `DISPLAY`，桌面启动器启动时
stdout 也可能不是 TTY。推荐按以下优先级判断：

1. 命令行显式指定：`--ui=tui` / `--ui=gtk`；
2. 环境变量显式指定：`SPARK_UI=tui|gtk`；
3. 从 `.desktop` / GIO 启动：`GIO_LAUNCHED_DESKTOP_FILE` 存在 → GUI；
4. SSH 会话：`SSH_CONNECTION` / `SSH_TTY` 存在 → TUI；
5. stdin/stdout 是 TTY → TUI；
6. 有图形会话（`WAYLAND_DISPLAY` / `DISPLAY`）→ GUI；
7. 都没有 → 报错，提示用户在终端运行 `spark-tui`。

判断 TTY 必须是真正的 `isatty()`，不能只看 `TERM` 是否存在。

### 5.1 环境变量参考

| 变量 | 含义 | 建议用途 |
| --- | --- | --- |
| `XDG_SESSION_TYPE` | `wayland` / `x11` / `tty` | 图形会话判断 |
| `WAYLAND_DISPLAY` | Wayland socket | 图形会话判断 |
| `DISPLAY` | X11 display | 图形会话判断 |
| `XDG_CURRENT_DESKTOP` | `GNOME` / `KDE` 等 | 主题适配，不单独决定前端 |
| `GIO_LAUNCHED_DESKTOP_FILE` | 从 .desktop 启动 | GUI 的强信号 |
| `XDG_ACTIVATION_TOKEN` | Wayland 激活令牌 | GUI 启动提示 |
| `SSH_CONNECTION` / `SSH_TTY` | SSH 会话 | 优先 TUI |
| `TERM` | 终端类型 | 辅助信息，不能代替 isatty |
| `DBUS_SESSION_BUS_ADDRESS` | 会话总线 | GApplication / 通知 |
| `SPARK_UI` | 本项目自定义 | 强制 `tui` / `gtk` |

### 5.2 纯逻辑判断

建议放在 `src/platform/session.{h,cpp}`，不依赖 GTK，方便 TUI 和 GUI 共用、
也方便单元测试：

```cpp
enum class UiMode { Auto, Tui, Gtk };

bool IsTerminalSession() {
  return isatty(STDIN_FILENO) || isatty(STDOUT_FILENO);
}

bool HasGraphicalSession() {
  const char* session = g_getenv("XDG_SESSION_TYPE");
  if (session != nullptr &&
      (g_strcmp0(session, "wayland") == 0 ||
       g_strcmp0(session, "x11") == 0)) {
    return true;
  }
  return g_getenv("WAYLAND_DISPLAY") != nullptr ||
         g_getenv("DISPLAY") != nullptr;
}

UiMode DetectUiMode(UiMode requested) {
  if (requested != UiMode::Auto) return requested;

  if (const char* ui = g_getenv("SPARK_UI")) {
    if (g_strcmp0(ui, "gtk") == 0) return UiMode::Gtk;
    if (g_strcmp0(ui, "tui") == 0) return UiMode::Tui;
  }

  if (g_getenv("GIO_LAUNCHED_DESKTOP_FILE") != nullptr) {
    return UiMode::Gtk;
  }
  if (g_getenv("SSH_CONNECTION") != nullptr ||
      g_getenv("SSH_TTY") != nullptr) {
    return UiMode::Tui;
  }
  if (IsTerminalSession()) return UiMode::Tui;
  if (HasGraphicalSession()) return UiMode::Gtk;
  return UiMode::Auto;  // 调用方负责报错
}
```

`spark-gui` 本身可以默认 `Gtk`，`spark-tui` 默认 `Tui`；只有可选的 `spark`
启动器需要跑上面的 `Auto` 逻辑。

### 5.3 从 TUI 启动 GUI

不要在 TUI 里 `system("spark-gui")`。推荐用 GIO 或 DBus 激活：

- 首选：给 `spark-gui.desktop` 加 `DBusActivatable=true`，通过
  `org.freedesktop.Application.Activate` 让已运行的 GUI 抬起窗口；
- 没有运行实例时，用 `g_app_info_launch()` 启动 `spark-gui.desktop`；
- 必须脱离终端时，用 `g_subprocess_new()` 并设置
  `G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE`，
  不要把 GUI 的 stdout 留在 TUI 的终端里。

TUI 里可以增加一个快捷键（例如 `g`）或 `--gui`，但不要默认自动拉起 GUI。

### 5.4 从 GUI 启动 TUI

GUI 里“在终端中打开”不要直接 `system("spark-tui")`，因为 GUI 进程没有
控制终端。使用桌面环境的终端应用（例如 `org.gnome.Terminal`）或让用户
手动在终端运行。最稳妥的做法是显示命令：

```text
spark-tui
```

并提供“复制命令”按钮，而不是假设某个终端模拟器存在。

## 6. .desktop 与打包

GUI 桌面项：

```ini
[Desktop Entry]
Name=Spark ORG
Comment=Spark ORG news, contacts, platforms and forum
Exec=spark-gui %U
Icon=org.sparkorg.Spark
Terminal=false
Type=Application
Categories=Network;InstantMessaging;
StartupNotify=true
DBusActivatable=true
```

TUI 桌面项：

```ini
[Desktop Entry]
Name=Spark ORG (Terminal)
Comment=Spark ORG terminal client
Exec=spark-tui
Icon=org.sparkorg.Spark
Terminal=true
Type=Application
Categories=Network;
```

GUI 还需要 AppStream 元数据 `packaging/spark-gui.metainfo.xml`，包含
`id`、`name`、`summary`、`description`、`launchable`、`provides` 和截图。
`launchable` 必须和 desktop 文件名/应用 ID 对应。

Debian 打包建议拆成两个二进制包：

- `spark-tui`：TUI，不依赖 GTK；
- `spark-gui`：GTK 前端，依赖 GTK4 / libadwaita；
- 两者共享同一个 `spark_core` 源码和配置目录，但可以独立安装。

如果暂时只做一个包，至少把 GUI 依赖放到 `Recommends`，不要让只想用 TUI 的
用户被迫安装整套 GNOME 依赖。

## 7. 后台任务与线程模型

`spark_core` 的 HTTP 请求是阻塞的，GTK 主循环绝不能直接调用同步网络请求。
推荐：

```text
GTK 主线程
   │ 提交任务
   ▼
std::jthread / GTask
   │ 阻塞调用 spark_core
   ▼
结果通过 g_idle_add / Glib::Dispatcher 回到主线程
   │
   ▼
更新 GTK 控件
```

原则：

- 所有 GTK / libadwaita API 只在主线程调用；
- 后台线程只操作 `spark_core` 的纯数据对象；
- 每个请求带 `GCancellable` 或 generation 计数，切换页面后丢弃旧结果；
- 登录、刷新、论坛写操作要有明确的 loading / disabled 状态；
- 错误显示在 `AdwToastOverlay` 或 `AdwStatusPage`，不要把异常直接抛到主循环。

## 8. 共享配置、登录和服务源

GUI 与 TUI 应使用同一份配置：

```text
~/.config/spark-tui/config.toml
```

这样在 TUI 登录后，GUI 可以直接复用令牌；反之亦然。注意：

- 文件权限保持 `0600`；
- 两个前端不要同时写配置，后续可以加 `flock` 或 GFile 锁；
- 论坛令牌和 Spark 令牌仍分开保存；
- 不要把密码写入配置，论坛登录只保存换来的 Flarum token。

### 服务源变更确认

目前“列出旧值 → 新值、探测 Spark / 论坛令牌、拒绝时要求退出登录”的逻辑在
`src/ui/app.cpp` 的 `SourceReviewState` / `ProbeSourceReview` /
`ApplySourceReview` 里，和 FTXUI 绑定。GTK 前端不应复制一份。

建议先提取到核心层，例如：

```text
src/source_review.h/.cpp
```

职责：

- 生成 `SourceChangePlan`（pending Config + 变更列表）；
- 在后台线程调用 `ProbeToken()` / `ForumClient::RawRequest("GET", "/api")`；
- 返回 accepted / rejected / unreachable；
- 提供 `Apply()` 和 `LogoutInvalidSessions()`；
- 不依赖任何 FTXUI 或 GTK 类型。

TUI 改为调用这个核心服务，GTK 直接复用同一套判断，避免两个前端行为不一致。

## 9. GNOME 主题与 libadwaita

目标不是“画一个像 GNOME 的界面”，而是**使用 GNOME 提供的组件和颜色**，
让系统主题、深色模式、强调色、高对比度自动生效。

### 9.1 跟随系统主题

```cpp
AdwStyleManager* style = adw_style_manager_get_default();
adw_style_manager_set_color_scheme(style, ADW_COLOR_SCHEME_DEFAULT);
```

`ADW_COLOR_SCHEME_DEFAULT` 表示跟随系统，不要硬编码深色或浅色。只有在用户
明确选择“强制深色”时才使用 `ADW_COLOR_SCHEME_FORCE_DARK`。

### 9.2 使用命名颜色

CSS 中优先使用 libadwaita 的命名颜色，不要写 `#ffffff` / `#000000`：

```css
window {
  background-color: @window_bg_color;
  color: @window_fg_color;
}

.card {
  background-color: @card_bg_color;
  border-radius: 12px;
  padding: 12px;
}

.accent-button {
  background-color: @accent_bg_color;
  color: @accent_fg_color;
}

.error-label {
  color: @error_color;
}
```

常用颜色：

```text
@window_bg_color       @window_fg_color
@view_bg_color         @view_fg_color
@card_bg_color         @card_fg_color
@popover_bg_color      @popover_fg_color
@accent_color          @accent_bg_color       @accent_fg_color
@success_color         @warning_color         @error_color
```

这样 GNOME 的强调色（包括 GNOME 47+ 的 accent-color）和深色模式会自动
映射，不需要自己读 GSettings。

### 9.3 图标与字体

- 优先使用 symbolic icon，例如 `dialog-information-symbolic`、
  `emblem-synchronizing-symbolic`、`open-menu-symbolic`；
- 品牌图标单独提供，不要拿彩色图标替代所有操作图标；
- 正文使用系统字体，不要强行设置字体家族；
- 代码、API 响应、Markdown 预览使用 `monospace` 样式类；
- 标题使用 libadwaita 的 `title-1` / `title-2` / `heading` 样式类。

### 9.4 高对比度与可访问性

- 使用系统控件和命名颜色，不要在 CSS 里覆盖焦点环、选择色；
- 所有图标按钮提供 `tooltip-text` 和 accessible label；
- 状态不能只靠颜色表达，配合文字或 symbolic icon；
- 测试 `GTK_THEME=HighContrast`、深色、浅色三种情况；
- 键盘操作要完整：`Ctrl+F` 搜索、`Ctrl+,` 设置、`Esc` 返回、`Ctrl+Q` 退出。

### 9.5 不要做的事

- 不要强制 `gtk-application-prefer-dark-theme`；
- 不要自己画标题栏、阴影和圆角来模拟 GNOME；
- 不要把 TUI 的配色表直接搬到 GTK；
- 不要给整个应用写一套固定颜色的 CSS；
- 不要依赖 `XDG_CURRENT_DESKTOP=GNOME` 才启用正常主题，KDE 下也应可读。

## 10. 测试清单

构建：

```bash
cmake -S . -B build -DSPARK_TUI_BUILD_GTK=ON
cmake --build build
```

无头环境：

```bash
xvfb-run -a ./build/spark-gui
```

手工检查：

- 从终端运行 `spark-gui`：应打开 GTK 窗口；
- 从 `.desktop` 启动：应打开 GTK 窗口，且已有窗口时抬起而不是开第二个；
- 在终端运行 `spark-tui`：仍然是 TUI，不受 `DISPLAY` 影响；
- SSH 会话运行 `spark`：选择 TUI；
- GUI 启动后关闭终端：窗口不应被 SIGHUP 带走；
- 深色 / 浅色 / 高对比度 / 不同强调色；
- 窄窗口下 `AdwNavigationSplitView` 折叠正常；
- 网络失败、401、论坛 403 都有可读错误状态；
- 登录态在 TUI 与 GUI 之间共享；
- 服务源变更被拒绝时，GUI 也必须阻止应用并引导退出登录。

## 11. 分阶段实施建议

1. **抽核心**：把 `SourceReview` 等 UI 状态机从 `src/ui/app.cpp` 提取到
   `spark_core`，TUI 行为不变；
2. **最小 GUI**：`spark-gui` 只做只读主页 / 新闻 / 论坛列表，验证
   GTK4 + libadwaita + 主题；
3. **登录与详情**：复用 `ApiClient` / `ForumClient`，完成登录、详情、
   搜索、分页；
4. **管理功能**：新闻管理、论坛管理、回复、发主题、API 工具；
5. **打包**：拆 `spark-tui` / `spark-gui` 二进制包，补 desktop、
   metainfo、图标和 CI；
6. **UI 模式检测**：最后加 `spark` 启动器或 `--ui=auto`，不要在最小 GUI
   阶段就把启动逻辑做复杂。

这样 GTK 前端不会反向依赖 FTXUI，TUI 也不会因为 GUI 的加入而变重。
