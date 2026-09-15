param(
    [string]$ComputerIp,
    [int]$Port = 6006
)

$ruleName = "MoodAnchor LAN Demo $Port"

function Test-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not (Test-Administrator)) {
    Write-Host "正在请求管理员权限，用于仅放行 TCP 端口 $Port …" -ForegroundColor Yellow
    $arguments = "-NoExit -ExecutionPolicy Bypass -File `"$PSCommandPath`" -Port $Port"
    if ($ComputerIp) { $arguments += " -ComputerIp $ComputerIp" }
    Start-Process powershell.exe -Verb RunAs -ArgumentList $arguments
    exit
}

if ([string]::IsNullOrWhiteSpace($ComputerIp)) {
    $ComputerIp = Read-Host "请输入电脑 WLAN 的 IPv4 地址（例如 192.168.87.164）"
}

$parsedIp = $null
if (-not [System.Net.IPAddress]::TryParse($ComputerIp, [ref]$parsedIp) -or $parsedIp.AddressFamily -ne [System.Net.Sockets.AddressFamily]::InterNetwork) {
    Write-Error "这不是有效的 IPv4 地址：$ComputerIp"
    exit 1
}

$localIps = Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
    Where-Object { $_.IPAddress -notlike '127.*' } |
    Select-Object -ExpandProperty IPAddress
if ($localIps -notcontains $ComputerIp) {
    Write-Warning "输入的地址不在当前电脑网卡列表中：$ComputerIp"
    Write-Host "当前可用 IPv4：$($localIps -join ', ')"
    $continue = Read-Host "仍继续放行端口吗？(y/N)"
    if ($continue -notmatch '^[Yy]$') { exit }
}

$existing = Get-NetFirewallRule -DisplayName $ruleName -ErrorAction SilentlyContinue
if ($existing) {
    Set-NetFirewallRule -DisplayName $ruleName -Enabled True -Action Allow -Profile Any | Out-Null
} else {
    New-NetFirewallRule -DisplayName $ruleName -Direction Inbound -Protocol TCP -LocalPort $Port -Action Allow -Profile Any | Out-Null
}

Write-Host "`n已放行本机 TCP $Port。" -ForegroundColor Green
Write-Host "请在另一个 PowerShell 窗口手动运行 SSH 隧道："
Write-Host ('ssh -g -N -L 0.0.0.0:{0}:127.0.0.1:{0} -p 11864 root@connect.nmb1.seetacloud.com' -f $Port) -ForegroundColor Cyan
Write-Host "输入密码后保持该窗口打开。"

$tunnelReady = Test-NetConnection -ComputerName 127.0.0.1 -Port $Port -InformationLevel Quiet
if ($tunnelReady) {
    Write-Host "`n本地 SSH 隧道端口已连通。" -ForegroundColor Green
} else {
    Write-Host "`n尚未检测到本地隧道。启动 SSH 后，可重新运行本脚本验证。" -ForegroundColor Yellow
}

Write-Host ('手机测试地址：http://{0}:{1}/health' -f $ComputerIp, $Port) -ForegroundColor Cyan
