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

## PocketJS NOTE4 编译与烧录

所有 `just` 命令都从仓库根目录执行。`build`、`build-package`、`device-info`、`flash` 和 `monitor` 会通过 `IDF_EXPORT` 自动加载 ESP-IDF 6.1；先设置该变量。首次准备依赖时执行：

```powershell
$env:IDF_EXPORT = "C:/Users/<user>/esp/esp-idf-v6.1/export.ps1"
git submodule update --init --recursive
just prepare
```

`just build` 会调用仓库锁定的 `third_party/pocketjs/tools/pocket.ts` 生成应用包，再用 ESP-IDF 构建 ESP32-S3 固件。它不会使用系统 PATH 中可能存在的其他 PocketJS CLI。默认构建目录是 `examples/pocketjs-note4/.pocket-build/idf`；如需缩短 Windows 路径或更换目录，可以设置：

```powershell
$env:NOTE4_BUILD_DIR = "C:/n4idf"
just build
```

运行主机侧显示转换测试：

```powershell
just test
```

烧录前必须确认设备是 NOTE4 v1.0、串口是 COM5，并且存在完整 16 MiB 原始 Flash 备份。检查备份和设备信息后烧录：

```powershell
just verify-backup
just device-info
just flash
```

`just flash` 会先执行 pinned PocketJS 包生成、ESP-IDF 构建、备份校验和设备信息检查，然后把 bootloader、分区表和应用写入 `COM5`。更换串口时设置 `NOTE4_PORT`；更换备份文件时设置 `NOTE4_BACKUP`，可选地设置 `NOTE4_BACKUP_SHA256`：

```powershell
$env:NOTE4_PORT = "COM5"
$env:NOTE4_BACKUP = "C:/backups/note4-original-16MiB.bin"
$env:NOTE4_BACKUP_SHA256 = "<SHA256>"
just flash
```

只使用已有 `.pocket` 包时，设置 `POCKETJS_NOTE4_PACKAGE` 后执行 `just build-package`：

```powershell
$env:POCKETJS_NOTE4_PACKAGE = "C:/packages/note4-hello.pocket"
just build-package
```

构建失败时先执行 `just clean`，再重新运行 `just build`。不要直接运行 `idf.py build` 来替代 `just build`，因为该流程还需要生成 pinned PocketJS 包和 NOTE4 波形依赖。若没有设置 `IDF_EXPORT`，recipe 会直接失败并提示设置路径。
