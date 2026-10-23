# Vendored agent skills

摘自 [mattpocock/skills](https://github.com/mattpocock/skills)（插件 `mattpocock-skills` v1.3.1），
安装于 2026-10-23，直接作为本仓库的 Claude Code 项目级 skills 使用。

- 来源：`skills/engineering/*` 与 `skills/productivity/*`，共 27 个 skill。
- 未包含：`skills/in-progress/`（beta）、`skills/misc/`（低频）、`skills/deprecated/`。
- 本地改名：上游的 `code-review` 重命名为 `mattpocock-code-review`，避免与 Claude Code
  内置的 `/code-review` 同名遮蔽；相关 skill 内的引用已同步更新。
- 用法：在 Claude Code 中运行 `/<skill-name>`，例如 `/grill-me`、`/tdd`、`/mattpocock-code-review`。
- 更新：从上游重新拷贝对应目录即可；本地改名与引用调整需要保留。
- 许可证：MIT，见 [LICENSE](./LICENSE)。
