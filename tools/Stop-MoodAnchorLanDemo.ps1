param([int]$Port = 6006)

$ruleName = "MoodAnchor LAN Demo $Port"
$rule = Get-NetFirewallRule -DisplayName $ruleName -ErrorAction SilentlyContinue
if ($rule) {
    Remove-NetFirewallRule -DisplayName $ruleName
    Write-Host "已删除临时防火墙规则：$ruleName" -ForegroundColor Green
} else {
    Write-Host "未找到规则：$ruleName"
}
