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

## NOTE4 原生 Rust UI 编译与烧录

所有 `just` 命令从仓库根目录执行。先设置 `IDF_EXPORT` 为 ESP-IDF 6.1 的 export.ps1 路径。

```powershell
$env:IDF_EXPORT = "C:/Users/<user>/esp/esp-idf-v6.1/export.ps1"
$env:NOTE4_BUILD_DIR = "C:/n4idf-rust"
just build
just test
just test-ui
```

`just prepare` 生成 Rust 字体、图标、时区和 NOTE4 波形；`just build` 先准备再构建 ESP32-S3 固件。默认输出 `.note4-build/idf`。Rust 使用 Espressif `esp` toolchain 和 xtensa-esp32s3-none-elf。构建失败先 `just clean` 再 `just build`，不要直接用 `idf.py build` 跳过准备。

烧录前确认设备是黑白 NOTE4 v1.0、串口 COM5，并存在完整 16 MiB 原始 Flash 备份。备份不能删除或覆盖。

```powershell
just verify-backup
just device-info
just flash
```

`just flash` 依次执行构建、备份校验、设备信息检查及烧录。`NOTE4_PORT` 可指定端口，`NOTE4_BACKUP` 指定备份，`NOTE4_BACKUP_SHA256` 可指定校验值。默认备份为 `note4-build/original-flash/note4-com5-original-16MiB.bin`。

配网页使用原生 TypeScript DOM；用 `bun run --cwd provisioning-web build` 和 `test` 验证。
