# Lody API 的“当前会话”与“会话状态”：MCP、深链接、移动端与云同步路径研究
研究日期：2026-10-25。主仓固定到 [LodyAI/Lody@cebc2e6](https://github.com/LodyAI/Lody/commit/cebc2e6171a1ef9f9d43da142e9eb79549ed2295)（提交时间 2026-10-25T13:28:24Z）；iOS 客户端固定到 [Innei/lody-ios@6a3bcff](https://github.com/Innei/lody-ios/commit/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34)（提交时间 2026-10-25T10:08:20Z）。下文所有 GitHub 行号链接都指向这两个提交，不使用 `main`。研究沿用 `docs/research/` 现有约定，把“源码/规范事实”“设计建议”“未验证”分开标注。

方法：先用 `gh api repos/<owner>/<repo>/commits/<sha>` 固定版本，再用 `gh api repos/<owner>/<repo>/tarball/<sha>` 下载快照到独立临时目录，仅做静态阅读；补充阅读本机 Lody 0.108.1 的运行时工具说明，作为协议佐证，不作为主线。`git clone` 在本机被 GitHub 凭据包装器拒绝（`remote helper 'https' aborted session`），因此改用 tarball；`gh api` 偶尔在 stderr 打印 `credential_unavailable`，但返回值可用。没有执行任何写操作、消息发送、归档、取消、创建、上传或分享；没有读取或记录 token、Cookie、API key、完整日志或会话正文。本文只记录字段、端点、枚举和结构。

## 1. 结论

1. **“当前会话”不是全局单例，而是调用上下文。** MCP 的当前会话来自每次请求携带的 `sessionId/workspaceId/machineId/workdir` 上下文；`sessionId` 传 `"current"` 或不传时才回落到该上下文，深链接中的显式 workspace 还必须与上下文一致。来源：[MCP 上下文](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L992-L1018)、[`resolveMcpSessionId`](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L1208-L1218)、[深链接规范](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/specs/deep-links.zh.md#L44-L50)。
2. **“会话状态”至少有三层，不能只看 `SessionMeta.status`。** 持久元数据给出 `idle/running/requestPermission/initializing`；ephemeral presence 给出 90 秒 TTL 的实时工作/查看状态；机器侧 `session/live-status` RPC 再给出 `idle/initializing/running/waiting/unknown`。MCP `lody_session_status_many` 把 presence、机器 RPC、durable dispatch pointer、历史与队列融合成 `executionState/phase/machine/observation/actions`。来源：[状态机](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/packages/shared/src/session-status-machine.ts#L9-L86)、[presence 常量](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/packages/shared/src/presence.ts#L18-L60)、[融合逻辑](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L1625-L1669)。
3. **手机不是“远程 MCP”，也不直连桌面局域网。** 官方移动端和第三方 iOS 客户端都走云端：Loro Streams 承载目录与会话文档，Machine RPC 承载发给目标机器的动作；直连本机只属于桌面/CLI 的 named pipe 与 loopback MCP HTTP。来源：[通讯架构](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/specs/communication-architecture.zh.md#L37-L55)、[iOS 流地址](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/packages/cloud-runtime/src/runtime/index.ts#L267-L286)、[iOS Machine RPC](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/packages/cloud-runtime/src/runtime/machine-rpc.ts#L10-L63)。
4. **推送不是状态 API。** APNs/OneSignal 只负责通知和 Live Activity 刷新；点击后仍要用本地 catalog 解析 workspace 与 session，catalog 未同步时进入等待。权限通知只打开会话，不会在通知里直接批准。来源：[iOS 推送设计](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/apps/mobile/PUSH_NOTIFICATIONS.md#L42-L47)、[路由解析](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/apps/mobile/src/features/notifications/routing.ts#L44-L90)。
5. **最小可靠路径是：`status_many(["current"])` 取快照，`session_list` 做筛选分页，`session_history` 读对话，Operation 只做偶发查询。** 连续观察用 CLI `session observe --follow --jsonl` 或移动端长轮询流，不要轮询 `operation_get`。来源：[status_many](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L1930-L2008)、[Operation 规则](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/README.md#L56-L85)。

## 2. 方法与证据边界

| 来源 | 固定版本 | 用途 | 可信度 |
| --- | --- | --- | --- |
| `LodyAI/Lody` | `cebc2e6171a1ef9f9d43da142e9eb79549ed2295` | 官方 CLI、MCP、共享 schema、云 API 合同、`specs/`、`site-docs/` | 权威；但云端实现不在公开仓 |
| `Innei/lody-ios` | `6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34` | 独立 iOS 客户端的 Swift/TS 实现、Loro 流、Machine RPC、推送与路由 | 佐证；第三方，非官方 |
| 官方移动端源码 | 无公开仓 | 只能依赖 `site-docs` 与第三方客户端推断 | 未验证 |

**源码事实：主仓没有官方移动端源码。** 固定提交下 `apps/` 只有 `cli/` 与 `electron/`，没有 `apps/mobile`；因此“官方 iOS/Android App 的内部实现”无法从公开仓证明，见 [apps 目录](https://github.com/LodyAI/Lody/tree/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps)。`Innei/lody-ios` 的 README 明确写明它是第三方、社区维护、与官方团队无关，见 [README](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/README.md#L28-L36)。因此第 5 节的移动端调用链只能证明“这个第三方客户端”的行为，不能直接等同于官方 App。

**源码事实：官方移动端能力有文档，但没有协议细节。** 官方文档只说明 iOS/Android 原生应用、iOS 实时活动、推送、进度监控、项目文件浏览、离线缓存，以及“实时文件编辑目前桌面端可用、移动端即将支持”，见 [移动端访问](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/site-docs/content/docs/zh/%28guides%29/mobile.mdx#L8-L63)。文档没有给出移动端获取“当前会话”的内部字段或 API。

**未能取证的部分。** 官方 App 的 Convex 查询名与参数、APNs payload 的完整 schema、Android 的 Live Activity/前台服务行为、移动端是否复用同一套 Loro Streams/Machine RPC、以及 E2EE 是否已在产品启用，均无法从公开仓确认。E2EE 相关 spec 仍标注 `Status: draft`，且基础模块明确“不启用加密工作区”，见 [E2EE README](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/specs/e2ee/README.md#L1-L9)、[E2EE 基础边界](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/specs/e2ee-foundations.zh.md#L8-L27)。

## 3. “当前会话”在协议中的表示与获取方式

### 3.1 MCP：隐式上下文 + `"current"` 哨兵

| 表示 | 位置/形式 | 语义 | 约束 |
| --- | --- | --- | --- |
| 隐式上下文 | `McpSessionContext`：`machineId/workspaceId/sessionId/localControlSocketPath/workdir` | 每个 MCP 请求的授权与路由边界 | HTTP 由 `AsyncLocalStorage` 注入；stdio 由环境变量兜底；两者不能混用 |
| 哨兵 | `sessionId: "current"` 或省略 | 解析为 `ctx.sessionId` | 只在当前上下文内有效，不能跨 workspace |
| 裸 ID | `[A-Za-z0-9_-]+` | 直接使用 | 不做跨安装搜索 |
| 新深链接 | `lody://session/<sessionId>?workspace=<workspaceId>` | 目标会话 + 目标 workspace | workspace 必须匹配；最多 8 KiB |
| 旧 URI | `session://<sessionId>` | 仅读取兼容 | 不注册 OS 协议，不重写历史 |

来源：[上下文接口](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L992-L1018)、[哨兵解析](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L1208-L1218)、[链接解析](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/packages/shared/src/session-link.ts#L19-L81)、[规范](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/specs/deep-links.zh.md#L12-L18)。

HTTP 传输的上下文不是 URL 参数，而是四个头：`Authorization: Bearer <token>`、`x-lody-mcp-session-id`、`x-lody-mcp-workspace-id`、`x-lody-mcp-machine-id`，外加 base64url 的 `x-lody-mcp-workdir-b64`。服务端只监听 `127.0.0.1`，每个请求单独建 server/transport，因此并发会话不会互相污染。来源：[头构造](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-http-protocol.ts#L16-L51)、[HTTP host 安全模型](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-http-host.ts#L26-L66)、[逐请求处理](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-http-host.ts#L289-L318)。

stdio 模式下上下文来自 `LODY_MCP_SESSION_ID`、`LODY_MCP_WORKSPACE_ID`、`LODY_MCP_MACHINE_ID`、`LODY_MCP_SOCKET_PATH`、`LODY_MCP_WORKDIR`；这是“每个会话一个进程”的兜底路径，不是 HTTP 的替代。来源：[环境兜底](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L1000-L1018)。

`session_list` 的 `openedBy`/`parent` 也支持 `"current"`：过滤时分别与 `ctx.sessionId` 比较；构造列表 cursor 指纹时同样把 `"current"` 归一化成实际 ID，避免切换会话后 cursor 被错误复用。来源：[过滤](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L1769-L1782)、[指纹](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L1714-L1735)。

### 3.2 深链接：会话 ID 与 workspace 一起编码

| 用途 | 地址 | 说明 |
| --- | --- | --- |
| 会话资源（所有版本） | `lody://session/<sessionId>?workspace=<workspaceId>` | 新链接统一使用；workspace 用稳定 ID |
| Stable 登录回调 | `ai.lody.stable://auth/callback#token=…` | 认证不走资源路由 |
| Nightly 登录回调 | `ai.lody.nightly://auth/callback#token=…` | 同上 |
| 安装专属会话入口 | `lody-oss://session/…`、`ai.lody.stable://session/…` | 仅定向打开；新链接仍生成 `lody://` |
| 旧读取 | `session://<sessionId>` | 仅应用内兼容 |

来源：[深链接规范地址表](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/specs/deep-links.zh.md#L10-L20)。

解析器最多接受 8 KiB，只允许 ASCII ID 和 `workspace`/`tab` 参数；拒绝凭据、端口、重复/未知参数、fragment、空 ID、路径遍历、空白、反斜杠和百分号编码。MCP 解析时如果链接带 workspace 且与当前上下文不同，会直接报“属于不同 workspace”；带 `tab` 的复合链接要求调用方改用子会话 ID。来源：[解析器](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/packages/shared/src/session-link.ts#L31-L66)、[MCP 限制](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/packages/shared/src/session-link.ts#L68-L81)、[规范校验](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/specs/deep-links.zh.md#L44-L50)。

链接只标识目的地，不授予访问权；workspace 不可用时不会回退到当前 workspace，而是显示错误；链接也不会自动解除归档。来源：[规范分发与访问](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/specs/deep-links.zh.md#L8-L8)、[失败语义](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/specs/deep-links.zh.md#L40-L42)。

### 3.3 CLI：`LODY_SESSION_ID` 与显式参数

CLI 的“当前会话”优先取显式参数，其次取 `LODY_SESSION_ID`。例如 `session status` 的 `[sessionId]` 参数在省略时回落到该环境变量；`session chat` 同样支持位置参数或环境变量；`session list --opened-by-current` 只读取 `LODY_SESSION_ID`。来源：[CLI 文档](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/site-docs/content/docs/zh/%28reference%29/%28settings-and-cli%29/cli.mdx#L320-L339)、[status 命令](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/commands/session.ts#L5469-L5486)、[list 命令](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/commands/session.ts#L5100-L5128)。Worktree setup/cleanup 脚本也会注入 `LODY_SESSION_ID` 与 `LODY_WORKSPACE_ID`，见 [环境变量表](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/site-docs/content/docs/zh/%28core-concepts%29/worktrees.mdx#L101-L111)。

**源码事实：没有“当前会话指针文件”。** 本地 daemon 通过 named pipe（Windows `\\.\pipe\<namespace>-control-<uid>`）或 Unix socket 通信，会话由调用参数/上下文决定，而不是读某个 `current-session` 文件；见 [本地 socket 规则](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/packages/shared/src/node/local-ipc.ts#L223-L258)。在固定版本中没有找到把当前会话写入固定文件的代码路径。

### 3.4 移动端：没有 `current` 哨兵，只有“前台会话/正在看的会话”

| 层 | 字段/键 | 生命周期 | 说明 |
| --- | --- | --- | --- |
| React 前台会话 | 模块内 `id`，`setForegroundSession` / `getForegroundSession` | 会话页面挂载时设置，卸载时清空 | 只影响 outbox 调度，不是服务端上下文 |
| 原生 Outbox | `inputs.foregroundSessionId` | 与 React 同步 | 前台会话不占后台发送槽，也不被后台自动派发 |
| DataRuntime | `sessionId` | `openSession(id)` 设置，`closeSession` 清空 | 当前订阅/观看的 session 文档 |
| Workspace 选择 | `workspace:<userId>` | 用户切换 workspace 时写入 | 不是 session，是 workspace 选择 |
| Catalog 缓存 | `catalog:<userId>:<workspaceId>` | 同步成功后写入 | 目录快照，不含当前会话指针 |

来源：[前台会话模块](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/apps/mobile/src/cloud/send/foregroundSession.ts#L1-L13)、[会话页设置](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/apps/mobile/src/features/sessions/useSessionRuntime.ts#L51-L60)、[卸载清理](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/apps/mobile/src/features/sessions/useSessionRuntime.ts#L111-L116)、[Outbox 前台](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/apps/mobile/modules/lody-kit/ios/Outbox/OutboxSession.swift#L87-L96)、[DataRuntime 字段](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/apps/mobile/modules/lody-kit/ios/Cloud/DataRuntime.swift#L9-L13)、[打开会话](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/apps/mobile/modules/lody-kit/ios/Cloud/DataRuntime.swift#L225-L233)、[workspace 选择](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/apps/mobile/modules/lody-kit/ios/Catalog/CatalogSession.swift#L67-L82)、[catalog 键](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/apps/mobile/modules/lody-kit/ios/Catalog/CatalogSession.swift#L89-L105)。

**设计建议：** 移动端不应引入全局 `current` 哨兵。前台可见会话、正在同步的会话、后台仍在工作的会话是三个集合；把三者压成一个“当前会话”会导致切换页面时误停后台任务。现有实现用 `active`、`sessions`、`reserved` 三个槽位区分，并限制最多 3 个后台工作会话，见 [会话状态槽位](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/packages/cloud-runtime/src/runtime/session.ts#L33-L73)、[LRU 限制](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/packages/cloud-runtime/src/runtime/session.ts#L207-L224)。

### 3.5 前置条件与 workspace 约束

| 场景 | 前置条件 | 失败表现 |
| --- | --- | --- |
| MCP 读状态 | 有效 CLI 凭据、workspace 访问权；默认先同步 workspace meta | 同步失败 `SYNC_UNAVAILABLE`（可重试）；catalog 首次同步未完成 `CATALOG_SYNCING`（可重试） |
| `session_list` 分页 | cursor 必须与过滤器、workspace、用户绑定 | 不一致 `CURSOR_INVALID` |
| 移动端前台同步 | Keychain 中有登录 token；已选 workspace；能取得 Loro Streams grant | 显示 `syncing`；grant 失败进 `offline`/`failed` |
| 移动端推送跳转 | 账号就绪、workspace 匹配、catalog 已同步 | `wrong_account`/`no_workspace`/`unknown_route` 丢弃；catalog 未完成则等待 |

来源：[MCP 同步读取](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L1827-L1836)、[状态批量断言](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L1930-L1938)、[批量上限常量](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L193-L200)、[逐项失败](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L1911-L1928)、[cursor 校验](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L1737-L1766)、[CATALOG_SYNCING](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/lib/resource-discovery.ts#L37-L45)、[SYNC_UNAVAILABLE](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/lib/command-runtime.ts#L78-L93)、[推送等待/丢弃](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/apps/mobile/src/features/notifications/routing.ts#L57-L90)。

## 4. “会话状态”的维度、字段与实时性

### 4.1 六套状态词汇，不能互相替代

| 来源 | 词汇 | 含义 | 是否实时 |
| --- | --- | --- | --- |
| `SessionMeta.status` | `idle`、`running`（可带 `activity/phase`）、`requestPermission`、`initializing`（可带 `stage/detail`） | durable 元数据；`running` 由心跳驱动 | 否，可能滞后 |
| MCP `execution.phase` | `idle`、`queued`、`initializing`、`running`、`waiting` | 历史 + 队列 + presence/RPC 融合后的执行阶段 | 调用时快照 |
| MCP `execution.executionState` | `idle`、`busy` | `phase !== idle` 即 busy | 调用时快照 |
| 机器 RPC `session/live-status` | `idle`、`initializing`、`running`、`waiting`、`unknown` | 目标机器当前内存态 | 10 秒超时的点查 |
| CLI `session status` | durable `status` 另加 `liveStatus: idle/initializing/running/waiting/unavailable/unknown` | durable 与机器点查并列返回 | 点查 |
| CLI `session observe` | `idle`、`pending`、`running`、`waiting`、`unknown` | 持久化证据投影，`metadata` 来源恒为 `unknown` | `--follow` 可连续 |
| iOS 视觉状态 | `live`、`attention`、`failed`、`idle`、`done`、`archived` | 对原始 status 字符串的 UI 映射 | 由流事件驱动 |

来源：[durable 状态定义](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/packages/shared/src/schema.ts#L114-L141)、[状态机](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/packages/shared/src/session-status-machine.ts#L9-L86)、[MCP phase](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L1541-L1568)、[机器 RPC 状态](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/packages/loro-streams-rpc/src/rpc.ts#L800-L824)、[CLI status 类型](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/commands/session.ts#L333-L361)、[observe 规范](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/specs/cli-session-observe.zh.md#L41-L46)、[iOS 映射](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/apps/mobile/src/features/sessions/status.ts#L3-L28)。

**关键差异：** durable `SessionMeta.status` 的心跳 TTL 是 180 秒，但代码注释明确说“在线 UI 应读 session presence，而不是 durable status”；presence 的 TTL 是 90 秒、心跳 30 秒。因此同一会话在“元数据仍写 running”和“presence 已消失”之间可以出现最长数十秒的不一致。来源：[元数据心跳](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/packages/shared/src/session-status-machine.ts#L82-L123)、[presence 常量](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/packages/shared/src/presence.ts#L18-L20)、[UI 读取建议](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/packages/shared/src/schema.ts#L1025-L1030)。

### 4.2 `lody_session_status_many` 的完整字段

输入只有 `sessionIds`，1–50 个唯一字符串；`"current"` 会被解析成上下文会话。输出 `items[]` 按输入顺序、逐项独立成功/失败。来源：[输入 schema](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L868-L875)、[工具描述](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L4737-L4752)。

| 字段 | 类型/枚举 | 来源 | 说明 |
| --- | --- | --- | --- |
| `sessionId` | string | 输入/解析后 | 无效或外部 ID 保留原始值 |
| `ok` / `error.code` | boolean / `SESSION_NOT_FOUND` 等 | 读取结果 | `ok=false` 时给 `error`；单条失败不终止批次 |
| `summary.id/title/createdAt` | string | `SessionMeta` | `title` 可省略 |
| `summary.lastActivityAt` | ISO string | `lastMessageAt ?? createdAt` | 不是 presence 时间 |
| `summary.archived` | boolean | `isArchived` | 归档仍可查 |
| `summary.ownerUserId/isMine` | string/boolean | `userId` 与调用者 | 用于“我的/团队”筛选 |
| `summary.machineId` | string | `SessionMeta` | 目标机器 |
| `summary.agentConfigId` | string? | `SessionMeta` | 输出不含 `agentRoleId/revision` |
| `summary.executionState` | `idle`/`busy` | 融合快照 | busy 可来自历史 active turn，即使 `live.working=false` |
| `summary.live.working` | boolean | presence/RPC/pointer | 是否“正在工作” |
| `summary.live.status` | `running`/`initializing`/`requestPermission`? | presence/RPC | 没有 idle |
| `summary.live.source` | `presence`/`rpc`/`pointer`/`none` | 决策来源 | 用于判断可信度 |
| `summary.live.observedAt` | ISO string? | 信号时间 | 仅 presence/RPC 有 |
| `summary.live.viewed` | true? | session-viewing presence | 是否有人在看 |
| `summary.openedBySessionId/openedByRootSessionId` | string? | `SessionMeta` | 直接打开者 / 子标签的根 |
| `summary.parentSessionId` | string? | `SessionMeta` | 父子关系 |
| `summary.executionContext` | `{kind:'chat'}`/`{kind:'github',repo}`/`{kind:'local',projectId}` | `project` | 工作上下文 |
| `summary.pullRequests[]` | `{url,state:'open'|'closed'|'merged',draft}`，最多 10 | `pullRequests` | `draft` 归一成 `open` |
| `execution.phase` | 上述五值 | 融合 | 比 `executionState` 细 |
| `execution.activeTurnId` | string? | 历史目录 | 当前 assistant turn |
| `execution.queuedTurnCount` | number | 消息队列/激活指针 | 排队数 |
| `execution.liveStatus` | `running`/`initializing`/`requestPermission`? | `live.status` | 与 `summary.live.status` 同源 |
| `execution.liveSource` | 上述四值 | `live.source` | 同上 |
| `machine.state` | `online`/`offline` | 机器 RPC 可达性 | 没有 `unknown` |
| `observation.quality` | `fresh`/`stale`/`unavailable` | RPC 成功/在线但失败/离线 | 三态新鲜度 |
| `observation.observedAt` | ISO string | RPC 时间或查询时间 | 不可用时是 `now`，见第 8 节 |
| `actions.chat` | `allowed`/`forbidden`/`temporarily_blocked` | 归档/机器状态 | 附 `reason` 错误对象 |

来源：[融合与输出](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L1930-L2008)、[summary 构造](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L1671-L1712)、[phase 计算](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L1548-L1568)。

### 4.3 `lody_session_list` 的筛选与分页

| 输入 | 取值 | 作用 |
| --- | --- | --- |
| `archive` | `active`（默认）/`archived`/`any` | 归档筛选 |
| `createdBy` | `me` | 只看自己创建 |
| `openedBy` / `parent` | ID 或 `current` | 打开者/父会话 |
| `executionState` | `idle`/`busy` | 融合后过滤 |
| `executionContext` | `chat`/`github{repo}`/`local{projectId}` | 工作上下文 |
| `pullRequest` | `exists`、`state`、`draft` | PR 状态；`exists:false` 时不能再带 state/draft |
| `updatedAfter` | ISO datetime | 严格大于 |
| `query` | ≤200 字符 | 标题/ID 搜索 |
| `machineId`/`agentConfigId`/`agentRoleId` | ID | 机器/配置/角色来源过滤 |
| `limit` | 默认 20，最大 100 | 页大小 |
| `cursor` | 不透明字符串 | 与过滤器绑定的 keyset |

来源：[输入 schema](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L883-L921)、[分页实现](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L1827-L1908)、[默认值](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L196-L200)。

**注意：** `agentRoleId` 可用于过滤，但 `sessionSummaryForMcp` 不输出它；要显示 Role 需另查 catalog 或详情。来源：[筛选](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L1732-L1733)、[summary 字段](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L1678-L1712)。

### 4.4 移动端 catalog 与会话 envelope 的字段

| 对象 | 字段 | 说明 |
| --- | --- | --- |
| `Session`（catalog） | `id/machineId/title`、`openedBySessionId/openedByRootSessionId/parentSessionId`、`status:string`、`archived/pinned`、`projectId/createdAt`、`latestUserMsgId`、`lastMessageAt/lastReadAt/lastRunningSeen/awaitingUserSince`、`branchName`、`pullRequests`、`diff{add,del}`、`lastModel`、`cliType/agentType/resume` | `status` 是原始字符串，不强制四值枚举 |
| `Catalog` | `projects[]`、`sessions[]`、`machineIds[]`、`machineNames`、`machineSimulators`、`agentUsage` | 目录快照 |
| `Connection` | `state: live/syncing/offline`、`machines:number`、`syncedAt?` | 传输健康，不是会话状态 |
| `CatalogSnapshot` | `userId/workspaceId/key`、`loading/connected`、`syncedAt`、`catalogRevision`、`connection`、`presence`、`pendingSessions` | 原生发给 RN 的快照 |
| `MachinePresence` | `state: live/unknown`、`onlineMachineIds[]` | 只区分 live/unknown |
| 会话 envelope | `v:1`、`status`、`reason?`、`revision`、`billableTurnCount`、`awaitingUserSince?`、`preview?`、`composer?`、`entries[]` | 单会话文档投影 |
| `entries[]` | `id/role/status/finished`、`userTurnId`、`executionId/executionFinished`、`steerCount`、`delivery`、`timestamp/startedAt/endedAt`、`modelInfo`、`items[]` | 逐轮执行证据 |

来源：[catalog 模型](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/packages/cloud-runtime/src/models/catalog.ts#L1-L47)、[catalog 投影](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/packages/cloud-runtime/src/protocol/catalog.ts#L146-L189)、[Connection](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/packages/cloud-runtime/src/models/catalog.ts#L42-L47)、[CatalogSnapshot](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/apps/mobile/modules/lody-kit/ios/Catalog/CatalogSnapshot.swift#L11-L41)、[presence 合同](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/packages/cloud-runtime/src/contract/sync.ts#L3-L8)、[envelope](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/packages/cloud-runtime/src/runtime/project.ts#L102-L117)。

**移动端的“working”不是读 catalog.status。** 运行时会扫描本地 CRDT：消息队列非空、或存在未完成的 assistant entry、或最新 user turn 仍是 `pending/seen/processing/pending_apply`，就判定 working；否则 false。catalog.status 只是 durable 摘要。来源：[isWorking](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/packages/cloud-runtime/src/runtime/session.ts#L129-L155)、[status 投影](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/packages/cloud-runtime/src/protocol/catalog.ts#L170-L180)。

### 4.5 实时性对照

| 机制 | 粒度 | 推送/轮询 | 延迟与限制 |
| --- | --- | --- | --- |
| MCP `status_many` | 会话级快照 | 请求/响应 | 每次调用；presence TTL 90s；机器 RPC 10s 超时；批量 50 |
| CLI `session observe --follow` | 事件级 | 持久化流 | 4 个并发历史房间；输出队列 1 MiB；不逐 token |
| presence 通道 | 机器/会话/查看者 | ephemeral 发布 | 心跳 30s，TTL 90s；只保留最新值，可丢弃 |
| Loro Streams（iOS） | 文档增量 | long-poll | 8 MiB 目录上限；100 页上限；断线退避 2–30s |
| APNs/OneSignal | 通知 | 系统推送 | 投递不保证；点击后还要 catalog 解析 |
来源：[status_many](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L1930-L2008)、[observe 成本](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/specs/cli-session-observe.zh.md#L98-L120)、[presence 预算](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/specs/loro-ephemeral-presence-channel.zh.md#L12-L47)、[iOS 长轮询](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/packages/cloud-runtime/src/runtime/index.ts#L324-L360)、[定时任务](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/site-docs/content/docs/zh/%28guides%29/scheduled-tasks.mdx#L14-L20)。

## 5. 移动端 / Remote Control 路径

### 5.1 传输拓扑

| 链路 | 客户端 → 服务 | 鉴权 | 用途 |
| --- | --- | --- | --- |
| 桌面本地 MCP | `http://127.0.0.1:<port>/mcp` | Bearer + 四个 `x-lody-mcp-*` 头 | Agent 调用会话工具 |
| 桌面本地控制 | Windows named pipe / Unix socket | 本机用户 + `x-lody-local-control` | UI ↔ CLI/daemon |
| 云目录/会话 | `https://<gateway>/ds/lody/<workspace>:meta`、`<workspace>:mf:<machine>`、`<workspace>:s:<session>` | Loro Streams grant | 目录、机器、会话文档 |
| 云 Machine RPC | `<workspace>:rpc:req:<machine>` / `<workspace>:rpc:res:<machine>:<uuid>` | 同一 grant + 可选加密信封 | 发消息、权限回答、文件/预览操作 |
| 云平台 API | Convex / `api.lody.ai` / `backend.lody.ai` | 账号 token / CLI token | 组织、权限、通知、grant |
| 推送 | APNs / OneSignal | 系统推送 token | 通知与 Live Activity |

来源：[本地 MCP host](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-http-host.ts#L26-L66)、[本地 socket](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/packages/shared/src/node/local-ipc.ts#L223-L258)、[iOS 流地址](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/packages/cloud-runtime/src/runtime/index.ts#L267-L286)、[Machine RPC 流](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/packages/cloud-runtime/src/runtime/machine-rpc.ts#L10-L63)、[官方 RPC 流 ID](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/packages/loro-streams-rpc/src/rpc.ts#L220-L230)。

**源码事实：移动端没有 MCP 服务器。** 它不调用 `lody_session_status_many`，也不持有 MCP HTTP 端口；它通过 `runtimeCalls` 暴露 `sendTurn`、`controlTurn`、`respondPermission`、`archiveSession`、`readFile` 等方法，由隐藏的 WKWebView 运行时执行。来源：[runtimeCalls](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/packages/cloud-runtime/src/contract/methods.ts#L118-L273)、[DataRuntime](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/apps/mobile/modules/lody-kit/ios/Cloud/DataRuntime.swift#L9-L33)。

### 5.2 iOS 精确调用链

1. RN 页面进入 `useSessionRuntime`，`watchSession(sessionId)` 经 `LodyKitModule` 调 `DataRuntime.openSession`；Swift 打开隐藏 WKWebView，握手 `hello`，再 `start(workspaceId,userId,active,knownRepos)` 与 `restoreSessions(ids,current,reserved)`，其中 `current` 就是 `sessionId`。来源：[RN 订阅](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/apps/mobile/src/features/sessions/useSessionRuntime.ts#L45-L60)、[openSession](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/apps/mobile/modules/lody-kit/ios/Cloud/DataRuntime.swift#L225-L233)、[握手](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/apps/mobile/modules/lody-kit/ios/Cloud/DataRuntime.swift#L242-L282)。
3. 云端运行时 `watch('meta')` 拼出 `<workspace>:meta`，用 grant 建 `StreamsClient`，`bootstrap` 读快照，`readOnce({live:'long-poll'})` 增量读；每轮 `projectRows` 生成 catalog，变更时 emit `catalog`，并 emit `synced`。来源：[watch](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/packages/cloud-runtime/src/runtime/index.ts#L267-L360)、[projectRows](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/packages/cloud-runtime/src/protocol/catalog.ts#L102-L189)。
4. 会话页面额外订阅 `<workspace>:s:<sessionId>`；文档增量进入本地 LoroDoc，`projectSession` 生成 envelope，`isWorking` 推导 working，`scheduleEmit` 合并 200ms/1000ms 内的变化。来源：[session 流](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/packages/cloud-runtime/src/runtime/session.ts#L321-L420)、[projectSession](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/packages/cloud-runtime/src/runtime/project.ts#L494-L645)、[合并](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/packages/cloud-runtime/src/runtime/session.ts#L156-L205)。
5. 发送一轮时，先 `appendUserTurn` 写本地 history（status `pending`），再通过 Machine RPC 的 `session/chat` 类方法发给目标机器；失败不自动重放，收到 ACK 后刷新。来源：[appendUserTurn](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/packages/cloud-runtime/src/runtime/session.ts#L464-L499)、[sendTurn](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/packages/cloud-runtime/src/runtime/session.ts#L514-L600)、[Machine RPC](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/packages/cloud-runtime/src/runtime/machine-rpc.ts#L10-L63)。

### 5.3 grant 与认证

iOS 原生从 Keychain 读取登录 token，向 `POST https://backend.lody.ai/api/loro-streams/token` 发送 `Authorization: Bearer <token>` 和 `{workspaceId}`；响应必须是 200，包含非空 `token`、`https` 且无 userinfo 的 `gatewayBaseUrl`、正数 `expiresIn`，可选 `shardHostSuffix`。失败时 `grant_failed` 标记为可重试。来源：[fetchGrant](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/apps/mobile/modules/lody-kit/ios/Cloud/DataRuntime+HostCalls.swift#L78-L114)、[grant 调用](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/packages/cloud-runtime/src/contract/methods.ts#L275-L292)。

### 5.4 推送、Live Activity 与深链接

**源码事实：** 官方云 API 的 `notifications.notifySessionCompleted`、`notifyPermissionRequested`、`syncLiveActivitySummary` 接收 `workspaceId`、`workspaceSlug`、`sessionId`、`sessionTitle`、`outcome`/`presentation` 等字段，并由 CLI token 鉴权；`syncLiveActivitySummary` 的条目状态是 `permission/question/running/unread`，活动 ID 形如 `lody-conversations:v5:<workspaceId>:<userId>`。来源：[通知 API](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/packages/cloud-api/src/index.ts#L959-L1045)、[Live Activity 汇总](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/packages/shared/src/live-activity-summary.ts#L52-L73)。

**第三方 iOS 的路由与官方深链接规范不一致，这是生态分歧，不是官方仓内部冲突。** iOS 通知使用 `data.route`，格式 `/<workspaceSlug>/sessions/<sessionId>`；Live Activity 使用 `lody:///{workspaceSlug}/sessions/{sessionId}`。而官方规范的新资源链接是 `lody://session/<sessionId>?workspace=<workspaceId>`，workspace 用稳定 ID 而不是 slug。iOS 的 `routeFromDeepLink` 会把 `lody://` 后面的路径取出来，再匹配自己的 route；官方解析器则只接受 `session` host 和 `workspace` 参数。两者不能互相解析。来源：[iOS 路由](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/apps/mobile/src/features/notifications/routing.ts#L5-L31)、[Live Activity 链接](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/apps/mobile/PUSH_NOTIFICATIONS.md#L55-L63)、[官方链接规范](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/specs/deep-links.zh.md#L12-L18)、[官方解析器](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/packages/shared/src/session-link.ts#L32-L66)。

**推送点击的处理顺序：** 先等认证与导航就绪，再校验接收者与 workspace，选择 workspace，用本地 catalog 找真实 Session；catalog 未同步时等待，同步完成仍找不到才报不可用。前台且正在看该会话时不显示通知；权限通知只打开会话，不在通知里执行批准。来源：[PUSH_NOTIFICATIONS](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/apps/mobile/PUSH_NOTIFICATIONS.md#L42-L47)、[解析逻辑](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/apps/mobile/src/features/notifications/routing.ts#L44-L90)、[深链接缓冲](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/apps/mobile/src/features/notifications/deepLinks.ts#L8-L25)。

### 5.5 移动端专属或被限制的能力

| 能力 | 状态 | 证据 |
| --- | --- | --- |
| iOS 实时活动/灵动岛 | 有 | 官方文档；iOS 第三方实现 |
| 可操作权限提醒 | iOS 有；点击只打开会话 | 官方文档 + iOS 推送设计 |
| Android Live Activity | 未验证；官方文档只写 iOS | 官方移动端文档 |
| 实时文件编辑 | 桌面端有，移动端“即将支持” | 官方文档 |
| 前台/后台 | 前台有终端输出、文件变化、diff、图片；后台停止 presence，运行时可能被销毁，改由推送 | 官方文档 + iOS DataRuntime |
| MCP 工具 / Durable Operation API | 无；iOS 只用 runtimeCalls/Machine RPC 与本地 outbox | iOS DataRuntime/Outbox |
| 跨 workspace 查询 | 单 workspace；切换会停止旧 watch | iOS CatalogSession |

来源：[官方移动端功能](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/site-docs/content/docs/zh/%28guides%29/mobile.mdx#L25-L63)、[iOS 后台](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/apps/mobile/modules/lody-kit/ios/Cloud/DataRuntime.swift#L57-L84)、[presence unknown](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/apps/mobile/modules/lody-kit/ios/Cloud/DataRuntime.swift#L120-L123)、[outbox](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/apps/mobile/modules/lody-kit/ios/Outbox/OutboxSession.swift#L78-L96)、[workspace 切换](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/apps/mobile/modules/lody-kit/ios/Catalog/CatalogSession.swift#L89-L105)。

## 6. 轮询、推送与事件流

| 机制 | 适合 | 不适合 | 关键限制 |
| --- | --- | --- | --- |
| MCP 请求/响应 | 一次性快照、筛选、读历史 | 持续观察 | 无订阅；每次调用都重新读 |
| CLI `observe --follow --jsonl` | 脚本/守护进程持续观察 | 需要逐 token 流式 | 只读；不确认已读、不回答权限；最多 4 个历史房间 |
| presence 通道 | 在线/工作中的低延迟提示 | 传输正文或历史 | 可丢弃、仅最新；TTL 90s |
| Loro Streams long-poll | 移动端前台目录/会话 | 后台常驻 | 连接超时 15s，poll 35s；8 MiB/100 页上限 |
| APNs/OneSignal | 后台提醒、Live Activity | 权威状态 | 投递不保证；点击后仍需 catalog |
| 定时任务 | 按时创建会话 | 观察状态 | 本机执行；睡眠/CLI 停止时不跑 |
| 宿主 `Monitor` / `notify_when_idle` | 本地条件等待、宿主 Agent 通知 | Lody 协议 | 均不是 Lody API；后者在固定仓中未找到 |

来源：[Operation 轮询规则](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/README.md#L67-L85)、[observe 边界](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/specs/cli-session-observe.zh.md#L82-L120)、[presence 不可靠性](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/specs/loro-ephemeral-presence-channel.zh.md#L12-L30)、[iOS 超时](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/packages/cloud-runtime/src/runtime/index.ts#L281-L286)、[定时任务](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/site-docs/content/docs/zh/%28guides%29/scheduled-tasks.mdx#L14-L20)。

**设计建议（移动端）：** 前台用 catalog + 当前会话两条 long-poll 流；进入后台立即停止 presence，改由推送提醒；恢复前台先 bootstrap/readOnce 补齐 offset，再显示 `syncing`，不要用定时器高频轮询。第三方 iOS 已经这样实现：后台 `setActive(false)` 停止 presence，恢复时 `setActive(true)` 并从保存的 offset 续读。来源：[iOS 生命周期](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/apps/mobile/modules/lody-kit/ios/Cloud/DataRuntime.swift#L57-L84)、[runtime setActive](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/packages/cloud-runtime/src/runtime/handlers/lifecycle.ts#L23-L38)、[readOnce](https://github.com/Innei/lody-ios/blob/6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34/packages/cloud-runtime/src/runtime/index.ts#L338-L360)。

**设计建议（只有 MCP 的客户端）：** MCP 没有订阅能力，若必须轮询，优先对一个小集合调用 `status_many`，低频、带上限、以 `observation.observation.observedAt` 判断数据年龄；不要用 `session_list` + `session_history` 代替状态查询，也不要在循环里调 `operation_get`。来源：[MCP 无订阅](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L1930-L2008)、[Operation 规则](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L4582-L4599)。

## 7. 最小可行调用序列

```text
status := lody_session_status_many({ sessionIds: ["current"] })
snap := status.items[0]   // summary / execution / machine / observation / actions

page := lody_session_list({ archive: "active", limit: 20 })
while page.nextCursor: page = lody_session_list({ archive:"active", limit:20, cursor:page.nextCursor })

page := lody_session_history({ sessionId: "current", limit: 10 })
while page.nextCursor: page = lody_session_history({ sessionId:"current", limit:10, cursor:page.nextCursor })

ops := lody_operation_list({ state: "active", limit: 20 })  // 偶发查询，不轮询
```

| 步骤 | 调用 | 必填 | 关键输出 | 分页/限制 | 错误处理 |
| --- | --- | --- | --- | --- | --- |
| 0 | 建立上下文 | 无 | `ctx` | HTTP 头或 stdio 环境 | 缺头 `400`；缺 env 启动失败 |
| 1 | `lody_session_status_many` | `sessionIds` | summary/execution/machine/observation/actions | 1–50 唯一；schema 写 1000 但运行时断言 50 | 单项 `SESSION_NOT_FOUND`；超限报错 |
| 2 | `lody_session_list` | 无 | 列表摘要 | 默认 20，最大 100；cursor 绑定过滤器 | `CURSOR_INVALID` 时丢掉 cursor 重来 |
| 3 | `lody_session_history` | `sessionId` 可省略 | 可见对话页 | 默认 10，最大 50，128 KiB | `SESSION_NOT_FOUND`；cursor 是原始位置 |
| 4 | `lody_operation_list` | 无 | Operation 摘要 | 默认 20，最大 100；keyset | `CURSOR_INVALID`；只偶发查 |
| 5 | `lody_operation_get` | `operationId` | 单个 Operation 快照 | 无分页 | 仅用于一次确认，不轮询 |
| 6 | `lody_operation_batch_get` | `batchId` | 固定批次结果 | 默认 20，最大 50；128 KiB | 大项用 `member-json` 分片 |

来源：[status_many 实现](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L1930-L2008)、[list schema](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L883-L921)、[history schema](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L923-L944)、[operation list](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/orchestration/operation-store.ts#L39-L45)、[operation batch](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L4521-L4579)。

### 7.1 错误码与动作

| 错误 | 可重试 | 含义 | 客户端动作 |
| --- | --- | --- | --- |
| `CATALOG_SYNCING` | 是 | catalog 首次同步未完成 | 退避后重试同一请求 |
| `SYNC_UNAVAILABLE` | 是 | workspace 同步暂不可用 | 至少 5 秒间隔、最多 3 次；复用同一 `operationId`，不要新造 ID |
| `CHAIN_DEPTH_EXCEEDED` | 否 | 委派链达到 16 跳上限 | 停止委派，把剩余工作报告给用户 |
| `REVERSE_CHAT_FORBIDDEN` | 否 | 本轮由目标会话发起，不能反向再发 | 在当前会话给出结果，不要重试 |
| `COLLABORATION_STOPPED` | 否 | 用户停止了会话树 | 只有用户能恢复 |
| `SESSION_NOT_FOUND` | 否 | 单条会话不存在/无权 | 批量中只标记该项 |
| `SESSION_ARCHIVED` | 否 | 会话已归档 | `actions.chat=forbidden` |
| `MACHINE_OFFLINE` | 是 | 目标机器离线 | `actions.chat=temporarily_blocked`，等机器恢复 |
| `CURSOR_INVALID` | 否 | cursor 与过滤器/作用域不匹配 | 丢弃 cursor，从第一页重来 |
| `BATCH_NOT_FOUND` | 否 | 批次不存在或不属于当前会话/用户 | 不要重新提交任务 |

来源：[CATALOG_SYNCING](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/lib/resource-discovery.ts#L37-L45)、[SYNC_UNAVAILABLE 与 3 次预算](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/README.md#L67-L85)、[链深](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L2278-L2292)、[反向](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L2391-L2396)、[归档/离线动作](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L1991-L2001)、[cursor](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L1737-L1766)、[批次](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L4542-L4549)。

## 8. 冲突、歧义与实现注意点

1. **`machine.state` 丢掉了“unknown”。** presence 规范要求在线状态保留三态（在线/离线/未知），且“未知”应继续执行而不是武断阻塞；但 `status_many` 只输出 `online/offline`，因为 `online` 来自机器 RPC 是否可达。`observation.quality` 用 `fresh/stale/unavailable` 部分恢复了区分，但客户端不能从 `machine.state` 单独判断“没查到”还是“确定离线”。来源：[presence 三态要求](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/specs/loro-ephemeral-presence-channel.zh.md#L49-L58)、[MCP 机器状态](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L1982-L1990)、[MCP 三态说明](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/AGENTS.md#L89-L93)。
2. **`observation.observedAt` 在不可用时是查询时间。** 代码用 `live?.observedAt ?? getServerNow()`；当机器离线/读不到状态时，`observedAt` 仍是当前时间，容易被误读成“刚刚观测到”。客户端应优先看 `quality`，不要把 `observedAt` 当作证据时间。来源：[输出](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L1983-L1990)、[now](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L1947-L1949)。
3. **`executionState=busy` 不等于 `live.working=true`。** `phase` 先看 `activeTurnId`，有未完成 assistant turn 就是 `running`，即使 presence 已消失；这能避免崩溃后误报 idle，但会让“忙但没心跳”的会话持续显示 busy。来源：[phase](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L1548-L1568)、[summary](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L1688-L1696)。
4. **schema 与运行时、文档不一致。** `SessionStatusManyToolInputSchema` 写 `.max(1_000)`，但 handler 先断言 50；`"current"` 被 `resolveMcpSessionId` 支持，却没写进该 schema 的描述。客户端应以 50 和 `"current"` 为准。来源：[schema](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L868-L875)、[常量](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L193-L200)、[断言](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L1930-L1931)、[解析](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/apps/cli/src/mcp/lody-mcp-server.ts#L1208-L1218)。
## 9. 未验证与后续取证建议

**未验证清单：**

- 官方 iOS/Android App 是否复用本文的 Loro Streams + Machine RPC 路径，以及其“当前会话”的内部表示；公开仓没有移动端源码，本文只能证明第三方 iOS 的 foreground/watched 会话。
- Convex `notifications.*` 的实际 payload、OneSignal data 字段、APNs 折叠/优先级，以及 Android 的后台策略、推送与 Live Activity/前台服务；公开仓只有客户端合同。
- E2EE 是否在真实工作区启用；spec 仍是 draft，不能把加密当作现状。
- `status_many.machine.state` 是否会补 `unknown`（当前只有两态），以及显式 `"current"` 是否写进正式文档。
- 移动端是否提供任何 Operation 等价物；第三方 iOS 没有，官方未知。
- `observation.observedAt` 在不可用时返回 now 是否是刻意设计，还是实现缺陷。
- 官方是否把移动端路径称为“Remote Control”未验证；公开文档只在桌面端“纯远程控制端”语境使用该词，见 [quickstart](https://github.com/LodyAI/Lody/blob/cebc2e6171a1ef9f9d43da142e9eb79549ed2295/site-docs/content/docs/zh/%28getting-started%29/quickstart.mdx#L64-L70)。

**后续取证命令：**

```bash
# 1. 固定版本复查官方规范
gh api repos/LodyAI/Lody/contents/specs/cli-session-observe.zh.md?ref=cebc2e6171a1ef9f9d43da142e9eb79549ed2295 --jq .content | base64 -d
gh api repos/LodyAI/Lody/contents/specs/deep-links.zh.md?ref=cebc2e6171a1ef9f9d43da142e9eb79549ed2295 --jq .content | base64 -d

# 2. 复查 MCP 当前会话与 status_many
gh api repos/LodyAI/Lody/contents/apps/cli/src/mcp/lody-mcp-server.ts?ref=cebc2e6171a1ef9f9d43da142e9eb79549ed2295 --jq .content | base64 -d | rg -n "resolveMcpSessionId|MAX_MCP_STATUS_BATCH_SIZE|buildSessionStatusMany|observation"

# 3. 复查第三方 iOS 的推送/路由/前台会话
gh api repos/Innei/lody-ios/contents/apps/mobile/PUSH_NOTIFICATIONS.md?ref=6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34 --jq .content | base64 -d
gh api repos/Innei/lody-ios/contents/apps/mobile/src/features/notifications/routing.ts?ref=6a3bcffa1a52242e16e1a4c3c16a478cd07f8c34 --jq .content | base64 -d

# 4. 若本机已登录，做只读快照（不要发消息）
lody session status <sessionId> --json
lody session observe <sessionId> --json
lody session observe <sessionId> --jsonl --follow   # 连续观察，Ctrl-C 退出

# 5. 若在真机上验证深链接，只打开自己的会话
# iOS Simulator（已安装对应 App 时）：
xcrun simctl openurl booted "lody:///__WORKSPACE_SLUG__/sessions/__SESSION_ID__"
```

**建议向维护者确认的问题：** `status_many.machine.state` 是否应增加 `unknown`；`observation.observedAt` 在 `unavailable` 时的语义；`"current"` 是否应写进所有会话工具 schema；官方移动端是否会把 Loro Streams 的 `session/live-status` 作为权威工作状态，还是继续读 durable `SessionStatus`。这些问题直接决定客户端应该把哪一层当作“状态真相”。
