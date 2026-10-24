set windows-shell := ["pwsh", "-NoLogo", "-NoProfile", "-Command"]

repo_root := justfile_directory()
project := repo_root
test_project := project + "/tests"
build_dir := env_var_or_default("NOTE4_BUILD_DIR", project + "/.pocket-build/idf")
test_build_dir := test_project + "/build"
test_config := env_var_or_default("NOTE4_TEST_CONFIG", "Debug")
port := env_var_or_default("NOTE4_PORT", "COM5")
backup := env_var_or_default("NOTE4_BACKUP", repo_root + "/note4-build/original-flash/note4-com5-original-16MiB.bin")
backup_sha256 := env_var_or_default("NOTE4_BACKUP_SHA256", "")
package := env_var_or_default("POCKETJS_NOTE4_PACKAGE", "")
idf_export := env_var_or_default("IDF_EXPORT", "")
pocketjs_root := repo_root + "/third_party/pocketjs"
pinned_package_dir := project + "/.pocket-build/package"
pinned_package := pinned_package_dir + "/note4-hello.pocket"

_default:
    @just --list

# Prepare the pinned PocketJS overlay, waveform payload, and NOTE4 component.
prepare:
    python -I {{project}}/tools/prepare_pocketjs.py
    python -I {{project}}/tools/extract_waveform.py
    python -I {{project}}/tools/prepare_deps.py

# Build and run the host-side display conversion tests.
test:
    cmake -S {{test_project}} -B {{test_build_dir}}
    cmake --build {{test_build_dir}} --config {{test_config}}
    ctest --test-dir {{test_build_dir}} -C {{test_config}} --output-on-failure

# Build the app package with the pinned PocketJS checkout.
package-pinned: prepare
    bun {{pocketjs_root}}/tools/pocket.ts build --host-profile {{project}}/pocket.host.json --manifest {{project}}/pocket.json --project-root {{project}} --outdir {{pinned_package_dir}} --plan-dir {{pinned_package_dir}}/plans --output {{pinned_package}}

# Configure the ESP-IDF project for ESP32-S3 and build the firmware.
build: package-pinned
    if ("{{idf_export}}" -eq "") { throw "Set IDF_EXPORT to the ESP-IDF export.ps1 path, for example C:/Users/<user>/esp/esp-idf-v6.1/export.ps1" }; . "{{idf_export}}" | Out-Null; $env:RUSTUP_TOOLCHAIN = if ($env:RUSTUP_TOOLCHAIN) { $env:RUSTUP_TOOLCHAIN } else { "esp" }; idf.py --project-dir {{project}} -B {{build_dir}} -DPOCKETJS_NOTE4_PACKAGE="{{pinned_package}}" set-target esp32s3
    if ("{{idf_export}}" -eq "") { throw "Set IDF_EXPORT to the ESP-IDF export.ps1 path, for example C:/Users/<user>/esp/esp-idf-v6.1/export.ps1" }; . "{{idf_export}}" | Out-Null; $env:RUSTUP_TOOLCHAIN = if ($env:RUSTUP_TOOLCHAIN) { $env:RUSTUP_TOOLCHAIN } else { "esp" }; idf.py --project-dir {{project}} -B {{build_dir}} -DPOCKETJS_NOTE4_PACKAGE="{{pinned_package}}" build

# Build with an existing .pocket package instead of invoking the PocketJS CLI.
build-package: prepare
    if ("{{package}}" -eq "") { throw "Set POCKETJS_NOTE4_PACKAGE to a .pocket package path" }; if ("{{idf_export}}" -eq "") { throw "Set IDF_EXPORT to the ESP-IDF export.ps1 path, for example C:/Users/<user>/esp/esp-idf-v6.1/export.ps1" }; . "{{idf_export}}" | Out-Null; $env:RUSTUP_TOOLCHAIN = if ($env:RUSTUP_TOOLCHAIN) { $env:RUSTUP_TOOLCHAIN } else { "esp" }; idf.py --project-dir {{project}} -B {{build_dir}} -DPOCKETJS_NOTE4_PACKAGE="{{package}}" set-target esp32s3
    if ("{{package}}" -eq "") { throw "Set POCKETJS_NOTE4_PACKAGE to a .pocket package path" }; if ("{{idf_export}}" -eq "") { throw "Set IDF_EXPORT to the ESP-IDF export.ps1 path, for example C:/Users/<user>/esp/esp-idf-v6.1/export.ps1" }; . "{{idf_export}}" | Out-Null; $env:RUSTUP_TOOLCHAIN = if ($env:RUSTUP_TOOLCHAIN) { $env:RUSTUP_TOOLCHAIN } else { "esp" }; idf.py --project-dir {{project}} -B {{build_dir}} -DPOCKETJS_NOTE4_PACKAGE="{{package}}" build

# Confirm that the original full-flash backup is present and optionally hash it.
verify-backup:
    $path = [IO.Path]::GetFullPath("{{backup}}"); if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing NOTE4 backup: $path" }; $file = Get-Item -LiteralPath $path; if ($file.Length -ne 16777216) { throw "NOTE4 backup must be exactly 16777216 bytes: $($file.Length)" }; $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToUpperInvariant(); if ("{{backup_sha256}}" -ne "" -and $hash -ne "{{backup_sha256}}".ToUpperInvariant()) { throw "NOTE4 backup SHA-256 mismatch: $hash" }; "NOTE4 backup verified: $path ($($file.Length) bytes, SHA-256 $hash)"

# Query the connected board and flash capacity without changing flash contents.
device-info:
    if ("{{idf_export}}" -eq "") { throw "Set IDF_EXPORT to the ESP-IDF export.ps1 path, for example C:/Users/<user>/esp/esp-idf-v6.1/export.ps1" }; . "{{idf_export}}" | Out-Null; python -m esptool --chip esp32s3 -p {{port}} chip-id
    if ("{{idf_export}}" -eq "") { throw "Set IDF_EXPORT to the ESP-IDF export.ps1 path, for example C:/Users/<user>/esp/esp-idf-v6.1/export.ps1" }; . "{{idf_export}}" | Out-Null; python -m esptool --chip esp32s3 -p {{port}} flash-id

# Verify the backup and target, then flash the built bootloader, partition table, and app.
flash: build verify-backup device-info
    if ("{{idf_export}}" -eq "") { throw "Set IDF_EXPORT to the ESP-IDF export.ps1 path, for example C:/Users/<user>/esp/esp-idf-v6.1/export.ps1" }; . "{{idf_export}}" | Out-Null; idf.py --project-dir {{project}} -B {{build_dir}} -p {{port}} flash

# Open the serial monitor for the selected port using the current build ELF.
monitor:
    if ("{{idf_export}}" -eq "") { throw "Set IDF_EXPORT to the ESP-IDF export.ps1 path, for example C:/Users/<user>/esp/esp-idf-v6.1/export.ps1" }; . "{{idf_export}}" | Out-Null; idf.py --project-dir {{project}} -B {{build_dir}} -p {{port}} monitor

# Flash and immediately monitor the selected port after the safety checks.
flash-monitor: flash
    if ("{{idf_export}}" -eq "") { throw "Set IDF_EXPORT to the ESP-IDF export.ps1 path, for example C:/Users/<user>/esp/esp-idf-v6.1/export.ps1" }; . "{{idf_export}}" | Out-Null; idf.py --project-dir {{project}} -B {{build_dir}} -p {{port}} monitor

# Remove generated ESP-IDF output for the selected build directory.
clean:
    if (Test-Path -LiteralPath "{{build_dir}}") { Remove-Item -LiteralPath "{{build_dir}}" -Recurse -Force }
