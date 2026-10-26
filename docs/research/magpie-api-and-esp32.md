# Magpie 登录、Cookie、路由状态与账号额度：NOTE4 / ESP32-S3 接入研究

研究日期：2026-10-25。上游为公开仓库 [yetone/magpie](https://github.com/yetone/magpie)，本报告固定到 **`7d67910f74889f7860e53a15fdf45bf1587b1555`**（提交时间 2026-10-24T19:11:41Z，[提交记录](https://github.com/yetone/magpie/commit/7d67910f74889f7860e53a15fdf45bf1587b1555)）。所有 GitHub 行号链接均指向该版本；本地引用指当前含未提交改动的工作区，不能当作已发布能力。笔记沿用 `docs/research/` 现有约定。

方法：通过 `gh api` 查询公开提交与源码树，下载该提交到单独新建的临时目录，仅静态阅读实现、路由注册与测试；未执行上游脚本，未读取用户凭据，未访问私人实例，未登录厂商或实际调用额度 API。下文把“源码事实”“设计建议”“未验证”明确区分。研究不修改固件、配置、业务网页，不提交、不烧录。

## 1. 接入结论与权限边界

**建议先做额度快照终端，再按需要增加路由状态。** Magpie 已提供网关聚合 API；让 Magpie 主机承担各厂商 OAuth、刷新、Cookie、CLI 与插件工作，NOTE4 只用一个受限网关密钥读标准化结果。管理 Web 访问密钥、网关密钥、厂商账号凭据是三个独立层次，不能互换。Web 的 `/api/...` 还具有管理账号、设置等写能力，给单片机保存其 Cookie 就是在保存管理权限。网关的命名密钥具有模型、账号、预算和 `quotas` 授权字段，不等于专用只读凭据。对应源码见 [Web guard](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/web.go#L195-L259)、[网关密钥模型](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/access/access.go#L111-L147)、[网关远程认证](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gateway/lan.go#L246-L283)。

路由页的真实输入是 **HTTP JSON 长轮询**，不是 SSE 或 WebSocket。远端设备可经已认证的 `magpie web` 读取 `/api/gateway/trace`；看似适合 Bearer 的 `/v1/magpie/trace` 实际只允许本机，远端 NOTE4 会被拒绝。因此两种可选实现是：

1. **最小额度版**：HTTPS + 网关 Bearer + 额度聚合 GET；无需 Cookie、无需厂商凭据。
2. **额度与路由版**：另经 Web Cookie 读取 trace，承担较大的管理权限；或在 Magpie 同机实现一个受限的只读适配服务，过滤后提供快照。后者是设计建议，当前仓库没有现成适配端点。

来源：[GUI trace handler](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/trace.go#L335-L387)、[网关 trace 本机限制](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gateway/trace.go#L694-L715)。具体额度端点、scope 与成本见第 4 节。

## 2. 身份、登录、Cookie 与退出

### 2.1 `magpie web` 管理界面登录

它没有用户名/密码 JSON 登录，也不是登录后颁发 JWT。进程有一个 Web key：默认每次启动生成随机 16 字节并编码为 32 字符十六进制；`MAGPIE_WEB_KEY` 可固定它，至少 16 字符，只允许字母、数字、`-._~`。自动更新重启可继承当前 run key。来源：[生成与校验](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/web.go#L146-L193)、[重启传递](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/web.go#L129-L143)。

| 方法与路径 | 请求 | 成功响应 | 失败与注意事项 |
| --- | --- | --- | --- |
| `GET /?k=<WEB_KEY>`，也可在其他路径带 `k` | URL query `k` | `303 See Other`；删除 query 中的 `k` 后跳转；`Set-Cookie` | `k` 错误直接 `401`；即使带正确旧 Cookie，错误 `k` 仍优先失败 |
| `POST /_magpie/sign-in` | `application/x-www-form-urlencoded`，`key=<WEB_KEY>&back=/...`；最大 body 4096B | 同样 `303` + `Set-Cookie`；`back` 必须是本机绝对路径，否则 `/` | 错 key 返回 `401` HTML；不是 `{token:...}` JSON |
| 后续 `GET /api/...` 等 | `Cookie: magpie_web_<LISTEN_PORT>=<WEB_KEY>` | 交给对应 handler | 无 Cookie/值错误时，普通 API 请求是 `401` 文本；接受 HTML 的 GET 得到 `401` 登录页 |

来源：[完整 guard 与表单路径](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/web.go#L200-L286)。

Cookie 值就是原始 Web key，服务器每次与当前 key 常量时间比较；不存在额外 session id 数据库，也没有另发 refresh token。Cookie 属性在该版本为 `Path=/; HttpOnly; SameSite=Lax; Priority=High`，未设置 `Domain`，**代码未设置 `Secure`**。固定 key 时设置 `Max-Age=34560000`（400 天）；随机运行 key 时 `MaxAge=0`，序列化不带 Max-Age，为浏览器会话 Cookie。400 天是客户端保存期，不代表服务器端 key 自动过期：只要 key 不变且客户端继续发送有效值，guard 没有时间校验。`HttpOnly` 不是“仅 HTTPS”，因此设备侧仍应强制使用 HTTPS。来源：[cookie 年龄](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/web.go#L150-L153)、[实际 Set-Cookie](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/web.go#L200-L208)、[RFC 6265 §4.1.2、§5.4](https://www.rfc-editor.org/rfc/rfc6265#section-4.1.2)。

Cookie 名中的端口来自 `StartWeb` 实例实际监听端口，通过 `webHandler(port,...)` 生成；不是反向代理外部 HTTPS 443 端口。公网 URL 支持反向代理路径的链接生成，但 `/_magpie/sign-in` 和 `/api/...` 注册仍是根路径；有 base path 的反向代理必须正确重写，不能据 `MAGPIE_PUBLIC_URL` 推断任意前缀都自动工作。设备应从实际 `Set-Cookie` 获取 cookie 名，配置 Web base URL 与 gateway base URL 时分开处理。部署路径、重定向和代理注入 Cookie flags 未实测。来源：[监听与 handler](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/web.go#L82-L107)、[NetworkLinks](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/web.go#L289-L302)、[cookie 名](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/web.go#L324-L328)。

**CSRF 与退出边界**：该 Web guard 不要求名为 `csrf`/`xsrf` 的 Cookie 或 token；依赖 SameSite=Lax 等浏览器边界，并在 `/api/` 拒绝 `Origin: null`、frame 目的、cross-site 非 document 请求。ESP-IDF 原生客户端不需要模拟浏览器 JS 或构造 CSRF token。这里不能推导“所有厂商都不用 CSRF”。在该版本 Web 路由/guard 中没有找到管理 Web 专用 logout/session revoke 端点；清设备 Cookie 只结束设备本地使用，不能使已泄露的 Web key 失效，撤销应更换服务 key/随机 key 重启（自动更新可能保留 key）。账号的 `/api/login/forget` 是另一个层次。来源：[frame guard](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/pluginpage.go#L301-L318)、[Web guard](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/web.go#L195-L286)。

### 2.2 网关 API 认证

远端 NOTE4 需要启用网络共享、通过 IP allow guard、发送启用的命名网关 key。支持 `Authorization: Bearer <KEY>`、`x-api-key`、`x-goog-api-key` 或 query `key`；设备建议只用 Bearer，避免密钥进入 URL 日志。Web key 和 Web Cookie 不能代替网关 key。未共享返回 `403`，无效/禁用/删除的 key 返回 `401`，过多失败认证返回 `429` 与 `Retry-After`。代理或隧道的 forwarding header 会使请求按远程处理，不能通过本机代理绕过限制。来源：[LAN guard](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gateway/lan.go#L246-L283)、[identifyCaller](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gateway/lan.go#L414-L445)、[key 读取与错误](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gateway/lan.go#L500-L549)。

密钥结构有 `off` 与限制字段，没有声明 TTL/expiry；不要给网关 key 编造“7 天有效期”。其实际有效性由配置、撤销/禁用和授权集合决定。网关额度授权不等于厂商 token 过期状态，后者由 Magpie 处理。来源：[Key/Identity](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/access/access.go#L111-L147)。

### 2.3 厂商账号登录 API 与凭据归属

下列全部是管理 API，须先过 Web Cookie（桌面内嵌 handler 的访问边界另有不同）。它们驱动 **Magpie 主机** 登录厂商，不是 NOTE4 自己的登录。

| 方法与路径 | 请求字段 | 响应/行为 |
| --- | --- | --- |
| `POST /api/signin` | JSON `agent,site,proxy,user`，后几项为选项 | `SignInState` 加打开网页错误信息；启动内置账号流程 |
| `GET /api/signin/{id}` | flow id | 当前 `SignInState`，未知 id `404`；UI 每 800ms 轮询 |
| `POST /api/signin/{id}/cancel` | 无业务 body | `204`，状态变 canceled |
| `POST /api/signin/{id}/callback` | `{url: "..."}`，读上限16KiB | 粘贴回调 URL/插件支持的 code；成功 `204` |
| `POST /api/signin/import` | `{agent,files:["文件文本",...]}`，上限8MiB | `{results,providers}`；导入前检查厂商，最长3分钟上下文；不是设备应走的路径 |
| `POST /api/plugin-signin/prompt` | `provider,method,inputs,key,value` | `{prompt,inputs}` 或 `{error}`；插件方法可逐问输入 |
| `POST /api/plugin-signin` | `provider,method,inputs,key,proxy,user` | OAuth flow 或 API key 方式直接 `done`，之后仍复用 signin 状态接口 |
| `POST /api/login/switch`、`forget`、`on`、`off` | `{agent,user}` | 切换/忘记/启用/禁用账号，响应 providers state；不是 Web logout |
| `POST /api/stepfun/{id}/session` | `{text: "粘贴的会话文本"}` | 保存平台会话，响应 providers state |
| `POST /api/stepfun/{id}/signout` | provider id | 移除该 StepFun 平台会话 |

来源：[signin 与账号切换注册](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/providers.go#L2027-L2059)、[signin handlers](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/providers.go#L2163-L2209)、[导入与 StepFun](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/providers.go#L2210-L2284)、[插件 handlers](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/plugins.go#L672-L748)、[800ms 轮询](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/assets/app.js#L12173-L12193)。一般 handler 的 `fail` 是 HTTP400 JSON `{error,...}`，因此厂商登录失败不应一概按 HTTP401 判断；flow 可在 HTTP200 中表示 `state:"failed"`。来源：[fail](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/api.go#L2481-L2504)。

`SignInState` 必要字段为 `id,agent,url,state`，状态枚举 **`installing | waiting | done | failed | canceled`**；可选 `code,pasteCallback,pasteCode,pasteKey,keysURL,instructions,opened,installing,user,plan,using,again,error,callbackBlocked,callbackPort,proxy`。通常等待浏览器10分钟；需要安装 CLI 则再加安装超时；同一 agent 新流程取消旧流程。来源：[state schema](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/signin.go#L47-L99)、[启动与超时](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/signin.go#L243-L292)。

关键厂商差异：

- **Codex/ChatGPT**：浏览器 OAuth authorization-code + PKCE S256，`state` 防串流程；授权页 `https://auth.openai.com/oauth/authorize`，redirect `http://localhost:1455/auth/callback`（主机无法监听时支持粘贴）。`POST https://auth.openai.com/oauth/token` 表单有 `grant_type=authorization_code,code,redirect_uri,client_id,code_verifier`，期待 `id_token,access_token,refresh_token`，保存于主机 auth/keychain 相关结构；刷新 token 会轮转、进程/跨进程加锁，设备不能复制 refresh token 自行与 Magpie 竞争刷新。没有固定的 access token 有效小时数承诺。来源：[authorize](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/signin.go#L345-L365)、[exchange/storage](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/signin.go#L825-L863)、[轮转与过期处理](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/account.go#L939-L1032)。
- **Copilot**：主机 `POST https://github.com/login/device/code`，form `client_id,scope=read:user`；返回 `device_code,user_code,verification_uri,expires_in,interval`。再 `POST https://github.com/login/oauth/access_token`，form `client_id,device_code,grant_type=urn:ietf:params:oauth:grant-type:device_code`，期待 `access_token`；`authorization_pending` 继续、`slow_down` 加5秒、`expired_token/access_denied` 失败。轮询下限1秒；GHE 域可不同。设备只需展示 flow 的 URL/code，浏览器由人完成。来源：[device flow](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/copilot_signin.go#L31-L168)、[poll floor](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/signin.go#L47-L60)。
- **Claude Code**：该版本调用主机自己的 `claude auth login`，没有在 Magpie 内自行重做完整登录；**Gemini/Antigravity**：主机 Google OAuth，本机临时 callback 端口。不要把旧版厂商登录逻辑套到当前版本。来源：[begin 分派](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/signin.go#L339-L393)。
- **插件厂商**：内置退休清单包括 Command Code、Cursor、Devin、Factory、Grok、Kiro、MiMo App、Qoder/CN、WorkBuddy/AI、ZCode、Zed；实际登录、刷新、额度都由社区插件承担，当前主仓库残留旧账号文件读取不能证明它还在向旧厂商 API 发请求。插件精确 HTTP 契约需单独固定插件版本，本研究未检查外仓所有插件。来源：[上游迁移/退休说明](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/AGENTS.md#L16-L104)、[内置拒绝已迁移账号登录](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/signin.go#L243-L252)。
- **StepFun 平台会话**：API key 不足以读取 Step Plan。用户浏览器先登录，bookmarklet 刷新后复制会话给 Magpie；主机保存于 `stepfun-auth.json`，后续向平台 POST refresh/额度接口，带 `Cookie: Oasis-Token=<access>...<refresh>`。这是**厂商 Cookie**，应留在 Magpie 主机，与 `magpie_web_<port>` 无关。来源：[StepFun 协议注释](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/stepfun_plan.go#L3-L23)、[实际 Cookie 发送](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/stepfun_plan.go#L350-L360)。

账号列表 `Login` 不含 secrets，包含 `agent,user,plan,seen,active,on,lapsed,own,paused,returns`；`lapsed` 表示厂商拒绝刷新、需再次登录。主机保存的 `savedLogin.Auth` 则是实际 token blob，绝不能下发到 UI/设备作为“额度数据”。`forget` 的行为有厂商差异，例如 Codex 当前账号有备用时先切换、最后账号可删除 keychain/auth.json；不能声称任意 `forget` 都会远程撤销厂商 refresh token。来源：[Login 与 savedLogin](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/logins.go#L40-L104)、[ForgetLogin](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/logins.go#L1240-L1335)。

## 3. 路由页：后端事实、动画与断线恢复

### 3.1 后端协议

`GET /api/gateway/trace?after=<int64>` 立即返回 JSON；带任意非空 `wait` 时在没有变化的情况下最多等待25秒。服务在第一次变化后额外合并250ms，返回“发生变化的 route 完整对象”，不是需要逐条应用的 JSON patch。第一轮用 `after=0`；以后用最后成功提交快照的顶层 `seq`。来源：[HTTP handler](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/trace.go#L360-L380)、[Trace 实现](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gateway/trace.go#L820-L874)。

| JSON | 含义与设备处理 |
| --- | --- |
| `seq:int64` | 整体变化游标，非时间戳；完整解析并入库后再推进 |
| `mine:bool` | 此页面能获取正在服务网关的 trace（可能经同机进程代理），不简单等于“Web 进程就是网关” |
| `now:RFC3339` | 服务器时间；可估算显示时钟偏差 |
| `routes:[]` | `route.seq > after` 的对象；以 `id` 合并替换 |
| `ids:[]int64` | 当前 trace 保留的所有 id；删除本地未完成而已不在 ids 的条目，避免永远显示运行中 |
| `totals:{requests,rerouted,errors}` | 网关运行内累计（具体字段以 Totals 源码为准），不是厂商剩余额度 |
| `by?:{pid,serves}` | 从其他 Magpie 服务进程获取 trace 时提供，帮助判断 takeover |

`totals.requests`是网关启动以来已答复请求数，`rerouted`是失败后转交后续候选的try数，`errors`是最终向agent返回错误的请求数。来源：[TraceState/Totals](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gateway/trace.go#L500-L527)、[GUI envelope 与价格字段](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/trace.go#L20-L34)。

路由必要字段为 `id,seq,time,agent,model,provider,order,left,tries,done,status,error,ms,tokens,out`，另可有 `group,rule,affinity,pinned,ttft,firstText,flow,sent,queued,...`。`order` 是加权后的候选；候选包括 `id,provider,name,kind,who,known,used,amount,limit,unit,renews,pace,due,rest,capped,capBack,...`，它们是**决策当时**的额度与冷却值，不是持续刷新的全局账号清单。`known=false` 必须显示未知，不能按 `used=0` 解释为满额可用。来源：[Route](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gateway/trace.go#L33-L143)、[候选与额度](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gateway/trace.go#L234-L313)、[决策快照赋值](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gateway/trace.go#L424-L475)。

`tries[]` 含 `id,model,start,done,status,ms,ttft,fail,error,rest,again,gone,...`。没有统一 `phase:"streaming"` 后端枚举，设备阶段应由字段推导并标为本地状态：`!done && tries空` 为等待路由；最新 try 未完成为处理中；失败且出现下一 try 为重试；route done 后再判结果。UI 成功判断是 `status < 400 && !fail`，不能只见 HTTP200 就认定成功，因为流中断后仍可标 fail。错误类别有 `rate,credit,quota,other,auth,canceled,foreign,floor,verify,refused,shape,proxy,effort,overflow,room,slow,prompt,restricted`，未知新字符串保留或映射 OTHER。`rest` 含 `why,status,until,by,failures,key,model,link`，`by` 可为 `credit,retry-after,resets,window,quota,cooldown,backoff,restricted,off`。来源：[Try](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gateway/trace.go#L315-L394)、[UI fail/tryOk](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/assets/plugins/routing/client.js#L445-L449)、[Rest](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gateway/routing.go#L482-L506)。

### 3.2 动画实际是什么

小鸟/圆点飞行、线条、呼吸灯、返回包等均是浏览器根据上述状态执行的本地动画。`play()` 等待 `tries` 出现、某次 `try.done`、最终 `route.done`，但去程620/900/1400ms、回程1400ms等时长是视觉安排，不能当作后端传输时间；真实耗时另有 `ms/ttft/...`。它用 SVG/DOM、`requestAnimationFrame`、浏览器 animation API；回放还会把超过1800ms的空隙裁短。后端并没有发送“小鸟坐标/动画帧”。来源：[play](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/assets/plugins/routing/client.js#L3370-L3482)、[replay 时间压缩](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/assets/plugins/routing/client.js#L3499-L3508)。

上游页面倒计时每1秒更新，trace 只在页面/托盘路由标签可见时继续长轮询；并非固定1秒网络轮询。catch 后等待3秒再请求，`mine=false` 时等5秒；路由 session title 还有15秒独立轮询。上述是**上游实际周期**，下文 NOTE4 的建议周期并非来自上游。来源：[标题轮询](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/assets/plugins/routing/client.js#L2648)、[可见性与长轮询](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/assets/plugins/routing/client.js#L3743-L3861)、[失败重试](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/assets/plugins/routing/client.js#L3905-L3907)。

### 3.3 恢复规则与丢失边界

- 以 id 合并每次返回的完整 route。若新 `seq` 小于旧值，说明网关重启，清旧状态并从全量重新开始；`by.pid` 改变时上游 UI 清 routes、`seq=0` 再请求。服务端也会将 `after > current seq` 归零。来源：[UI 恢复](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/assets/plugins/routing/client.js#L3818-L3853)、[服务端回退](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gateway/trace.go#L827-L841)。
- trace保留目标为60个请求，优先移除已完成项；全为进行中时可以暂留到120个，因此不是严格最多60个。不能据游标声称断线后可重放所有过程。短请求可能在一次轮询间隔内已结束，多次 try 中间变化会合并；离线久了可能不在内存。`ids` 用于修复“未完成”条目，而不是缺失事件补全。来源：[traceKeep](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gateway/trace.go#L29-L30)、[trim](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gateway/trace.go#L638-L648)、[UI ids 处理](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/assets/plugins/routing/client.js#L3846-L3853)。
- 历史可选 `GET /api/gateway/history?day=YYYY-MM-DD` 得到 `{days,routes,cut}`；单条 `GET /api/gateway/route?id=<id>&day=<date>` 优先找实时记录，再找历史，错误日期400/过期404。这是日期查询，不能臆造分页 cursor 或 Last-Event-ID。设备最小实现不读历史，界面注明“最近观测/可能遗漏”；若要准确累计，读取后端权威 totals/统计，而非设备自己数每次变化。来源：[历史 handlers](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/trace.go#L335-L387)。
- 不把 `/api/gateway/call`（请求/响应详情）、prompt、session title、folder、archive、错误正文等同步给设备 UI。trace 本身带账号名、session、路径片段、Prompt 统计对象和错误文字，不能当无敏感数据的心跳；最小快照只保留白名单字段。实际请求详情另经 call 接口读取，不需要为了动画获取。来源：[Route 元数据](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gateway/trace.go#L47-L143)、[GUI call 路由](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/providers.go#L1057)。

## 4. 额度 API、数据与厂商差异

### 4.1 对外 HTTP 端点全集（该版本中与账号额度/余额直接相关的入口）

**网关聚合已存在。** `GET /v1/magpie/quotas` 返回 `{"object":"list","data":[Quota,...]}`；没有 `count/total/nextCursor`，卡片数只能由 `data.length` 得到，而且去重/权限过滤后不能等价为“所有账号总数”。不解析 `provider/user/cursor/offset/limit`，不能据 query 自行假设过滤或分页。`kept=1` 明确只读取已保留快照、不问厂商；普通GET允许刷新缓存，12秒上下文。来源：[注册](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gateway/gateway.go#L687-L697)、[handler](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gateway/gateway.go#L797-L815)、[seat去重](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/account_seats.go#L104-L152)。

| 方法与路径 | 鉴权层 | 参数/返回与刷新含义 |
| --- | --- | --- |
| `GET /v1/magpie/quotas[?kept=1]` | gateway key + scope | list包装的精简Quota；普通读可触发缓存刷新，kept只读旧值 |
| `GET /v1/magpie/quotas/cards` | 同上 | list包装的GUI `SubscriptionQuota`；只取本机缓存、不请求vendor；字段比Quota多 |
| `POST /v1/magpie/quotas/refresh?provider=&user=` | 同上 | 返回cards；provider/user可选，无provider刷新可见集合；30秒上下文；同provider+小写user节流30秒；拒绝继续转发远端卡 |
| `GET /v1/magpie/quotas/history?days=&provider=&user=` | 同上 | list包装 `QuotaHistory[]`；days至多45；范围过滤，无分页 |
| `GET /v1/magpie/limit` | gateway key自身 | `{limited:false,key:...}` 或 `{limited:true,key:...,limit:...,limits:[...]}`；这是网关key预算，不是厂商余额 |
| `GET /api/usage/quotas[?asked=1]` | Web Cookie | 裸 `SubscriptionQuota[]`；asked任意非空标记主动读取；可带 `X-Magpie-Reading:1`；12秒 |
| `POST /api/usage/quotas/refresh?provider=&user=` | Web Cookie | provider必填；返回全部卡片；30秒 |
| `GET /api/usage/quotas/history?days=` | Web Cookie | 本地+远端历史；该GUI入口不解析provider/user；远端10秒 |
| `GET /api/usage/quotas/history/holds?provider=&user=&from=&to=` | Web Cookie | 已完成RFC3339Nano时间区间；只算本地历史与缓存，不询问厂商 |
| `GET /api/login/usage?agent=` | Web Cookie | `map[user]SubscriptionQuota`；一个agent多个账号并发、共用账号缓存；12秒；不可假定省略agent代表所有agent |
| `POST /api/provider/balance` | Web Cookie | 编辑器按form内id/URL/path/method/headers/token即时读取；`{ok,amount,error?}`；15秒；非批量展示首选 |
| `POST /api/usage/codex-reset` | Web Cookie | `{user}`；**消费一个reset credit**；`{code,windows}`；设备读取额度不需要调用 |
| `POST /api/usage/workbuddy-checkin`、`trae-checkin`、`minimax-checkin`、`qoder-checkin` | Web Cookie | 供应商签到写操作；一分钟上下文；返回结果数组；不是普通读数 |
| `POST /api/usage/plugin-checkin` | Web Cookie | `{provider}`，插件签到写操作 |

网关cards/refresh来源：[gateway.go](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gateway/gateway.go#L817-L905)、[path常量](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/remote_quotas.go#L31-L41)；GUI来源：[usage.go](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/usage.go#L180-L328)、[balance handler](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/providers.go#L1916-L1943)、[LoginUsage](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/providers.go#L2021-L2025)；key budget来源：[key_limit.go](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gateway/key_limit.go#L111-L124)。

相关设置写入口还有 `POST /api/settings/quota-reads`、`quota-left`、`account-discovery`、`codex-auto-reset`、`codex-credits`；后两项可改变自动消费reset与额度耗尽后使用credits的行为。账号开关/切换/删除与StepFun会话在2.3节。`/v1/magpie/usage`、`/usage/requests` 属于使用账本统计，不能替代厂商当前可用额度。来源：[读取策略设置](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/api.go#L1384-L1422)、[reset/credits设置](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/api.go#L1719-L1762)、[usage路由注册](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gateway/gateway.go#L687-L690)。

### 4.2 “所有账号”的授权含义

不受budget/model/account限制的启用gateway key可得到聚合可见集合；**受限key默认403 `gateway_key_held`**，只有显式开启 `quotas` 才按scope返回。scope要求provider的catalog模型均被允许，再核对账号/key集合；同名多个账号须都允许。受限输出排除远端卡，移除窗口 `holds/holdsWhy`；key自身 `/limit` 仍可读。故“有Bearer就可看所有账号”不成立，也不能用展示端client过滤代替服务端授权。来源：[scope定义与过滤](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gateway/quotas_scope.go#L41-L182)、[held拒绝](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gateway/usage_api.go#L29-L48)。

普通GET是聚合完成后过滤输出，冷读仍可能读取其他账号；refresh会将刷新的卡限制为可见范围。它不是每个调用者独立的一套供应商刷新缓存。建议给设备单独命名key、按账号/模型授权并开启quotas，若要真正只读、隐藏其他网关能力，另由反向代理/适配服务白名单限制方法路径（建议，非key内置read-only flag）。来源：[GET先report后scope](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gateway/gateway.go#L802-L814)、[refresh scope](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gateway/gateway.go#L872-L901)。

### 4.3 响应schema与单位

两套schema不可混用。公开v1 `Quota`：

```text
provider,name,kind(subscription|plan|balance),plan?,user?,windows[],
balance?,balanceSplit?,error?,asOf?,readDeferred?,readAt?,
until?,renew?,resets?,resetsUsed?,lastServedAt?,last?,from?,alias?,seat?
```

公开 `QuotaSpan`：

```text
name,used,remaining,unlimited?,resetsAt?,display?,
amount?,limit?,unit?,pool?,holds?,holdsWhy?
```

`used/remaining`是**百分比**；`amount/limit/unit`是厂家提供的原始计量。mapper直接计算 `remaining=max(0,100-used)`，没有统一把所有used强制钳到100，数据可超额；`unlimited=true`仍单独传出，不能把算出的remaining当有限额度，UI应优先显示“无限”。`amount/limit/unit`带omitempty，可缺失，0不能一律解释为“厂家明确给了零上限”。`resetsAt`缺失/null表示没有已知绝对重置时间；内部只有正的`resetSecs`时mapper推为本次`now+resetSecs`，设备不能将缺失时间当Unix0。不同厂商、不同周期、共享池的百分比不能相加。`balance`是格式化字符串，不能直接 `atof` 当统一美元数；有 `balanceSplit` 时按其单位/币种展示。`readAt`是实际读取时间，`asOf`是本次失败fallback所用旧值时间，`readDeferred`表示主动没有读取；HTTP200不表示每张卡fresh，必须同时检查error及时间。来源：[Quota/Span结构](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/quotas.go#L77-L138)、[转换](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/quotas.go#L170-L199)。

GUI/cards的 `SubscriptionQuota/QuotaWindow` 还包含 `icon,accessSku,balanceParts,balanceTrend,pluginError,needsAccessKey,seeAt,held,daily,checkins,gatewayKey` 等；窗口可有 `resetSecs,runsOutAt,runsOut,family,credits,capped,capsSome,capId,startsAt`，不能把它们全部承诺给精简v1客户端。共享池经 `PooledWindows` 输出，不能把模型窗口简单累加成账号总额。来源：[GUI schema](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/subscription_usage.go#L29-L210)、[report窗口转换](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/quotas.go#L170-L199)。

### 4.4 厂商读取全集与开放边界

下面列出当前主仓库能静态确认的调度族及其一手实现。它是Magpie主机到厂商的路径清单，**NOTE4不用逐个实现**。存在路径不证明真实服务今天可用。

| 内置账号订阅 | 主机额度来源/认证 | 差异 |
| --- | --- | --- |
| Claude Code | 主机运行 `claude -p /usage --output-format json ...` | CLI自己处理认证与HTTP；不能编造内部quota URL；解析session/week |
| Codex | `GET https://chatgpt.com/backend-api/wham/usage`；Bearer+`chatgpt-account-id` | primary/secondary、plan/credits/reset；有reset credits另读`/wham/rate-limit-reset-credits` |
| Copilot | `GET https://api.github.com/copilot_internal/user`；`Authorization: token <github-token>`+Copilot头 | 企业域可不同；chat/completions/premium、unlimited/overage/reset |
| Gemini CLI | `POST https://cloudcode-pa.googleapis.com/v1internal:retrieveUserQuota`，`{project}`，OAuth Bearer | 模型bucket remainingFraction/resetTime；还可能需project/token读取 |
| Antigravity | `POST https://daily-cloudcode-pa.googleapis.com/v1internal:fetchAvailableModels`及`:retrieveUserQuotaSummary`，OAuth Bearer | per-model与5h/week共享池；tier失败会重试其他tier，summary失败fallback |

来源：[实际账号调度](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/subscription_usage.go#L394-L512)、[Codex/Copilot解析](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/subscription_usage.go#L941-L1198)、[Google endpoints](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/google.go#L65-L100)、[Google quota](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/google.go#L1060-L1296)、[Claude CLI读取](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gateway/claude_warmup.go#L82-L115)。

| API key/平台套餐族 | 主机路径 | 凭据/注意 |
| --- | --- | --- |
| BigModel / Z.ai | `GET https://open.bigmodel.cn/api/monitor/usage/quota/limit` 或 `https://api.z.ai/...` | 裸key放Authorization；个人套餐另读subscription/list与customer-package-reset/list，可能回退团队quota `?type=2`及business host |
| Kimi Code | `GET <coding/v1 base>/usages` | Bearer；按host/path识别，非固定provider id |
| OpenCode Go | `GET https://opencode.ai/zen/go/v1/usage` | Bearer+OpenCode session头；不能代替Zen余额 |
| Command Code keyed preset | `GET https://api.commandcode.ai/alpha/billing/credits`，必要时`/subscriptions` | Bearer；与已退休commandcode-plan账号插件区分 |
| MiniMax Coding Plan | `GET https://api.minimaxi.com/v1/token_plan/remains` 或`.minimax.io` | Bearer；地区差异 |
| Ollama Cloud | `GET https://ollama.com/api/balance` | Bearer；included allowance与purchased balance分别建卡 |
| StepFun Step Plan | 平台`QueryStepPlanRateLimit`、`GetStepPlanStatus` POST，详见2.3 | 保存的Cookie session，不是推理API key |
| 火山Ark Coding/Agent Plan | `POST https://open.volcengineapi.com/?Action=GetCodingPlanUsage`或`GetAFPUsage`，Version=2024-01-01 | 账号AK/SK、HMAC-SHA256/service ark；不是推理key |
| 腾讯Token Plan企业版 | `POST https://tokenhub.tencentcloudapi.com/`，`DescribeTokenPlanList/DescribeTokenPlan`，version=2026-03-22 | SecretId/SecretKey/TC3-HMAC-SHA256；不是sk套餐key；个人/TokenHub按量余额不支持 |

来源：[plan host识别](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/planquota.go#L37-L70)、[plan请求与GLM分支](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/planquota.go#L505-L617)、[套餐调度](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/planquota.go#L635-L838)、[StepFun](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/stepfun_plan.go#L493-L613)、[火山](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/volcengine_usage.go#L61-L143)、[腾讯](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/tencent_usage.go#L96-L235)。

| 内置余额host分派 | URL/金额路径 |
| --- | --- |
| DeepSeek | `https://api.deepseek.com/user/balance`，`balance_infos.total_balance`及币种/赠送拆分 |
| Moonshot CN/国际 | `https://api.moonshot.cn/v1/users/me/balance`或`.ai`，`data.available_balance` |
| OpenRouter | `https://openrouter.ai/api/v1/credits`，`total_credits-total_usage` |
| SiliconFlow CN/国际 | `https://api.siliconflow.cn/v1/user/info`或`.com`，`data.totalBalance`；410/code20092作为端点退休错误，无替代承诺 |
| StepFun CN/国际 | `https://api.stepfun.com/v1/accounts`或`.ai`，`balance` |
| Command Code | `/alpha/billing/credits`，monthly+purchased+free credits |
| Ollama Cloud | `/api/balance`，purchased balance |
| AiHubMix | `https://aihubmix.com/dashboard/billing/remain`的total_usage在此指key剩余；独立BalanceToken走`/api/user/self`的`data.quota/500000` |
| Cline特殊分支 | `GET https://api.cline.bot/api/v1/users/me/plan/usage-limits`、`/users/me`、`/users/{id}/balance`；Bearer+Cline client头，金额millionths USD/1e6，另有ClinePass窗口 |

常规key余额通常用Chat认证头（一般Bearer），自定义headers可覆盖。来源：[余额分派](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/balance.go#L315-L569)、[默认认证](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/balance.go#L46-L87)、[AiHubMix](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/balance.go#L1210-L1258)、[Cline](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/cline_usage.go#L26-L155)。

开放扩展：自定义 `BalanceURL/BalancePath/BalanceMethod/BalanceToken/Headers` 支持JSON路径、数组、算术/聚合；兼容new-api/one-api `/api/usage/token`、`/api/user/self`，sub2api `/v1/usage`、`/api/v1/user/profile`，旧relay `/dashboard/billing/credit_grants`。独立BalanceToken默认裸Authorization、JWT自动Bearer、已有scheme保留；new-api账号token还需New-Api-User。因此没有一个可永久穷举的“全厂商URL表”。ChatGPT API账号剩余额度与OpenCode Zen余额在该版本指向网页设置/console查看，不能冒称已有读取端点。来源：[自定义余额](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/balance.go#L121-L184)、[relay识别](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/balance.go#L572-L604)、[BalanceToken认证](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/balance.go#L1557-L1567)。

插件运行时枚举 `plugin.Cached()`，声明Usage、已登录、未迁移中的账号逐个 `AccountUsage → auth.usage`，每账号20秒，未见统一厂商批量hook/分页协议。2.3所列13个退休ID由11个插件包承担；具体URL/认证/内部请求成本不可从主仓库确认，必须另查固定插件版本。来源：[plugin_usage](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/plugin_usage.go#L73-L174)、[RPC schema](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/plugin/providers.go#L1047-L1116)、[host usage hook](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/plugin/host.js#L2322-L2403)。

### 4.5 批量、分页、缓存与刷新成本

**批量是Magpie聚合，不代表每厂商都能一请求批查。** 订阅、单账号、key套餐、余额大多有一分钟缓存；订阅有pending合并，过期已有缓存可立即返回并后台刷新，首次无缓存需等待，后台20秒上下文独立于请求取消。key套餐/余额未见同样的pending合并，冷读并发可能重复访问。`keyReadsAtOnce=8`是每批/每provider的并发槽，不是全进程统一上限。来源：[订阅缓存](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/subscription_usage.go#L244-L309)、[单账号缓存](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/login_usage.go#L163-L240)、[套餐缓存](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/planquota.go#L635-L753)、[余额缓存](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/balance.go#L1423-L1544)、[并发槽](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/keys.go#L264-L294)。

| 成本/可靠性情况 | 实际影响 |
| --- | --- |
| 多API key | 通常逐key查询；独立account BalanceToken余额可只查一次；不限为一请求/provider |
| Cline | 完整读取通常limits+user id+balance三次；候选ID重试可更多 |
| GLM | 个人quota之外通常还有term/reset读取；团队fallback增加请求 |
| 腾讯企业 | list一次+每team detail；list固定Offset0/Limit100，未翻后续页，超过100项截断风险；源码注明无真实账号实测 |
| Ollama/Command Code | plan与balance可能分别查询同URL，未见共享原始response cache |
| Google/Codex/Copilot | token/project刷新、tier fallback、401后换token等增加请求 |
| Claude Code | 手动最低间隔30秒；自动随机5–15分钟且需近期Claude使用；非当前保存账号只在该卡手动刷新运行自己的/usage，整页刷新不会逐一启动 |
| GUI单卡refresh | 随后调用整批Quotas响应全部卡，其他过期/未初始化缓存仍可能带来读取 |
| read策略“when I ask” | 非Asked请求可不读vendor；旧读数并非强制fresh SLA；quota拒绝的stale处理有例外 |

来源：[腾讯分页/未实测](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/tencent_usage.go#L155-L235)、[腾讯注释](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/tencent_usage.go#L3-L22)、[Claude读取策略](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/subscription_usage.go#L626-L873)、[定向refresh](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/quota_refresh.go#L96-L204)、[GUI后续整批](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/usage.go#L206-L218)及4.4各实现。

最后成功读数持久化于Magpie主机 `quotas.json`，部分临时错误fallback，未发现统一最大陈旧期限；`readAt/asOf/readDeferred/error`必须展示。远端Magpie卡与remote-key限额缓存30秒，普通卡读取对端`/cards`只取缓存，不递归读它的远端卡；每远端8秒/最多8MiB，key限额每key8秒/最多1MiB。来源：[last-good](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/quota_last.go#L59-L183)、[remote cards](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/remote_quotas.go#L83-L131)、[remote key limits](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/remote_key_limits.go#L123-L175)。

额度/cards/LoginUsage/history均无cursor分页；history最多45天、每窗口最多6000点、5分钟采样/30秒合并，可能附区间前一点供连线。结构为 `[{provider,user,lines:[{name,points:[{at,left,start?,resetsAt?}]}]}]`，不能下载全历史进设备后再简单作图。来源：[历史常量/schema](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/quota_history.go#L43-L76)、[过滤与日期](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/quota_history.go#L248-L310)、[采样](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/quota_history.go#L385-L421)。

Codex reset是实际 `POST https://chatgpt.com/backend-api/wham/rate-limit-reset-credits/consume`，body含 `credit_id,redeem_request_id(UUID)`，Bearer+account-id，会消费credit。GLM reset只展示并让用户去厂商页面消费。普通额度终端不应自动触发reset/checkin/credits配置。来源：[Codex consume](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/codex_resets.go#L206-L243)、[GLM reset处理](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/planquota.go#L791-L838)。

## 5. NOTE4 的最小可实现设计

### 5.1 本地事实与可用接缝

当前 NOTE4 为400×300黑白墨水屏。设备UI由独立Rust组件完成字体测量、分页和栅格化；C层保留Wi-Fi、BLE、HTTPS、快照与EPD presenter。字体采用固定cmap，未覆盖字符显示Unicode码点标签；UTF-8传输成功不等于字形可显示。

当前UI使用1bpp full baseline及后续partial damage更新。显示buffer包含RGB565 240000B PSRAM、BW15000B PSRAM，以及Rust两个固定布局arena；gray60000B PSRAM仅在请求灰度呈现时临时分配，当前黑白UI不常驻该缓冲。面板刷新本身仍有硬件延迟；不宜逐秒全刷。实际internal余量必须在Wi-Fi/BLE/TLS组合负载下测量。

独立screen task接收最新状态快照及带版本的额度候选，直接借用C快照构建Rust布局；只有布局、呈现和版本接受成功后提交模型。短按翻页复用布局缓存。实现见 `main/note4_screen.c` 和 `components/note4_ui/`。

持久化当前只是 Wi-Fi NVS blob，`nvs_flash_init/nvs_open` 本身不能证明加密；开发文档明确未启用 flash/NVS encryption。NVS 分区仅24KiB，没有专用 cache/credential 分区或 nvs_keys；不要把每次额度/路由日志写进现有 NVS。Wi-Fi driver 用 RAM storage，项目自行保存 Wi-Fi blob。来源：[config store](../../main/config_store.c#L16-L60)、[分区](../../partitions.csv#L2-L4)、[开发版加密说明](../ble-provisioning.md#persistence-and-hardware)、[Wi-Fi RAM storage](../../main/wifi_manager.c#L79)。

BLE 已有加密/认证校验及 bonding/MITM/SC 可作为将来凭据配置的基础，但现有 SET_SLOT 只支持 Wi-Fi。每帧实际payload至多8B、事务上限1024B，新增 server URL/认证模式/key 配置要设计新 opcode、长度上限、版本与提交语义，不能声称现有网页已能下发 Magpie Cookie。HttpOnly Cookie 也不能让普通网页 JS 直接读出转发；跨源网页自动登录另受 CORS 与浏览器策略约束。来源：[BLE 认证校验](../../main/ble_provisioning.c#L59-L62)、[bonding配置](../../main/ble_provisioning.c#L237-L240)、[协议限制/SET_SLOT](../ble-provisioning.md#frames)。

### 5.2 推荐最小接口集合与请求示例

**建议阶段一**只有网关聚合额度 GET，手动刷新作为可选项；设备不触发厂商登录、不花费 quota reset、不修改账号上限或开关。额外需要配置 `gateway_base_url`、Bearer key、服务器证书信任及显示筛选；如要 trace，再单独配置 `web_base_url` 和 Web key/会话，或设计同机过滤桥接服务。适配服务是尚未实现的建议，不应向用户展示一个不存在的“NOTE4 API”。

以下为协议示例，所有地址和 key 都是占位符，未发到真实服务。网关与 Web 可以是不同端口或代理 location；不能盲用同一 base URL。

```http
GET /v1/magpie/quotas HTTP/1.1
Host: magpie.example.invalid
Authorization: Bearer <ENABLED_GATEWAY_KEY>
Accept: application/json
Accept-Encoding: identity
```

若确需直接读 Web trace，先原生客户端表单登录（避免把 Web key 放在 URL）。关闭自动跨站重定向，先收集303的 headers：

```http
POST /_magpie/sign-in HTTP/1.1
Host: magpie-web.example.invalid
Content-Type: application/x-www-form-urlencoded
Accept: application/json

key=<URL_ENCODED_WEB_KEY>&back=%2F
```

成功收到例如 `Set-Cookie: magpie_web_<INTERNAL_LISTEN_PORT>=<WEB_KEY>; Path=/; HttpOnly; SameSite=Lax; ...`，保存**实际** name/value，再读：

```http
GET /api/gateway/trace?after=0 HTTP/1.1
Host: magpie-web.example.invalid
Cookie: <ACTUAL_COOKIE_NAME>=<WEB_KEY>
Accept: application/json
Accept-Encoding: identity
```

下一次 `after=<LAST_COMMITTED_SEQ>&wait=1`。Cookie 请求只发 name=value，不把 `HttpOnly/SameSite/Path/Max-Age` 当作 Cookie 键回传。各 `Set-Cookie` 独立解析，不能按逗号拆分（Expires 自身含逗号）；只为配置的 origin/host/path 保存单实例会话，限制重定向目标与HTTPS，主机/端口改变后重新确认实例。Cookie RFC 本身不按端口隔离，设备可以实施更严格的固定 origin 发送策略。来源：[RFC6265 §3、§4.2.2、§5.4](https://www.rfc-editor.org/rfc/rfc6265)、[上游 port cookie](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/web.go#L200-L208)。

### 5.3 C 快照与状态机（设计建议）

以下是**设备内部结构草案**，不是上游 wire schema，也不是本任务已写入固件的代码。字符串、数组上限需产品选择与压力测试；超限必须标记 truncated，不能静默少算账号。百分比/金额缺失与数值0必须分开。上游 route id/seq 应用64位保存。

```c
#define MP_MAX_ACCOUNTS 16
#define MP_MAX_WINDOWS 4

typedef enum {
    MP_BOOT, MP_WAIT_WIFI, MP_SYNC_TIME, MP_FETCHING,
    MP_READY, MP_STALE, MP_BACKOFF, MP_AUTH_REQUIRED, MP_CONFIG_ERROR
} mp_net_state_t;

typedef struct {
    char label[48];
    bool unlimited, used_valid, amount_valid, reset_valid;
    double used_percent;       /* 转换后的设备值，勿混淆上游其他 used 字段 */
    double amount, limit;
    char unit[16];
    int64_t reset_unix_s;
} mp_window_t;

typedef enum { MP_SUBSCRIPTION, MP_PLAN, MP_BALANCE } mp_card_kind_t;
typedef enum {
    MP_VALUE_UNKNOWN, MP_VALUE_FRESH, MP_VALUE_STALE,
    MP_VALUE_DEFERRED, MP_VALUE_AUTH_ERROR, MP_VALUE_READ_ERROR
} mp_value_state_t;

typedef struct {
    char ref[128], provider[48], display_name[64];
    mp_card_kind_t kind;
    mp_value_state_t value_state;
    int64_t read_at_unix_s, as_of_unix_s; /* 0为未提供，非1970年的读数 */
    char balance_text[96];      /* 保留格式/币种；拆分若需要则另加有界字段 */
    bool balance_present, windows_truncated;
    uint8_t window_count;
    mp_window_t windows[MP_MAX_WINDOWS];
} mp_card_t;

typedef enum {
    MP_ROUTE_UNKNOWN, MP_ROUTE_IDLE, MP_ROUTE_ROUTING,
    MP_ROUTE_RUNNING, MP_ROUTE_RETRYING, MP_ROUTE_OK, MP_ROUTE_FAILED
} mp_route_state_t;

typedef struct {
    uint32_t generation;
    mp_net_state_t state;
    int64_t received_unix_s, received_monotonic_ms;
    bool cards_truncated;
    uint32_t total_cards;      /* data.length，不能称全部账号总数 */
    uint8_t card_count;
    mp_card_t cards[MP_MAX_ACCOUNTS];
    int64_t trace_seq, newest_route_id;
    uint32_t active_observed;  /* 最近保留窗口所见，非全局并发保证 */
    mp_route_state_t route_state;
    char model[64];
} mp_snapshot_t;
```

配置/secrets 使用单独 C 模块，结构中不出现 Bearer、Web key、Cookie、OAuth token。完整新响应解析成功后原子替换快照；失败保留 last-good + stale、age，不将空/错误响应显示为“余额0”。network task 向长度1队列 overwrite；UI 比较规范化内容或 generation，只在可见内容改变时刷新。联网错误状态转移建议：

1. `WAIT_WIFI → SYNC_TIME → FETCHING → READY`；掉网转 STALE，重连后重拉额度全快照。trace首次/进程切换/游标异常 `after=0`。
2. `401 → AUTH_REQUIRED`，停止无限自动重登；Web Cookie失效可在仍配置合法Web key时最多受控重登一次。`403` 区分网络共享关闭/权限不足/受限 key拒绝；更换凭据不能解决所有403。
3. `429` 尊重 `Retry-After`，网络/TLS/5xx 使用带 jitter 指数退避；配置错误/证书错误进入明确故障状态，不降级到跳过证书。
4. quota 数据刷新与面板呈现分离；显示“上次成功更新时间/离线/未知/截断”，只按需显示少量账号窗口，翻页是本地显示分页，不等于上游 API 分页。

### 5.4 TLS、JSON、网络调度与能耗

这些是依据 ESP-IDF v6.1 官方能力提出的建议，尚未在本硬件测量：

- 用 C/ESP-IDF `esp_http_client`，`cert_pem`（自建服务CA）或 `crt_bundle_attach`（受信公共CA），按hostname验证证书；Wi-Fi后使用 `esp_netif_sntp_init` / `esp_netif_sntp_sync_wait` 校时再验证有效期。保留 last-good 数据，不能因SNTP失败关闭TLS验证。来源：[HTTP client v6.1](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-reference/protocols/esp_http_client.html)、[system time v6.1](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-reference/system/system_time.html)。
- 需要Cookie时通过 `HTTP_EVENT_ON_HEADER` 逐个接收 `Set-Cookie`，`esp_http_client_set_header` 手工发送；不要假定它自动实现浏览器Cookie jar。原始HTTP流可 `open(0) → fetch_headers → read` 循环；chunked/未知长度不能依赖Content-Length一次性分配。声明有限最大body、header/字符串/嵌套深度、账号数；超限报错并保留旧快照，或采用有记录的过滤策略。不开gzip协商可避免新增解压开销（示例 `Accept-Encoding: identity`）。来源：[HTTP stream 与 events](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-reference/protocols/esp_http_client.html)。
- 25秒长轮询必须显式设置timeout高于25秒，例如35秒是**建议**；v6.1 `timeout_ms=0` 默认5秒，会提前中断。一个handle可顺序复用连接，不可并发perform。单任务长轮询会延后手动额度刷新；可先选无`wait`的短轮询，或设计取消/调度；双handle并行会增加TLS内存峰值。来源：[timeout与handle约束](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-reference/protocols/esp_http_client.html)。
- ESP-IDF6.0起已移除内置 `json` 组件，当前6.1不能假定 `REQUIRES json` 便可用；JSON必须新增显式依赖并锁版本，参见[6.x迁移说明](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/migration-guides/release-6.x/6.0/protocols.html)。若用cJSON，先有界收集body，再parse成堆树，结束 `cJSON_Delete`；这不是增量解析。更大聚合响应应另选并验证token/stream parser，或由适配服务预先瘦身，不能将 `esp_http_client_read` 分块误叫JSON流解析。来源：[Espressif cJSON组件](https://components.espressif.com/components/espressif/cjson)。
- **建议周期**：额度常态60–300秒一次、用户操作触发一次；trace仅在用户打开状态页时使用，先用5–10秒无wait短轮询，屏幕最短10–30秒合并呈现或仅状态变化/手动刷新。它们是第一版功耗/可读性折中，不能当厂商限流允许值或上游既有常量。真实厂商refresh TTL见第4节；不要每轮都调用强制refresh。长轮询保持Wi-Fi并不自动意味着无法modem sleep；modem/auto light sleep与deep sleep策略需一起实测，deep sleep会断网。来源：[Wi-Fi低功耗 v6.1](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-guides/low-power-mode/low-power-mode-wifi.html)。
- 测握手、JSON、JS渲染并发时的internal/PSRAM最低水位、最大连续free block与峰值；8MiB PSRAM不保证TLS所需internal连续块。可用 `heap_caps_get_largest_free_block`、`heap_caps_get_minimum_free_size` 等指标。本地已有调试记录：旧固件Wi-Fi/BLE初始化后64KiB screen task stack装不进internal RAM，才改用PSRAM stack；这更说明TLS余量须上机测。既有验证仅串口boot/render调用，真实AP/BLE完整流程、断电NVS与物理可见屏效果仍待验收，不能称完整联网链路已经验证。来源：[现有验证记录](../provisioning-validation.md#remaining-physical-acceptance)、[stack故障记录](../provisioning-validation.md#L20)。当前报告不承诺精确内存预算、续航或可容纳账号数量。来源：[heap v6.1](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-reference/system/mem_alloc.html)、[本地显示buffer](../../main/note4_presenter.c#L43-L46)。

### 5.5 凭据与缓存保存

仅持久保存配置和最小长期凭据，优先受限网关 key；Cookie可在RAM中重新登录获取，Web key本身是管理secret。HTTP header、URL query、完整JSON、账号邮箱、错误正文不进入串口日志；快照可使用用户设置的短别名。NVS保存应单独namespace/版本/长度校验、避免每次轮询写Flash；现在24KiB NVS不可拿来做路由历史仓库。详情来源见5.1。

未来产品可选择 NVS encryption：`CONFIG_NVS_ENCRYPTION` 及适当key保护方案；flash方案需要至少4KiB的 `nvs_keys` 与flash encryption，HMAC方案另有配置。eFuse、安全启动、flash encryption会改变生产/恢复流程，本次不执行、也不把“建议开启”写成开发版已保护。来源：[NVS encryption v6.1](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-reference/storage/nvs_encryption.html)、[当前未启用说明](../ble-provisioning.md#persistence-and-hardware)。

## 6. 尚未验证与后续验收

- 本报告是固定提交的源码契约，未向任何真实实例验收HTTP状态、反向代理base path、证书链、Cookie flags改写、响应大小和延迟；开始实现前用独立测试实例/合成数据回放确认。
- 所有插件的确切厂商域名、token有效期、额外CSRF/Cookie、分页与限流需按所安装插件的固定版本再查；主仓库不能证明动态扩展全集。
- 凭据有效期由所在层次决定；没有证据的固定小时数/配额刷新SLA不作承诺。空余额、0%、未知、过期、错误必须可区分。
- 断线恢复只能得到仍被保留的route最新对象；不能承诺捕获每个请求/每个中间事件。若产品要审计完整请求，需服务器持久历史方案与隐私设计，超出本终端范围。
- NOTE4 尚未实测TLS/JSON内存峰值、BLE与Wi-Fi并发、429退避、25秒长轮询取消、显示合并/残影、功耗与续航；当前全刷路径可能数秒。
- 当前固件没有完整Magpie配置协议、网络模块或安全存储实现；本文的C结构/周期/状态机是下一步实施草案。

## 7. 额度界面 UI：源码行为与 NOTE4 移植边界

补充研究日期：2026-10-25；仍固定 **`7d67910f74889f7860e53a15fdf45bf1587b1555`**。本次查询 main 指向提交 [`42976f9a082a533995b601e28a01537b8e1c2052`](https://github.com/yetone/magpie/commit/42976f9a082a533995b601e28a01537b8e1c2052)，未将其实现混入本节。以下是定向阅读 HTML、JS、CSS 的静态结论；未运行实例、未实机渲染、未核对官方屏幕截图，不代表亲见的屏幕效果。

### 7.1 入口、卡片与组织方式

主窗口入口为 **Usage → Overview → Allowances**，额度卡在统计汇总之前；Requests 的搜索/账号筛选属于请求账本，不能称为额度卡筛选。托盘则把 **Allowances** 与 **Usage** 分成两个标签，内部 key 分别为 `usage`、`stats`。来源：[页面结构](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/assets/plugins/usage/client.js#L19-L66)、[Overview 标签](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/assets/plugins/usage/page.js#L2200)、[托盘标签](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/assets/index.html#L88-L105)。

桌面按 provider 分卡、同厂商账号分段：卡头为厂商 icon/name，账号优先显示 alias、旁列原账号，`≡ seat` 表示与 key 套餐同席位；plan 旁按 `renew` 显示 Renews / Expires / Until。不是每卡都有“当前账号”徽章；`magpie's own` 是请求归属提示。默认展开最近服务的账号，无记录取首个，其余保留简版条；折叠状态持久保存。余额 key 默认保留至少3个，剩余仅1个时不折叠；账号超过8个时保留首5个及已展开/签到开关所在项。同币种且均可读的 key 才可求和。来源：[卡片主体](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/assets/app.js#L15529-L15820)、[alias/seat](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/assets/plugins/usage/page.js#L351-L376)、[plan 到期](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/assets/app.js#L16635-L16645)。

顺序遵循用户拖动保存的 `usageOrder`，未指定项保持后端顺序，并非按余额动态排序；桌面按约260px最小列宽打包高低不同的卡。托盘沿用顺序，只列展开账号（全折叠仍保留一个），Arrange 可隐藏厂商并调整顺序；这不同于后端授权过滤。来源：[排序/布局](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/assets/app.js#L15863-L15918)、[托盘组织/Arrange](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/assets/app.js#L17081-L17140)。

### 7.2 读数、时间与颜色的真实语义

| UI 内容 | 实际规则与容易误读处 |
| --- | --- |
| Used / Left | 全局切换并保存设置；显示先将 used 钳到0–100，条长度取整、文字可留1位小数。有 `limit>0` 显示原始 amount/limit/unit，Left 改为 limit−amount；否则使用 display。余额始终是剩余，不随切换改变。 |
| 单/多窗口、共享池 | 每窗口一条；family 汇总取使用最多的模型，同值取较早重置者。共享池按 pool 展示5h/周等窗口，Every model 可展开成员；不能相加成“账号总百分比”。 |
| Unlimited | 明文 Unlimited，桌面不画有限条；托盘过滤无限窗口，若全为无限则只写 Unlimited。 |
| 重置与 pace | 同时显示本地日期/时钟和分钟、小时、天粒度的相对时间；过时写 Reset time passed。Pace 是 used 与 startsAt→resetsAt 均匀预算的百分点差，Left 模式反号，缺起止时间不显示。 |
| 到期与预测 | `runsOutAt/runsOut` 是一批额度何时到期及数量；“Runs out … at this pace”另由历史拟合得来，至少3点且跨度1小时，最多取最近一周的连续下降段。余额预测使用 `balanceTrend`，不是解析 balance 字符串自行推算。 |
| held / holds | `held` 是 Codex app 当前不再发送请求的后端事实；本次所读额度卡渲染器未直接消费它，不能编造通用 held 徽章。`holds` 则是按 Magpie 已路由调用估算整个窗口可容纳 token/标价成本，UI 写 Whole ≈；不足时写 not enough data，并在 tooltip 解释原因。账号 cap 的 held 提示在账号编辑器中另算。 |

前四项来源：[百分比/数量/family/pool](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/assets/app.js#L13970-L14175)、[桌面窗口渲染与 tooltip](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/assets/app.js#L18251-L18349)、[托盘窗口](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/assets/app.js#L17287-L17369)。预测来源：[额度拟合](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/assets/app.js#L16277-L16315)、[余额预测](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/assets/app.js#L16499-L16559)。held/holds 来源：[后端 Held 定义](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/subscription_usage.go#L166-L169)、[Whole 提示](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/assets/app.js#L18190-L18225)、[cap 提示](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/assets/app.js#L13700-L13752)。

**90%预警有布局差异**：托盘环及余额百分比 meter 在 used/share≥90 时加 `full`，CSS 改琥珀色；桌面普通 `quotaWindows` 未加该 class，不能写成“所有额度条到90%自动变黄”。普通条默认强调色，error 使用错误样式。hover/title 承载精确重置时间、模型成员、原始错误和估算前提。趋势图的大图才画 `qc-now` 当前时刻竖虚线，小 spark 不画；它不是普通进度条的时间刻度。来源：[阈值与托盘 title](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/assets/app.js#L17343-L17361)、[余额 meter](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/assets/app.js#L18032-L18039)、[条样式](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/assets/app.css#L917-L957)、[time marker](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/assets/app.js#L16358-L16388)。

### 7.3 初载、空、错误与旧数据

初载桌面/托盘各两张 skeleton；返回空数组时桌面隐藏额度区，托盘隐藏 Allowances 标签并退回 Agents。单卡 error 显示简化原因、原文放 title，插件错误首行会直接显示；`needsAccessKey` 在错误分支提供 Open settings，`seeAt` 提供 Open the console。有效余额但窗口读取失败时，桌面可同时显示余额和错误。整页网络失败保留已有数据，首次失败则退成空集合，**该 catch 没有自行给每张卡追加 stale 标记或专用全页错误面板**。来源：[初载/空](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/assets/app.js#L15544-L15582)、[加载失败](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/assets/app.js#L15408-L15437)、[托盘空态](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/assets/app.js#L17041-L17073)、[错误文案](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/assets/app.js#L13386-L13450)及7.2窗口渲染引用。

`asOf` 显示旧值时间，并区分 readDeferred 的“本次未刷新”和读取失败；缓存窗口已过重置时间则明确“当前额度未知”。只有 `readDeferred` 而没有 asOf，不会凭空出现独立 deferred 徽章。正常 readAt 显示 Updated …，每30秒更新年龄文本。`lapsed` 属于 Login/账号编辑器，那里显示 Sign-in required、Sign in again/Remove；额度页主要根据 error 文本展示，不直接读取 Login.lapsed。来源：[asOf](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/assets/app.js#L17272-L17279)、[readAt](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/assets/app.js#L18042-L18065)、[账号状态与操作](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/assets/app.js#L12954-L13105)。

### 7.4 自动读取、刷新与写操作

Overview 自动更新可选 Off/5/10/30/60秒、默认5秒，但额度的普通读另按约60秒调度；页面隐藏或弹出菜单时跳过定时刷新。打开额度/恢复窗口焦点是 asked read，30秒内重复打开合并；手动 Refresh now 仍发 asked read。前端读取 **HTTP响应头** `X-Magpie-Reading`，仅其字符串值等于 `"1"` 时将本地 `reading` 布尔值置真，再每1.5秒追读、最多20次；响应相同则避免重画。这不是要求客户端发送同名请求头，也不是 JSON 布尔字段；第4节的同名请求头用途须另行区分。上述是前端调度，厂商缓存/Claude特殊节流仍见4.5。来源：[自动调度/刷新菜单](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/assets/plugins/usage/page.js#L3505-L3615)、[打开节流/追读](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/assets/app.js#L15383-L15440)。

单卡 hover 刷新按钮按 provider/user 发 POST，busy 时防重入；响应虽含全部卡，通常只替换目标卡，并保护它不被旧整页 GET 覆盖。账号登录/Use/Make first/开关在账号编辑器；卡片折叠不是账号切换。Codex Use a reset 先弹不可撤销确认，再消费；签到显示成功/不可参与/验证码/须原厂app/失败等状态，并提供 Auto check-in 和适用账号集合的 Check in now。它们都是写操作，不能当刷新捷径。来源：[单卡刷新/并发保护](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/assets/app.js#L18067-L18156)、[签到/reset UI](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/assets/app.js#L17750-L17925)及7.3账号操作引用。

### 7.5 desktop / tray / web 外观边界

三种布局共用源码：desktop 是多列额度条卡；tray 是紧凑厂商分组、数字环与余额网格，并省略简版账号；web 在≤760px或粗指针且≤1024px时改触控布局，控件放大、导航横向滚动。默认系统UI字体13px/1.4；Windows另有微软雅黑UI等字体栈。主题按 URL、已保存主题、系统配色选择浅/深；强调色浅色 `#4f46e5`、深色 `#a5b4fc`。页面淡入/上移、骨架闪动、刷新旋转是浏览器动画；reduced-motion 会减弱或停用。不能把440 CSS px托盘直接等比缩到400物理像素屏。来源：[桌面卡样式](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/assets/app.css#L834-L844)、[触控布局](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/assets/app.css#L3636-L3697)、[字体/色彩](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/assets/app.css#L2-L122)、[主题](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/assets/theme.js#L1-L13)、[减弱动画](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/assets/app.css#L3622-L3634)、[托盘基准尺寸](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/gui/textsize.go#L5-L40)。

### 7.6 对400×300 NOTE4的建议（尚未实现）

- **保留语义**：精简v1足够展示账号/plan、alias/seat、窗口名、used或remaining、amount/limit/unit、pool、unlimited、resetsAt、balance/balanceSplit及error/readAt/asOf/readDeferred。alias/seat 已是可选字段，无需为短别名另取cards；来源：[Quota 字段](https://github.com/yetone/magpie/blob/7d67910f74889f7860e53a15fdf45bf1587b1555/internal/provider/quotas.go#L77-L115)。全局固定 Left 可减少认知切换；未知、零、无限、旧值分开，绝不把旧窗口到期解释为已恢复满额。
- **需要cards或适配字段**：icon、family成员、startsAt算Pace、runsOutAt额度到期、needsAccessKey修复提示、checkins/reset策略、balanceTrend均超出精简v1现有字段；历史耗尽预测还需history及拟合。holds虽可出现在v1窗口，受限key会移除，不能承诺总能展示。若要显示held，须额外定义说明并承认它不是上游额度卡现有徽章。字段边界依据4.2–4.3及本节源码。
- **稳定分页与按键**：建议一页1账号、2–3窗口，其余详情分页；页首厂商/短别名/页码，中部窗口名与剩余数字，页尾重置时间、更新时间或故障文字。保持稳定顺序、按卡ref保持选择，更新额度不自动换到别卡。UP/DOWN翻页，OK进详情/动作菜单；长DOWN保留现有配网入口，现状没有长OK事件。依据：[按钮](../../main/button_input.c#L9-L52)、[已有状态驱动布局](../../app.tsx#L35-L67)。
- **避免照搬交互**：hover信息改成可用OK进入的详情；不复刻拖动、悬浮刷新、环形切换、秒级倒计时、闪动和曲线放大。现有4bpp路径全刷数秒，只在可见文本/状态变化或翻页时呈现；不能每次poll或generation变化都全刷。红绿/琥珀会转亮度，须加“低/耗尽/待登录”等文字，不能只靠颜色。动态中文账号仍受烘焙字形限制，优先可控短别名和缺字降级。依据：[全灰度区域](../../main/note4_screen.c#L126-L144)、[显示转换](../../main/display_adapter.c#L3-L10)及5.1字体/刷新证据。

未确认项：真实浏览器换行、触控可达性、主题/平台差异和动画效果未渲染验收；未核官方截图与该SHA是否一致；NOTE4字号可读性、每页窗口数、全刷延迟/残影须后续实机测试。本节未新增UI、适配服务或设备网络能力。

