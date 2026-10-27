# Repository Conventions

## Attribution

Do not include Claude Code attribution when committing code or creating PRs in this repository:

- Do not add `Co-Authored-By: Claude Code`.
- Do not add `Generated with Claude Code` or any other Claude Code attribution or promotional lines.

Commit messages should simply describe the changes.

## Agent skills

Project-level skills are located in `.claude/skills/`. See `.claude/skills/README.md` for their sources and documentation.

### Issue tracker

Issues live in this repo's GitHub Issues, driven by the `gh` CLI. See `docs/agents/issue-tracker.md`.

### Triage labels

Five default roles: `needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, `wontfix`. See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: root `GLOSSARY.md` plus `docs/adr/`. See `docs/agents/domain.md`.

## Building and Flashing the NOTE4 Native Rust UI

Run all `just` commands from the repository root. Use ESP-IDF 6.1 and set `IDF_EXPORT` to the SDK's `export.ps1` according to your local installation. The build scripts use PowerShell and require Python, uv, just, and the Espressif Rust `esp` toolchain (target: `xtensa-esp32s3-none-elf`).

```text
just build
just test
just test-ui
```

`just prepare` generates Rust fonts, icons, time zones, and NOTE4 waveforms; `just build` runs preparation before building the ESP32-S3 firmware. Build output defaults to `.note4-build/idf` within the repository; use `NOTE4_BUILD_DIR` to specify a different build directory. If a build fails, run `just clean` followed by `just build`. Do not bypass preparation by running `idf.py build` directly.

Before flashing, confirm that the device is a monochrome NOTE4 v1.0. Set `NOTE4_PORT` to its actual serial port and `NOTE4_BACKUP` to a complete 16 MiB raw flash backup of that device; use `NOTE4_BACKUP_SHA256` to specify a checksum if needed. Do not identify the current device based on the repository's default serial port or backup filename. Never delete or overwrite the backup.

```text
just verify-backup
just device-info
just flash
```

`just flash` builds the firmware, verifies the backup, checks device information, and flashes the device, in that order; `just flash-dev` uses the same verification process. The device information check does not replace confirming the panel model.

## Development Screenshots and Release Builds

During development, use the following commands with the actual device's `NOTE4_PORT` and backup configuration:

```text
just build-dev   # Build development firmware only
just flash-dev   # Build and flash development firmware
just screenshot  # Capture screenshots repeatedly after flashing once
```

Use the provided tooling to export the current quota page. Do not make ad hoc source changes to add screenshot interfaces. Screenshots capture the device framebuffer and do not replace acceptance checks on the physical screen. Output is saved in the ignored directory `.note4-build/validation/screenshots/` within the repository; run serial monitoring, screenshot capture, and flashing sequentially. Switch back to a release build with `just flash` before delivery.

`just build` / `just flash` create release builds by default, explicitly disable `NOTE4_DEVELOPMENT`, and check that the ELF/BIN files contain neither the screenshot protocol nor its entry points. Release builds must not include these interfaces. Development builds default to `${NOTE4_BUILD_DIR}-dev`; use `NOTE4_DEV_BUILD_DIR` to specify a different directory. After a failure, run `just clean-dev` before rebuilding. See `docs/development-screenshots.md` for details.

The provisioning page uses native TypeScript DOM APIs. Validate it with `bun run --cwd provisioning-web build` and `bun run --cwd provisioning-web test`.
