# 插件开发指南

插件用来承载按角色、按需安装的扩展功能，例如新闻管理、联系人/平台编辑、
论坛管理。插件是**可选包**：不装不影响 TUI / GUI 的启动和使用。

## 1. 边界：C ABI + C++ SDK

跨动态库边界只使用 C 结构和普通 UTF-8 字符串（`src/plugin/api.h`），
不传 `std::string`、`std::vector` 或 nlohmann 类型，也不跨模块
`new/delete`。插件内部仍然用 C++ 写，由 `src/plugin/sdk.h` 的适配宏
生成 C 导出：

```cpp
#include "plugin/sdk.h"

class MyPlugin : public spark::plugin::Plugin {
 public:
  explicit MyPlugin(const spark_host_api* host)
      : spark::plugin::Plugin(host) {}

  std::string Id() const override { return "my-plugin"; }
  std::string ActionsJson() const override;
  std::string RunJson(const std::string& action,
                      const std::string& input) override;
};

SPARK_PLUGIN_EXPORT(MyPlugin)
```

宿主只寻找一个符号：

```c
const spark_plugin_api* spark_plugin_entry(uint32_t host_abi_version);
```

`spark_plugin_api` 带 `struct_size` 和 `abi_version`；不匹配的插件会被
跳过并记录日志，不会影响主程序。

## 2. 宿主能力（以及安全边界）

插件拿到的 `spark_host_api` 只有：

- `api_request` / `forum_request`：由宿主带着用户令牌发起请求，插件只拿
  JSON 结果；
- `settings_json`：实例地址、界面语言、插件私有数据目录；
- `log`；
- `free_string`。

宿主**不会**把 `Config`、`ApiClient`、令牌、加密数据或任何凭据交给插件。
插件也不应该尝试从配置目录读取令牌；需要数据时通过 `api_request` 让宿主
代为请求。

> 说明：插件是原生动态库，和主程序同进程运行。上述是 API 层的强制边界；
> 如果以后要防住恶意插件直接读文件，需要把插件放到子进程并配合
> Landlock/seccomp，这是后续的加固项。

## 3. 动作模型

插件用 JSON 描述动作，前端据此生成入口和输入表单：

```json
[
  {
    "id": "save-news",
    "title": "保存新闻",
    "description": "按 slug 创建或更新新闻",
    "scopes": ["news.edit"],
    "fields": [
      {"key": "slug", "label": "Slug", "type": "text", "required": true},
      {"key": "json", "label": "新闻 JSON", "type": "multiline",
       "required": true, "initial": "{}"}
    ]
  }
]
```

`run_json` 输入是字段 JSON，输出：

```json
{"ok": true, "message": "完成", "refresh": "news"}
```

失败时返回 `{"ok": false, "message": "..."}`；`scopes` 用来按令牌权限收起
动作：登录时客户端会保存令牌返回的 scopes，前端只显示 scopes 全部满足的
动作。旧令牌没有记录 scopes 时视为未知，全部显示，由服务端最终鉴权。

## 4. 在 TUI / GTK 里使用

TUI：

- 按 `P` 打开插件动作列表；没装插件时显示扫描目录。
- `↑↓` 选择动作，`Enter` 打开表单。
- `Tab` / `↑↓` 切换字段；文本字段直接输入。
- `Enter` 执行；多行字段的 `Enter` 会调用 `$EDITOR` 编辑内容。
- `Ctrl-S` 也可以执行；`Esc` 逐级返回。

GTK：

- 只有当前令牌至少能用上一个动作时，侧边栏才出现“插件”页；没装插件时
  界面不变。
- 点动作行打开对话框，文本字段用输入框，多行字段用等宽文本框。
- “执行”在后台线程调用插件，结果用 toast 显示，失败留在对话框里提示。

动作成功后插件可以返回 `{"refresh":"news"}`（也支持 `forum` / `me`），
TUI 会清掉对应缓存并在当前页重新加载。

## 5. 加载路径

按顺序扫描：

1. `$SPARK_PLUGIN_PATH`（冒号分隔）；
2. `$XDG_DATA_HOME/spark-client/plugins`（默认 `~/.local/share/...`）；
3. 构建时安装目录（`<libdir>/spark-client/plugins`）；
4. `/usr/lib/spark-client/plugins`、`/usr/local/lib/spark-client/plugins`；
5. `/usr/lib/*/spark-client/plugins` 等多架构目录。

同一个 id 只加载一次，重复的会被跳过。

## 6. 构建与打包

```bash
cmake -S . -B build -DSPARK_TUI_BUILD_PLUGINS=ON
cmake --build build
```

- 插件目标默认关闭；
- 每个插件一个 `.deb`，例如 `sparkapp-plugin-news`，只依赖
  `sparkapp-core (= 版本)`；
- `sparkapp-tui` / `sparkapp-gui` **不依赖**插件，最多在文档里提示；
- 插件包安装到 `<libdir>/spark-client/plugins/`，和前端包完全解耦。

加载器本身也支持 Windows（`.dll` + `LoadLibrary`）；示例插件和 Debian 包目前
只在 Linux 构建。

当前示例：`plugins/news_admin/`（新闻保存/删除、主页置顶增删）。
