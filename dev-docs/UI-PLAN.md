# 主页与详情渲染计划

> 状态：计划阶段，尚未实现  
> 目标：启动进入主页；详情默认渲染为人可读内容；底层 JSON 可通过设置项和快捷键查看。

## 1. 需求拆解

1. **主页**
   - 启动不再直接进入新闻列表；
   - 默认落到主页，展示站点概览、服务地址、快捷入口；
   - 新闻/联系/平台/搜索/我的仍然保留；
   - 不要求主页阻塞启动，网络失败时也要能显示。

2. **详情渲染**
   - 当前详情页是 `BuildJsonLines()` 的通用 JSON，属于底层数据；
   - 默认改为按 kind 渲染：新闻、联系人、平台、账号信息；
   - 用户可在设置中打开「显示底层数据」，关闭时显示渲染结果；
   - 详情页提供快捷键临时切换底层数据。

3. **设置**
   - 现有设置页增加一个「界面与显示」区域或条目；
   - 持久化到 TOML；
   - 不影响服务源 / 实例地址设置。

4. **底部提示与帮助页**
   - 底部状态栏不再显示一长串快捷键；
   - 底部左侧只显示「? 帮助」；
   - 所有快捷键、设置页操作、搜索 / 详情 / 登录等说明集中到帮助页；
   - 帮助页内容较多，需要支持上下滚动。

## 2. 主页设计

### 2.1 导航方案（推荐）

把主页作为第一个标签页，现有标签整体后移：

```text
1 主页   2 新闻   3 联系   4 平台   5 搜索   6 论坛   7 我的
```

启动 `tab_ = Tab::Home`，不再同步加载新闻首页。

`Tab` 枚举重构为：

```cpp
enum class Tab {
  Home = 0,
  News = 1,
  Contact = 2,
  Platform = 3,
  Search = 4,
  Me = 5,
};
```

需要同步调整：

- `TabBar()` 名称数组从 5 项改为 6 项；
- `1`–`6` 快捷键映射；
- `Tab` / `TabReverse` 的模数从 5 改为 6；
- `SwitchTab()` 里对 Search / Me / Home 的分支；
- `ListFor()` 只处理 News / Contact / Platform；
- `LoadList()` 的 `kind` 映射保持不变；
- `CurrentList()` / `MoveSelection()` 在 Home 下需要安全返回；
- `Hints()` / `HelpView()` 更新。

> 备选方案：主页作为独立 overlay，按 `h` 打开，保留 1–5 标签。
> 优点是改动小；缺点是首页不可见、发现性差。推荐第一种。

### 2.2 主页内容

最终布局（面向普通用户，不暴露技术细节）：

```text
Spark ORG
资讯、联系人与平台入口

最新新闻                                      共 N 条
  2026-09-25  Spark ORG 域名回滚：cc.cd → dpdns.org
  2026-09-24 18  Spark Conf 26.9
  2026-10-20  欢迎：这是用于调试的示例新闻

r 刷新
```

说明：

- 服务地址、同步源、手动 / 自动标记、API 版本、配置文件路径等技术信息
  全部收进设置页，不在主页展示；
- 「最新新闻」显示 5 条，只显示 `active = true` 的条目；
- 新闻日期按接口的 `date.accuracy` 截断（year / month / day / hour /
  minute / second），不再固定显示 `00:00:00`；
- 预览加载失败时显示友好提示「暂时无法获取新闻，请稍后重试」，不影响
  主页其它内容；
- 主页不做选择列表，保持轻量；回车不响应，用户按 `2` 进入新闻页；
- 顶部栏只保留站点名、六个标签和登录状态，避免窄终端下被截断。

### 2.3 启动加载策略

当前 `App::Run()`：

```text
LoadSiteSync() → 同步加载 news 第一页 → 进入 UI
```

改为：

```text
LoadSiteSync()（仍需站点名 / 语言 / API 版本）
  → tab_ = Home
  → 后台加载 Home 预览（最近 3 条新闻，可选）
  → 用户按 2 才进入新闻页并加载完整列表
```

- `LoadSiteSync()` 保持同步，因为语言和站点名到处都要用；
- 新闻第一页不再在启动时同步拉取；
- `Home` 预览用 worker 后台加载，失败只更新主页状态；
- 如果用户直接按 `2`，按现有 `LoadList()` 正常加载。

## 3. 详情渲染设计

### 3.1 状态

`App` 增加：

```cpp
std::string detailKind_;      // news / contact / platform / me
bool detailRaw_ = false;      // 当前详情页是否显示底层 JSON
bool meRaw_ = false;          // 我的页面是否显示底层 JSON
```

打开详情时：

```cpp
detailKind_ = item.kind;
detailRaw_ = cfg_.showRawDetails;  // 默认来自设置
```

按 `x`：

```text
detailRaw_ = !detailRaw_;
```

`meRaw_` 同理。

### 3.2 渲染函数

新增 `src/ui/render_detail.h/.cpp`，与 FTXUI 解耦，便于单测：

```cpp
std::vector<std::string> RenderDetailLines(
    const std::string& kind,
    const nlohmann::json& data,
    int width,
    bool raw);

std::vector<std::string> RenderMeLines(
    const nlohmann::json& data,
    int width,
    bool raw);
```

`raw == true` 时直接复用现有 `BuildJsonLines()`，保持现在的行为。

### 3.3 各 kind 的渲染字段

**新闻 `news`**

```text
标题
日期 · 地点
状态: 启用 / 停用

正文（desc，HTML 去标签）
…

图片
  - alt: url
链接
  - name: url
原文: url
```

- `desc` 可能包含 HTML：去掉标签、保留 `<br>` / `</p>` 换行；
- 解码常见实体：`&amp; &lt; &gt; &quot; &#39; &nbsp;`；
- 长文本用 `WrapText()` 按终端宽度换行；
- `place.desc` / `place.geo` 有值才显示；
- `active == false` 显示「停用」；
- `priority` / `slug` 默认只在底层数据里显示。

**联系人 `contact`**

```text
姓名
职务

电话
  - …
备用电话
  - …
邮箱
  - …
备用邮箱
  - …
主页
  - name: url
备用主页
  - name: url
社交
  - name: url
PGP 指纹: …
PGP 公钥: （默认折叠，显示前几行 / 提示按 x 看完整数据）
```

- 数组为空则整段跳过；
- `pgpKey` 约 2KB，渲染模式只显示指纹和「公钥已省略，按 x 查看底层数据」；
- `telExtra` / `emailExtra` / `homepageExtra` 与主字段分开显示。

**平台 `platform`**

```text
名称
简介
按钮: button
地址: url
站内页: pageUrl
```

- `url` 是外链，`o` 可直接打开；
- `pageUrl` 是站内锚点，保留展示但不强制打开。

**我的 `me`**

```text
用户名
显示名
角色
最近在线
头像: url / 无
邮箱（PGP 声明）
  - …
PGP 指纹 / 公钥
```

- 字段来自 `/api/v2/me.json`，可能包含 `user` 包裹；渲染函数兼容 `data.user` 和顶层两种结构；
- 底层模式仍显示完整 JSON。

### 3.4 HTML / Markdown 处理

首版只做最小 HTML 清理，不追求完整 Markdown：

- 去标签、解码常见实体；
- `<br>`、`</p>`、`</div>`、`</li>` → 换行；
- 保留纯文本段落；
- 不做加粗/链接样式（终端颜色可后续再加）；
- 图片只显示 URL，不下载。

## 4. 设置项

### 4.1 配置

`Config` 增加：

```cpp
bool showRawDetails = false;   // false = 渲染，true = 底层 JSON
```

TOML：

```toml
[ui]
show_raw_details = false
```

`config.example.toml` 同步更新，`Config::Load/Save` 与测试覆盖。

### 4.2 设置页

当前设置页只有一个条目「服务源与实例地址」。增加第二个条目：

```text
设置
▶ 服务源与实例地址              >
  详情显示底层数据              关
```

行为：

- 选中「详情显示底层数据」；
- `Enter` / `Space` 切换 开 / 关；
- 立即写入 `cfg_.showRawDetails` 并保存 TOML；
- 不改变当前已打开的详情页（当前页用 `x` 临时切换）；
- 菜单项右侧显示 `开` / `关`。

如果以后显示项变多，再把它拆成「界面与显示」子页；首版直接放一个 toggle，避免过度设计。

### 4.3 快捷键

推荐：详情页 / 我的页面按 `x` 临时切换：

```text
x  切换 渲染 / 底层数据
```

- `x` 只在 Detail / Me 模式生效；
- 切换后底部状态显示「底层数据: 开」或「底层数据: 关」；
- 不持久化临时切换；持久化默认值走设置项；
- 如果当前详情来自搜索，仍按 `detailKind_` 渲染；
- 在列表页按 `x` 无效果；
- 在搜索输入框里 `x` 仍然正常输入。

其他候选键：`d`（data）、`D`、`Ctrl-D`。推荐 `x`，因为当前未占用且不容易和 `d` 的删除语义混淆。

### 4.4 底部提示与帮助页

#### 底部提示

当前 `App::Hints()` 会根据标签页 / 模式返回一长串快捷键。改为：

```cpp
std::string App::Hints() const { return "? 帮助"; }
```

底部布局保持不变：

```text
? 帮助                                          当前状态 / API 地址
```

- 左侧固定只显示 `? 帮助`；
- 右侧继续显示状态消息或当前 API 地址；
- 不再在底部显示 `q 退出`、`↑↓ 选择`、`Enter 详情` 等提示；
- `Ctrl-C` 仍可随时退出，`q` 的说明放到帮助页；
- 设置页、详情页、搜索页的底部也统一只显示 `? 帮助`。

#### 帮助页

`HelpView()` 改为分组 + 可滚动内容：

```text
快捷键
────────────────────────────
全局
  1–7              主页 / 新闻 / 联系 / 平台 / 搜索 / 论坛 / 我的
  Tab / Shift-Tab  切换标签
  ?                打开 / 关闭帮助
  s                设置
  l                切换语言
  r                刷新当前页面
  /                搜索
  L                设备码登录
  O                登出并吊销令牌
  q                退出（Ctrl-C 随时退出）

列表
  ↑ ↓ / j k        选择条目
  PgUp / PgDn      翻页
  Home / End       首条 / 末条
  Enter            查看详情
  n                加载下一页
  o                在浏览器打开当前条目

详情
  ↑ ↓ / j k        滚动
  PgUp / PgDn      翻页
  Home / End       顶部 / 底部
  Esc              返回列表
  o                在浏览器打开

搜索
  输入             编辑关键词
  Enter            搜索
  ↑ ↓              选择结果
  Enter            查看详情
  Esc              清空 / 返回

我的
  ↑ ↓              滚动
  r                刷新
  L                登录
  O                登出
  s                设置

设置
  ↑ ↓ / j k        选择条目
  Enter            进入
  Esc / q          关闭设置

服务源与实例地址
  Tab / ↑ ↓        切换字段
  Enter / Ctrl-S   保存
  F5 / Ctrl-R      立即同步服务源
  Esc              返回设置

设备码登录
  Esc              取消登录

帮助
  ↑ ↓              滚动
  PgUp / PgDn      翻页
  Home / End       顶部 / 底部
  ? / Esc          关闭帮助
```

实现要点：

- `App` 增加 `int helpScroll_ = 0;`；
- 打开帮助时 `helpScroll_ = 0;`；
- `HelpView()` 构造 `std::vector<std::string>`，用现有 `ScrollLines(lines, helpScroll_, height)` 渲染；
- 帮助页高度取 `screen_.dimy() - 8`，与详情页一致；
- 帮助页事件处理：

```cpp
if (event == Event::Escape || event == Event::Character('?')) {
  help_ = false;
} else if (event == Event::Character('q')) {
  screen_.Exit();
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
```

- 帮助页右上角显示 `↑↓ 滚动` 和当前滚动位置（可选）；
- 帮助页内容与当前实际快捷键保持一致，后续新增快捷键必须同步更新；
- 设置页 / 详情页的 `Hints()` 不再返回长文本，避免维护两份说明。

## 5. 实现步骤

### 阶段 1：配置 + 设置项 + 快捷键

- [x] `Config` 增加 `showRawDetails`，TOML 读写
- [x] 设置页增加「详情显示底层数据」toggle
- [x] `App` 增加 `detailKind_` / `detailRaw_` / `meRaw_`
- [x] Detail / Me 支持 `x` 切换
- [x] `Hints()` 固定返回 `? 帮助`
- [x] `HelpView()` 改为分组快捷键 + 滚动（`helpScroll_`）
- [x] 帮助页覆盖全局 / 列表 / 详情 / 搜索 / 我的 / 设置 / 实例地址 / 登录
- [x] 单元测试：配置往返、默认值

### 阶段 2：渲染器

- [x] 新增 `src/ui/render_detail.h/.cpp`
- [x] news / contact / platform / me 四类渲染
- [x] HTML 去标签 / 实体解码 / 换行
- [x] 长文本按终端宽度换行
- [x] raw 模式复用现有 `BuildJsonLines`
- [x] 单元测试：四类 fixture、空字段、HTML、PGP 省略

### 阶段 3：主页

- [x] `Tab` 枚举增加 `Home` / `Forum`，重构 1–7 快捷键
- [x] `TabBar` 六项
- [x] 启动 `tab_ = Home`
- [x] `HomeView()`：站点名、简介、最新新闻预览（技术信息移入设置）
- [x] 后台加载 5 条 active 新闻预览
- [x] 主页 `r` 刷新，`s` 设置，`L` 登录
- [x] 移除启动时的同步新闻列表加载
- [x] 帮助页 / README 更新
- [x] 新闻日期按 `date.accuracy` 截断显示

### 阶段 4：验证与文档

- [x] 脚本化 UI 测试：启动主页、按 2 进新闻、详情 `x` 切换
- [x] 设置 toggle 持久化验证
- [x] 离线 / 新闻接口失败时主页仍可用
- [x] `.deb` 重新打包
- [x] README、`docs/config.example.toml`、`dev-docs/TODO.md` 更新

## 6. 涉及文件

```text
src/config.h/.cpp              # show_raw_details
src/ui/app.h/.cpp              # Tab::Home、HomeView、详情 raw 状态、settings toggle
src/ui/render_detail.h/.cpp    # 新增：按 kind 渲染
src/ui/app.cpp                 # BuildJsonLines 保留给 raw 模式
tests/test_core.cpp            # 配置 + 渲染器测试
docs/config.example.toml       # [ui] show_raw_details
README.md                      # 启动页、快捷键、设置项
dev-docs/UI-PLAN.md            # 本文档
```

## 7. 验收标准

- 启动进入主页，不自动进入新闻列表；
- 主页只展示站点名、简介和最新新闻，服务地址等技术信息在设置中；
- 新闻日期按 `date.accuracy` 显示，精度之后的单位隐藏；
- 新闻 / 联系 / 平台详情默认显示渲染内容，不再直接显示 JSON；
- 设置中可切换「详情显示底层数据」，并写入 TOML；
- 任意详情页按 `x` 可临时查看完整底层 JSON，再按 `x` 返回渲染；
- 我的页面同样支持渲染 / 底层切换；
- 底层 JSON 与现有 `BuildJsonLines` 输出一致；
- 底部提示条只显示 `? 帮助`（右侧仅在操作后显示状态信息）；
- 帮助页包含全局、列表、详情、搜索、我的、设置、实例地址、登录的全部快捷键；
- 帮助页内容超过一屏时可上下滚动；
- 新闻接口失败时主页、设置、其它已缓存页面仍可操作；
- 单元测试通过，`.deb` 正常构建。

## 8. 待确认

1. 主页是否作为第 1 个标签（推荐），还是独立 `h` overlay？
2. 主页要不要显示「最新新闻」预览？如果显示，3 条还是 5 条？
3. 设置项用「详情显示底层数据」直接 toggle（推荐），还是放到「界面与显示」子页？
4. 快捷键用 `x`（推荐）还是 `d` / `D`？
5. 渲染详情只显示中文标签，还是按界面语言中英切换？
6. 「我的」页面是否也默认渲染，还是保持 JSON？
7. PGP 公钥在渲染模式是否只显示指纹（推荐），完整公钥仅底层模式可见？
8. 底部提示条是否只保留 `? 帮助`（推荐），还是额外保留 `q 退出`？
