# 自建 Gitea / Forgejo 不使用 Actions

这个目录存在的唯一目的：Gitea 检测到 `.gitea/workflows` 后会忽略
`.github/workflows`，因此不会把 GitHub Actions 工作流排进没有 runner 的队列。

CI 在 GitHub Actions（主）和极狐 GitLab（备用）上运行。除非自建 runner
恢复，不要在这里放工作流 YAML。

如果仍然看到 `Waiting to run`，在仓库设置中关闭 Actions 单元，或到
「工作流」页面手动取消旧任务。
