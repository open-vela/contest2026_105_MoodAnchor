MoodAnchor 局域网 Qwen 演示脚本

1. 在 AutoDL 服务器保持 Qwen 后端运行在 6006。
2. 在电脑 PowerShell 运行 Start-MoodAnchorLanDemo.ps1。
   - 脚本会请求管理员权限，只放行 TCP 6006。
   - 电脑 IPv4 由操作者手动输入。
3. 在另一个 PowerShell 手动运行 SSH 隧道（密码由操作者手动输入）：
   ssh -g -N -L 0.0.0.0:6006:127.0.0.1:6006 -p 11864 root@connect.nmb1.seetacloud.com
4. 手机与电脑同一局域网，在浏览器访问 http://电脑IPv4:6006/health。
5. 演示结束可运行 Stop-MoodAnchorLanDemo.ps1 删除临时防火墙规则。

注意：该脚本不能绕过校园网客户端隔离；遇到隔离时请使用电脑热点或 USB 网络共享。
