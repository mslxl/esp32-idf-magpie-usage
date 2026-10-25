# NOTE4 Magpie Usage 逐项验收

目标：黑白 NOTE4 v1.0，400×300，ESP32-S3，COM5。#12–#23 的软件和硬件证据分开记录；本清单的存在不表示实机验收通过。所有结果默认“待测”，完成一项再进入下一项。不要用串口里的 presented 或构建成功代替肉眼观察。

## 构建与产物

从仓库根目录运行：

```powershell
$env:IDF_EXPORT = 'C:/Users/mslxl/esp/esp-idf-v6.1/export.ps1'
$env:NOTE4_BUILD_DIR = 'C:/n4idf-provisioning'
just build
just test
bun run --cwd provisioning-web test
bun run --cwd provisioning-web build
bun test ./tests/quota-app.test.ts ./tests/quota-pages.test.ts ./tests/quota-time.test.ts
bun run --cwd provisioning-web test:browser
```

失败的固件构建先 `just clean`，再 `just build`。固定 PocketJS 在 build 中生成，不调用系统 PocketJS CLI。产物：`.pocket-build/package/` 和所选 NOTE4_BUILD_DIR；自动测试记录见 `docs/provisioning-validation.md`，不同修订的旧结果不可代替本次结果。

烧录是单独的人工验收步骤。本轮开发不自动烧录。先肉眼核对板卡丝印/外观确为黑白 NOTE4 v1.0，确认串口 COM5，再执行 `just verify-backup`、`just device-info`。备份必须完整 16 MiB，记录 SHA-256，不能删除、覆盖。确认后才运行 `just flash`。`device-info` 只能验证芯片/Flash，不能识别彩屏型号。

记录 Git commit、工作树是否有改动、启动 `build version/idf/elf`、bin SHA-256、PocketJS pinned SHA、测试日期。固件默认 1bpp。开机、配对、每个布局/错误页分别拍照，照片放忽略目录 `note4-build/acceptance/`。

## 合成服务器与故障序列

使用合成 key，不给 fixture 生产 key。默认绑定 loopback；设备访问需显式绑定本机 LAN 地址或经已配置的代理。允许 HTTP 仅用于用户显式开启的测试；真实 HTTPS 测试需公开 CA 证书及匹配域名，不能跳过校验。

```powershell
$env:NOTE4_FIXTURE_KEY = 'note4-fixture-only-key'
python -I tools/magpie_fixture.py --bind 0.0.0.0 --port 8787 --prefix /note4 --count 120
```

设置 endpoint 为 `http://<测试机LAN地址>:8787/note4`，显式打开 HTTP。登录 Cookie 固定使用内部端口 3210，和外部 8787 不同。日志仅记录序号/状态/字节数，不记录请求头、正文和 key。默认覆盖五种厂商、多窗口、原币种余额、中文及缺字。

可在忽略目录创建 JSON scenario，通过 `--scenario note4-build/acceptance/scenario.json` 读取。每次合法 GET 消耗一项，最后一项重复。例：

```json
[
  {"body":[{"provider":"Codex","user":"fixture","windows":[{"name":"5h","used":10}]}],"reading":"1"},
  {"status":500},
  {"raw":"broken json"},
  {"body":[]}
]
```

支持字段：`status`、`body`、`raw`、`contentType`、`reading`、`retryAfter`、`delay`（秒）、`dripMs`（每字节毫秒）、`truncate`。分别制作 401→恢复、连续401、403、429秒/date、reading21次、半包、无限慢流（足够长 raw + dripMs）、空数组、重复身份及智能变化序列。每次换序列重启 fixture；不要把重新登录 POST 算作 GET 序号。TLS 可传 `--cert`/`--private-key`，使用已有公开 CA 链；私钥留在仓库外，不提交。

## 按序执行的清单

每项记录：时间、固件 ELF、操作、实际结果、通过/失败、照片或脱敏日志路径。失败时保留现场，修复后复测该项及相关恢复项。

| 项 | 对应票 | 操作 | 预期与证据 |
| --- | --- | --- | --- |
| 1 | #12/#23 | 核对板型、COM5、16 MiB备份/hash，按 just 烧录，冷启动 | 设置或原配置正常启动；肉眼可读，默认1bpp；保存身份/照片 |
| 2 | #12 | 长DOWN进设置，真实Chrome/Edge Web Bluetooth连接，确认六位码；取消一次再配对 | 设备显示匹配码；未认证写被拒；配对成功可读metadata，密码不回填 |
| 3 | #12 | 网络默认一卡，填写再逐个+至五卡；扫描建议、连续输入、稀疏网络保存 | 焦点稳定、最多五槽，保存留在设置；既有密码只显示presence |
| 4 | #12 | Magpie填写代理前缀、HTTP开关、30/3600秒、时区、A/B、自适应；保存并重连 | 所有设置准确回读；两个Tab互不覆盖；新key输入清理、只回keyPresent |
| 5 | #12 | 未保存退出→继续编辑；再确认丢弃；保存后按OK/网页明确退出；重启 | 仅已提交值生效；迁移专用设备的已知旧配置保留Wi-Fi并清旧网关凭据；未知配置明确失败 |
| 6 | #13 | 公共CA域名+前缀，冷启动未校时，外部端口异于Cookie内部端口 | 先必要校时，POST303后直接GET，不跟Location；显示登录/读取状态；日志不含凭据 |
| 7 | #13/#15 | 分别测过期/错误域名/不可信证书、HTTP关闭、POST401、GET401→一次重登→再401、403 | 分类准确；证书/403/认证耗尽暂停；不能自动降级或无限重登；短UP/DOWN不回旧页 |
| 8 | #15 | 同值保存并退出；同URL换key；换实例；慢请求期间进设置 | 可开始新认证轮次；同实例偏好保留；换实例清旧数据/偏好/基线；迟到结果不覆盖 |
| 9 | #14 | 合成多卡/相同身份多卡/同名异身份/重复窗口/0/缺失/无限/110%/余额组成 | 每记录独立，数字/单位/窗口完整；空数组、首次失败、全过滤状态分别明确 |
| 10 | #14/#22 | 半包/HTML/null/包装对象/坏JSON/超预算；旧数据存在时重复 | 明确失败，完整旧数据/目录/智能基线保留；释放资源后新完整结果恢复 |
| 11 | #16 | 30秒常态、慢请求跨周期、reading=1连续超过21次，非法header；网络/429故障 | 单在途，无积压；追加最多20次、完成后至少1.5秒；退避及Retry-After生效 |
| 12 | #16 | 有旧值时断网/连续500，119秒和120秒观察，期间进出设置，再恢复完整/空数组 | 顶部故障与重试占实际空间；临时可翻页；120秒独立错误页，设置暂停计时，恢复保留/钳制页码 |
| 13 | #17 | 改Asia/Shanghai/含DST时区，跨日/周/年、asOf/readDeferred、旧窗口过期、非法/未来时间 | 中文24h/日期和缓存文案正确；过期额度未知；分钟变化不产生秒级刷屏/改变轮询期限 |
| 14 | #18 | 120+卡目录跨块读取，期间更新数据；断连/超时/换实例重用token；隐藏同身份组 | 稳定完整视图，stale明确；隐藏共享而卡不合并；完整目录可恢复隐藏，浏览器原文不丢 |
| 15 | #18/#19 | 分块偏好>1024字节、同offset重试/冲突/错序、FINISH后取消、COMMIT失败 | FINISH不等于保存；冲突/失败保留网页草稿与旧提交；重启偏好完整 |
| 16 | #19 | provider/type/query组合，Unicode casefold，自定义移动，消失/重现、明确清理、模式切换 | 隐藏优先，维度AND/多选OR；新组尾部；消失偏好保留至清理；自定义序不被模式切换覆盖 |
| 17 | #20 | 原序→仅B额度变化→更新时间/改名/窗口乱序→微小decimal变化→重复/删除/重现；中插失败 | 初次原序，变化组稳定前移；1=1.0、同刻等价；失败不推进基线；隐藏项也参与比较 |
| 18 | #21 | A/B、自适应开/关、多厂商、窄列高卡、极长头/单窗口、缺字、页码越界 | B列188/间8；高卡新全宽页、完整续页、后续恢复双列；无孤标题/裁字/遗漏；缺字[U+...]；物理14px可读 |
| 19 | #21 | 比较五种本地图标，顶部异常翻页、相同数据追读、EPD繁忙时翻页/状态变化 | 图标黑白、名称保留；异常不压正文；相同可见结果不刷新；不补积压帧，记录残影/可读性 |
| 20 | #22 | 增长响应/目录/偏好，Wi-Fi/BLE/TLS组合；分配故障/NVS满/操作边界断电 | 明确资源失败，旧快照/目录/基线/偏好一致；记录峰值/连续块/所有栈，恢复可成功；实机注入方式与host覆盖分开记录 |
| 21 | #23 | 以下至少24小时联合运行，穿插以上故障与真实控件/屏幕观察 | 无panic/WDT/栈溢出/持续泄漏；有完整时间线、资源采样、照片；不能只根据脚本输出判通过 |

## 资源预算与24小时记录

当前候选上限：响应含NUL 393216字节，JSON树及索引1572864字节，稳定目录/上传文档各393216字节；PSRAM保留262144字节，internal保留32768字节。旧模型、候选、智能比较临时空间、目录视图、上传/解码、QuickJS及TLS会并存。分配前检查剩余空间和最大块，真正分配失败仍返回错误。无账号条数限制，超字节预算整次失败。QuickJS使用固定PocketJS guest配置，启动后每分钟报告heap_used/heap_limit；字体和渲染缓冲开销包含在实际余量里。

这些是待实测的工程保护值，不是已验收容量。固定预算的最终依据必须补上TLS连接、解析、候选发布、BLE目录和偏好提交组合的最低余量、最大块、任务栈、峰值。当前按域上限及分配时free/largest检查，不是覆盖所有并存对象的统一峰值记账器；预检查和真正分配间也可能发生其他任务分配。host malloc故障模拟不能证明ESP32 internal/TLS分配路径或物理断电。

资源恢复还有已知软件缺口：固定PocketJS的Rust UI/render层存在分配耗尽后abort的路径（`engine/ui-cabi/src/lib.rs`），其`ui_qjs.h`也明确注明Rust分配耗尽不可恢复。当前Solid ErrorBoundary覆盖可抛出的UI更新异常，不能捕获native abort；持续OOM下恢复闭包或boundary重建也可能失败。因此第20项不能按现状记为全部通过，#22还需补齐该依赖的可恢复分配支持及真实组合故障验证。

网络候选仅在页面物化、render/present和协调器版本检查通过后推进目录与智能基线。若物理呈现结束后版本才失效，下一帧恢复旧页面，不能保证正在刷新的面板即时回滚。显示偏好独立切换，guest状态字段不包含在额度页面回滚闭包中。实际故障注入要分别观察模型、目录、基线、偏好、状态和物理屏幕，不能把一次合成JS异常恢复当作整个组合事务的证据。

关闭其他串口monitor，从ESP-IDF已加载的Python环境运行（需pyserial）：

```powershell
python -I tools/note4_endurance.py --port COM5 --hours 24 --output note4-build/acceptance/run-01.jsonl
python -I tools/note4_endurance.py --summarize note4-build/acceptance/run-01.jsonl
```

先自行创建忽略输出目录。采集器不烧录，不主动重置，独占串口；某些USB串口驱动打开时仍可能重置，须记录。文件独占创建，不覆盖旧证据。仅保存严格白名单资源/构建/状态/呈现记录及去正文的故障标记，丢弃任意SDK/网络日志。诊断不包含key/Cookie/密码/完整HTTP响应。采集开始前记录构建身份，必要时人工重启以采到build行。

24小时至少覆盖30秒常态、有限追读、翻页、反复设置保存/取消/退出、Wi-Fi断连/恢复、校时失败、401/403/证书、资源失败恢复。每个阶段记开始/结束、期望/实际、照片。`observed_24_hours`仅说明记录跨度，不能证明期间无采样缺口或屏幕正常；检查screen/worker/coordinator每分钟样本、复位次数及缺口。稳定阶段对比相同负载的first/last和最低水位，持续下降需调查，不能将正常高水位缓存误作泄漏。24小时结果不作为续航承诺。

## 票据状态

#12–#21 有可测试的软件路径；自动验证结果与两轴审查见 `docs/provisioning-validation.md`。#22 尚有PocketJS native分配耗尽不可恢复的软件缺口，目标机实测预算/组合故障也未完成；#23 的真实BLE/TLS/屏幕/24小时证据必须按本清单补齐。未获得证据的验收项保持待测，不能仅按代码存在关闭票，也不修改父票 #11。
