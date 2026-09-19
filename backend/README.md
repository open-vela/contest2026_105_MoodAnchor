## MoodAnchor 云端对话服务

本次提交 APK 已内置腾讯云地址和公开评审访问码，已完成手机实际对话验证。不依赖 AutoDL 或电脑 SSH 中转的部署说明见 [tencent_scf/README.md](tencent_scf/README.md)。Cloudflare Workers 实现保留于 [cloudflare/README.md](cloudflare/README.md)，但其默认域名在本项目测试网络无法访问，不作为本次评审入口。服务在线状态及模型额度不保证持续可用。

这是手机 App 与云端大语言模型之间的轻量中转服务，使用 Python 标准库，无需 `pip install`。

```text
Android App → MoodAnchor 后端（你们部署） → Coze / SiliconFlow 等模型服务
              ↑ 真实 API Key 仅在这里
```

App 只请求本服务的 `/chat` 接口；真实模型 Key 不写入 Android 源码、APK、Git 仓库、演示视频或截图。

在部署环境中设置真实值；`.env.example` 仅用于查看变量名称，不会被本服务
自动加载。若使用 `.env` 文件，请由部署工具加载为进程环境变量，且 `.env`
不可提交。服务端不会在源代码或 APK 中保存真实密钥。

必需环境变量：

- `COZE_API_KEY`：扣子个人访问令牌，至少授予 chat 权限。
- `COZE_BOT_ID`：已发布为 API 服务的智能体 ID。
- 对外提供评审服务时：`REQUIRE_DEMO_TOKEN=true`、`DEMO_ACCESS_TOKEN` 为独立随机访问码。访问码不是模型 Key；请求需带 `Authorization: Bearer <访问码>`。

按需启用 Qwen 时还需设置 `SILICONFLOW_API_KEY`；其他可选项为 `QWEN_MODEL`、
`COZE_API_BASE`、`DEFAULT_PROVIDER`、`PORT` 和 `RATE_LIMIT`。

健康检查：GET /health
聊天接口：POST /chat，JSON：{"message":"你好","provider":"coze","client_id":"review-device-2026"}

注意：正式服务应在前面配置 HTTPS、持久化限流和用户鉴权。Python 内存限流只适用于单个常驻进程；腾讯云无状态函数应同时依赖平台/Coze 额度控制，不能把内存限流解释为全局预算上限。

## 开源边界

`server.py`、部署说明和 `.env.example` 可以公开提交。真正的 `COZE_API_KEY`、
`SILICONFLOW_API_KEY`、服务器登录密码、证书及 `.env` 文件必须保留在部署者自己的
环境中；即使仓库完全开源，也不能把这些凭据一同发布。
