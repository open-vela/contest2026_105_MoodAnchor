# Cloudflare Workers 试用后端

用于验证 `手机 → Cloudflare Worker → Coze`，不经过开发者电脑、SSH 或 AutoDL。
2026-09-17 已完成代码、本地模拟测试、Worker 本地运行检查和首次云端部署。
试用地址：`https://moodanchor-coze-demo.moodanchor-worker.workers.dev`。
当前 `DEMO_ENABLED=false`，尚未配置模型凭据；本机访问该域名出现 TLS 握手失败，
因此**公网可访问性、真实模型调用及移动数据网络体验尚未通过验证**。
该实现仅支持 Coze，不托管大模型，且不作为本次评审入口。

## 部署

在本目录使用 Node.js 22+ 和 pnpm：

```powershell
pnpm install --frozen-lockfile
pnpm test
pnpm exec wrangler login
pnpm exec wrangler deploy
```

仅使用账号已具备的免费计划；不需要为此次测试购买 GPU、域名或升级付费计划。
部署默认 `DEMO_ENABLED=false`，`/chat` 会拒绝请求；`/health` 只证明 Worker 存活，不能证明模型可用。
如账号已有同名 Worker，应先改 `wrangler.jsonc` 的 `name`，不要覆盖其他服务。

配置以下 Secrets（命令会提示输入；不要将值作为命令行参数或写进源码）：

```powershell
pnpm exec wrangler secret put COZE_API_KEY
pnpm exec wrangler secret put COZE_BOT_ID
pnpm exec wrangler secret put DEMO_ACCESS_TOKEN
```

也可在该 Worker 的 Settings → Variables and Secrets 页面配置为 Secret。
`DEMO_ACCESS_TOKEN` 是另外生成的随机评审访问码，不是 Coze Key；建议至少 32 个随机字符，仅发给测试者。
Coze Bot 需发布到 API 渠道，Key 需要必要的 chat 权限。Bot 自身提示词及工作流仍需在 Coze 配置。
确认 Coze 预算/额度限制和访问码都已设置后，将 `wrangler.jsonc` 中 `DEMO_ENABLED` 改为 `true`，再部署。
暂停试用时改回 `false` 并部署。若通过控制台改变量，注意后续部署以本文件为准。

## 接口与 App 接入

`POST https://<实际部署域名>/chat`，请求头：

```text
Content-Type: application/json
Authorization: Bearer <评审访问码，不是模型密钥>
```

请求示例（不包含任何真实凭据）：

```json
{"message":"你好，请用一句话回应。","provider":"coze","client_id":"random-device-uuid","new_conversation":true}
```

成功响应保留旧 App 的 `reply / provider / bot_id / conversation_id` 字段。
后续消息原样回传 `conversation_id` 并省略 `new_conversation`，即可续接。
此版本的 `conversation_id` 是绑定 client_id 的签名令牌，24 小时过期，不是原始 Coze 会话 ID。
不支持迁移旧 Python 后端会话；切换服务后需要开始新会话。轮换 Coze Key 也会使已有令牌失效。

当前评审 APK 默认连接腾讯云，已支持服务地址、访问码设置及 Authorization 请求头。若切换到自行部署的 Worker，请先确认网络可达与真实对话通过，再在 App 设置中更换地址和对应访问码；保存设置会开始新会话。
腾讯云评审访问码已随源码和 APK 公开，不是保密凭据；模型密钥仍只存于服务端。当前测试网络无法访问 workers.dev 默认域名，因此本 Worker 不作为当前评审入口。

## 验收（需要真实部署后执行）

1. 检查 `/health`，确认 Worker 地址可访问。
2. 不带访问码访问 `/chat`，应为 401（服务关闭时为 503），不得消耗模型调用。
3. 带正确访问码发送一条无敏感信息的消息，验证真实回复。
4. 使用返回的会话令牌发第二条消息，验证上下文；退出后开启新会话，验证隔离。
5. 关闭 AutoDL 和电脑 SSH 转发，在手机移动数据网络重复测试。
6. 再测试校园网/其他 Wi-Fi，不将单一网络成功视为全国网络均可用。

本地 `pnpm test` 使用模拟响应，不消耗模型额度，也不能替代以上真实验收。

## 限制与隐私

- Cloudflare 免费额度与 Coze 模型额度是两回事；不保证两者持续充足，也不保证网络持续可达。
- 内置限流为共用评审访问码每个 Cloudflare 位置约 10 次/分钟；它最终一致，**不是全球硬预算上限**。使用 Coze 可用的预算限制、最小权限 Key 和定期停用措施控制支出，不公开访问码。
- 限流绑定 `namespace_id=2026105` 应在你的账号内独立使用，避免与其他 Worker 的限流计数混用。
- 75 秒上游等待上限、16 KiB 请求上限和 1 MiB 回复流上限；错误、断流或空响应不会伪装成模型回复。
- 不自动重试模型请求，避免超时重试导致重复计费；连接取消不保证上游已经停止计费。
- 不在 Worker 日志记录聊天正文、密钥或上游错误原文，也不建立本地聊天数据库。
- 聊天内容会经 Cloudflare 传至 Coze；为了续接对话，调用使用 `auto_save_history=true`。Coze 会保存相关会话；客户端退出不等于删除云端记录。避免输入敏感信息。
- 这是受访问码保护的小范围试用后端，不是具备用户账户、逐用户计费及全球预算控制的正式服务。

参考：[Secrets](https://developers.cloudflare.com/workers/configuration/secrets/)、[限流及其边界](https://developers.cloudflare.com/workers/runtime-apis/bindings/rate-limit/)、[Coze 对话 API](https://docs.coze.cn/developer_guides_chat_v3)。
