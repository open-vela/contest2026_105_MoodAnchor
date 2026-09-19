# 腾讯云 SCF Web 函数部署

该方案运行现有 `backend/server.py`，仅做 HTTP 转发、评审访问控制和 Coze 流式响应收集；不运行或租用 GPU 模型。腾讯云按请求启动函数，电脑和 AutoDL 不需要保持运行。

## 1. 创建部署包

在 `backend/tencent_scf/` 运行：

```powershell
.\build_package.ps1
```

生成的 `MoodAnchor-tencent-scf.zip` 只含 `server.py` 与 `scf_bootstrap`，不含环境变量或凭据；脚本会保留 `scf_bootstrap` 的 Linux 可执行权限。提交时请使用此 ZIP。

## 2. 腾讯云控制台配置

1. 完成腾讯云实名认证并开通 **云函数 SCF**。
2. 新建 **Web 函数**，地域优先广州或上海，运行环境选 **Python 3.10**；内存 256 MB，超时 90 秒；不选 GPU、VPC、数据库、存储或预置并发。
3. 上传上述 ZIP；不要手动填写 Python 执行方法。Web 函数会使用 ZIP 内的 `scf_bootstrap` 启动脚本；该脚本会自行定位同目录的 `server.py` 并监听 `0.0.0.0:9000`。
4. 选择“默认创建”触发器，获得平台提供的 HTTPS URL；不要升级到标准 API 网关。
5. 在函数环境变量填写下列值。不要把 Key 写入 ZIP、Git、APK、截图或聊天记录：

```text
COZE_API_KEY=<仅有 chat 最小权限的 Coze Key>
COZE_BOT_ID=<已发布 API 的 Bot ID>
COZE_API_BASE=https://api.coze.cn
DEFAULT_PROVIDER=coze
PORT=9000
RATE_LIMIT=10
REQUIRE_DEMO_TOKEN=true
DEMO_ACCESS_TOKEN=<至少 32 位、单独生成的随机评审访问码>
```

`DEMO_ACCESS_TOKEN` 不是 Coze Key。它用于拦住不知道评审访问码的请求；每分钟限流也不能替代 Coze 侧的额度/预算限制。

## 3. 验收

先通过函数 URL 请求 `GET /health`。随后以无敏感内容测试：

```powershell
$reviewToken = "<评审访问码>"
Invoke-RestMethod -Method Post -Uri "https://<函数URL>/chat" `
  -Headers @{ Authorization = "Bearer $reviewToken"; "Content-Type" = "application/json" } `
  -Body '{"message":"你好，请用一句话回应。","provider":"coze","client_id":"review-device-2026"}'
```

预期：未带访问码为 `401`，配置遗漏为 `503`，模型限流/额度问题为 `429`，成功时才有 `reply`。必须在手机移动数据下重复测试，并关闭电脑 SSH、AutoDL 后再认定可供评审体验。

当前评审版 App 已内置腾讯云 HTTPS 地址和评审访问码，并保留服务配置入口。访问码可从公开源码或 APK 获取，不能作为保密凭据；模型 Key 仍只在服务端保存。云端启动脚本修复不需要重新安装 APK。

启动脚本使用腾讯云 Python 3.10 对应的 `/var/lang/python310/bin/python3.10`，启动前检查 SSL 和 HTTPSHandler，并在日志中输出解释器版本及 OpenSSL 版本（不输出凭据）。若遇到 `<urlopen error unknown url type: https>`，末尾 `>` 是异常字符串的结束符，不属于 URL；应检查实际解释器的 SSL/HTTPS 支持。不要通过改用 HTTP 或关闭证书校验绕过问题。

## 运行与费用边界

函数空闲时不持续占用一台机器，但 SCF 用量、响应流量、日志以及 Coze 模型调用均可能产生费用。默认 API 网关调用次数不单独计费，但 Web 函数响应流量仍按平台规则计算。演示结束后删除函数 URL 或将 `REQUIRE_DEMO_TOKEN=true` 但撤销/更换访问码，并在 Coze 侧撤销 Key。

参考：[腾讯云 Web 函数](https://cloud.tencent.com/document/product/583/56124)、[启动与 9000 端口](https://cloud.tencent.com/document/product/583/56126)、[Web 函数计费](https://cloud.tencent.com/document/product/583/66237)。
