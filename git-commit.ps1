param([Parameter(Mandatory=$false)][string]$Content = "自动提交")

$ts = Get-Date -Format "yyyyMMdd-HHmmss"
$msg = "$ts $Content"
git add -A
git commit -m $msg