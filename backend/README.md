MoodAnchor Coze 后端（Python 标准库，无需 pip install）

部署前复制 `.env.example` 为本机 `.env`，并在部署环境中设置真实值；`.env`
不可提交。服务端不会在源代码或 APK 中保存真实密钥。

必需环境变量：

- `COZE_API_KEY`：扣子个人访问令牌，至少授予 chat 权限。
- `COZE_BOT_ID`：已发布为 API 服务的智能体 ID。

按需启用 Qwen 时还需设置 `SILICONFLOW_API_KEY`；其他可选项为 `QWEN_MODEL`、
`COZE_API_BASE`、`DEFAULT_PROVIDER`、`PORT` 和 `RATE_LIMIT`。

健康检查：GET /health
聊天接口：POST /chat，JSON：{"message":"你好","user_id":"设备随机ID"}

注意：正式服务应在前面配置 HTTPS、持久化限流和用户鉴权。
