# 仓库约定

## 署名

在本仓库中提交代码或创建 PR 时不要署名 Claude Code：

- 不要添加 `Co-Authored-By: Claude Code`
- 不要添加 `Generated with Claude Code` 或其他 Claude Code 署名/推广行

提交信息正常描述改动即可。

## Agent skills

项目级 skills 位于 `.claude/skills/`，来源与说明见 `.claude/skills/README.md`。

### Issue tracker

Issues live in this repo's GitHub Issues, driven by the `gh` CLI. See `docs/agents/issue-tracker.md`.

### Triage labels

Five default roles: `needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, `wontfix`. See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: root `GLOSSARY.md` plus `docs/adr/`. See `docs/agents/domain.md`.
