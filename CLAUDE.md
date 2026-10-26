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

所有 `just` 命令从仓库根目录执行。使用 ESP-IDF 6.1，按本地安装设置 `IDF_EXPORT`，指向 SDK 的 `export.ps1`。构建脚本使用 PowerShell；依赖 Python、uv、just 及 Espressif Rust `esp` toolchain（目标 `xtensa-esp32s3-none-elf`）。

```text
just build
just test
just test-ui
```

`just prepare` 生成 Rust 字体、图标、时区和 NOTE4 波形；`just build` 先准备再构建 ESP32-S3 固件。默认输出仓库内 `.note4-build/idf`，可用 `NOTE4_BUILD_DIR` 指定构建目录。构建失败先 `just clean` 再 `just build`，不要直接用 `idf.py build` 跳过准备。

烧录前确认设备是黑白 NOTE4 v1.0，并设置 `NOTE4_PORT` 为实际串口、`NOTE4_BACKUP` 为该设备完整的 16 MiB 原始 Flash 备份；可用 `NOTE4_BACKUP_SHA256` 指定校验值。不要依赖仓库默认的串口和备份文件名判断当前设备。备份不能删除或覆盖。

```text
just verify-backup
just device-info
just flash
```

`just flash` 依次执行构建、备份校验、设备信息检查及烧录；`just flash-dev` 使用相同的校验流程。设备信息检查不能代替确认面板型号。

## 开发截图与 release

开发时使用以下命令，沿用实际设备的 `NOTE4_PORT` 和备份配置：

```text
just build-dev   # 仅编译开发固件
just flash-dev   # 编译并烧录开发固件
just screenshot  # 烧录一次后可反复截图
```

使用正式工具导出当前额度页，不要再临时修改源码添加截图接口。截图为设备帧缓冲，不能代替物理屏幕验收。输出位于仓库内忽略目录 `.note4-build/validation/screenshots/`；串口监控、截图、烧录依次执行。交付前用 `just flash` 切回 release。

`just build` / `just flash` 默认构建 release，显式关闭 `NOTE4_DEVELOPMENT`，并检查 ELF/BIN 不含截图协议和入口。release 不允许包含相关接口。开发构建默认位于 `${NOTE4_BUILD_DIR}-dev`，可用 `NOTE4_DEV_BUILD_DIR` 指定；失败后先 `just clean-dev` 再重建。详见 `docs/development-screenshots.md`。

配网页使用原生 TypeScript DOM；用 `bun run --cwd provisioning-web build` 和 `test` 验证。
