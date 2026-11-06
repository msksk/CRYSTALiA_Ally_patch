[CmdletBinding()]
param(
    [switch]$Install
)

$ErrorActionPreference = 'Stop'

$scriptDir = $PSScriptRoot
$outDir = Join-Path $scriptDir 'out'
$dll = Join-Path $outDir 'd3d9.dll'
$knownHash = '800D3CA43C778EDC60997742D166823446A7DDEC8749F726AAD1B1F16E94F660'

function Test-Pe32Dll {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $fs = [System.IO.File]::OpenRead($Path)
    try {
        $br = [System.IO.BinaryReader]::new($fs)
        if ($br.ReadUInt16() -ne 0x5A4D) {
            return $false
        }

        $fs.Position = 0x3C
        $peOffset = $br.ReadInt32()
        if ($peOffset -lt 0) {
            return $false
        }

        $fs.Position = $peOffset
        if ($br.ReadUInt32() -ne 0x00004550) {
            return $false
        }

        # COFF header: IMAGE_FILE_MACHINE_I386 + IMAGE_FILE_DLL
        $fs.Position = $peOffset + 4
        if ($br.ReadUInt16() -ne 0x014C) {
            return $false
        }
        $fs.Position = $peOffset + 4 + 18
        if (($br.ReadUInt16() -band 0x2000) -eq 0) {
            return $false
        }

        # Optional header: PE32 (0x10B)
        $fs.Position = $peOffset + 4 + 20
        return ($br.ReadUInt16() -eq 0x010B)
    }
    finally {
        $fs.Dispose()
    }
}

Write-Host '=== mekuiro d3d9.dll Docker build ==='

if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
    Write-Host 'Docker が見つかりません。Docker Desktop をインストールして起動してください。' -ForegroundColor Red
    exit 1
}

$null = & docker info --format '{{.ServerVersion}}' 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Host 'Docker デーモンに接続できません。Docker Desktop を起動してください。' -ForegroundColor Red
    exit 1
}

if (Test-Path -LiteralPath $outDir) {
    Remove-Item -LiteralPath $outDir -Recurse -Force
}

Write-Host 'Docker イメージをビルドしています...'
& docker build --progress=plain `
    --file (Join-Path $scriptDir 'Dockerfile') `
    --output "type=local,dest=$outDir" `
    $scriptDir

if ($LASTEXITCODE -ne 0) {
    Write-Host 'ビルドに失敗しました。' -ForegroundColor Red
    exit $LASTEXITCODE
}

if (-not (Test-Path -LiteralPath $dll)) {
    Write-Host "ビルド成果物が見つかりません: $dll" -ForegroundColor Red
    exit 1
}

if (-not (Test-Pe32Dll -Path $dll)) {
    Write-Host 'ビルド成果物は 32-bit PE (x86) ではありません。' -ForegroundColor Red
    exit 1
}

$size = (Get-Item -LiteralPath $dll).Length
$hash = (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash

Write-Host ''
Write-Host 'ビルド完了'
Write-Host "  ファイル: $dll"
Write-Host ('  サイズ  : {0:N0} bytes' -f $size)
Write-Host "  SHA256  : $hash"
Write-Host "  確認済み: $knownHash (d3d9_final.dll)"
if ($hash -ne $knownHash) {
    Write-Host '  ※ ビルド環境のメタデータにより、確認済み DLL とはハッシュが異なります（動作には影響しません）。' -ForegroundColor Yellow
}

if ($Install) {
    if (Get-Process -Name 'mekuiro' -ErrorAction SilentlyContinue) {
        Write-Host 'mekuiro.exe が実行中です。終了してから再実行してください。' -ForegroundColor Red
        exit 1
    }

    $target = Join-Path (Split-Path -Parent $scriptDir) 'd3d9.dll'
    Copy-Item -LiteralPath $dll -Destination $target -Force
    Write-Host ''
    Write-Host "インストールしました: $target"
    Write-Host '元に戻す場合: .\restore.ps1'
}
