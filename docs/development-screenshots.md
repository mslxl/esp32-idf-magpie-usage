# 开发 USB 截图

开发过程中需要反复检查设备实际渲染结果，使用正式开发固件接口；release 固件不包含该接口。

## 使用

从仓库根目录执行，沿用黑白 NOTE4 v1.0、COM5 和原始 16MiB 备份校验要求：

```powershell
$env:IDF_EXPORT = "C:/Users/<user>/esp/esp-idf-v6.1/export.ps1"
$env:NOTE4_BUILD_DIR = "C:/n4idf-rust"
just flash-dev
just screenshot
```

开发固件只需烧录一次，即可重复 `just screenshot`。按设备 UP/DOWN 切换页面后再次截图。截图不改变设置、布局或页码，不触发重绘；导出屏幕任务最近成功呈现并接受的 1bpp 帧缓冲（呈现器的已显示帧，Rust 直接绘制的同一格式，无 RGB565 转换）。开发构建的屏幕任务每 100ms 唤醒一次以处理截图命令；release 只在事件或截止时间唤醒。截图期间屏幕任务会短暂用于 USB 导出，网络任务与 LED 定时器独立运行。

默认 PNG 和 JSON 校验信息保存到忽略目录 `.note4-build/validation/screenshots/`，使用带微秒的时间戳命名，不覆盖已有文件。`NOTE4_PORT` 覆盖 COM5。工具通过 uv 使用固定版本 pyserial，不需要手动安装。

需要指定输出或等待时长时：

```powershell
uv run --with pyserial==3.5 python -I tools/note4_screenshot.py --port COM5 --seconds 120 --output .note4-build/validation/page.png
```

串口监控、烧录与截图必须依次执行；COM5 被占用时先关闭占用程序。打开截图串口之前设置 DTR/RTS 为 false，避免截图主动复位设备。工具仅解析固定格式的帧数据，不输出或保存任意设备日志。

当前接口仅允许已连接、存在额度快照、无配对/设置/错误页面的额度页。其他页面返回 `N4SHOT WAIT`，工具等待至超时并说明需要开发固件和可截图页面。截图含屏幕上本来可见的账号及额度信息；它是 400×300 渲染结果，不代表实际墨水屏残影、对比度或物理可读性。

## 开发与 release 隔离

- `just build-dev` / `just flash-dev` 显式设置 `NOTE4_DEVELOPMENT=ON`，默认输出 `${NOTE4_BUILD_DIR}-dev`，可用 `NOTE4_DEV_BUILD_DIR` 指定。开发构建失败先 `just clean-dev` 再重建。
- `just build` / `just flash` 显式设置 `NOTE4_DEVELOPMENT=OFF`，保留原有 release 输出位置。
- CMake 默认 OFF，只有 ON 时才编译 `main/dev_capture.c`、加入截屏所需 USB 驱动依赖并定义 `NOTE4_DEV_CAPTURE`。屏幕任务的初始化、命令轮询调用均受预处理条件保护。
- `tools/check_release_capture.py` 在 release 构建后检查 CMake 开关、编译命令及 ELF/BIN，发现截图源码、`N4SHOT` 协议标记或截图入口即失败，避免开发缓存误用于 release。

交付前运行 `just flash` 切回 release 固件；无需修改或恢复源码。

## 协议与校验

开发接口只接受一行 `SHOT`，不提供配置读写或任意内存访问。设备输出 `N4SHOT BEGIN 400 300 15000 <zero-based-page> <pages> <fnv1a>`，随后 300 条行数据和 `N4SHOT END <fnv1a>`。每行是 50 字节 MSB-first，1 表示黑色；PNG 转换时反相成 PNG 灰度语义。

主机拒绝缺行、重复行、越界页码和校验不匹配，超时后重新请求。每张 PNG 的 JSON 包含尺寸、1-based 页码、总页数、帧 FNV-1a、PNG SHA-256 和黑像素数。`just test` 包含协议完整性、PNG 尺寸/极性/CRC 与 release 检查拒绝路径的测试。

## 验证记录（2026-10-27）

`just test`：C 14/14、截图工具 4/4 通过。开发构建和受保护 `just flash-dev` 通过，连续运行两次 `just screenshot` 均取得第 1/3 页的 400×300 帧，FNV-1a 均为 `7db8aa64`，PNG SHA-256 均为 `410450a1412125e16ed0d1884cde8e78e7c4ed93c74540e858035aace2ab244f`，已检查图片。开发 BIN SHA-256 为 `3A6087343263533AE2C7C4EA23B7B6FFB6F02AF9A6C45692D4A8E72E83B0BBDB`。

release 构建通过，排除检查确认编译命令不含截图源码、ELF/BIN 不含协议及入口；受保护 `just flash` 已将设备恢复到 release。release BIN SHA-256 为 `EB52C257B01397F41A42B881245733B322DBC5A4691E50E3BEE68B1888523F72`，原始 Flash 备份校验通过且未覆盖。构建仍存在 SDK 既有 `.note.GNU-stack` 链接警告。

release 设备端反向验证未取得截图：设备不读取截图命令时，主机观察到串口写超时。工具已对读写均设置 250ms 超时，并在写超时时明确提示烧录开发固件，避免等待期限被阻塞写入绕过；失败时不生成 PNG/JSON。接口排除结论同时由编译命令和 ELF/BIN 检查支持。
