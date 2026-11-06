# 元に戻す: ゲームフォルダの d3d9.dll を削除します。
$game = Split-Path -Parent $PSScriptRoot
$dll = Join-Path $game 'd3d9.dll'

if (-not (Test-Path -LiteralPath $dll)) {
    Write-Host "d3d9.dll はありません（既に元の状態です）。"
    exit 0
}

$ans = Read-Host "d3d9.dll を削除して元に戻します。よろしいですか? (y/N)"
if ($ans -ne 'y') {
    Write-Host "中止しました。"
    exit 0
}

Remove-Item -LiteralPath $dll -Force
Write-Host "削除しました: $dll"
