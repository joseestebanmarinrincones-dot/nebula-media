param(
    [string]$Version = ''
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

if (-not $Version) {
    $Version = (Get-Content (Join-Path $root 'VERSION') -Raw).Trim()
}
if ($Version -notmatch '^\d+\.\d+\.\d+$') {
    throw "Version invalida: '$Version'."
}

$exe = Join-Path $root 'dist\NebulaMedia.exe'
if (-not (Test-Path $exe)) {
    Write-Host 'Falta el ejecutable; compilando primero...'
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'build.ps1') -Version $Version
    if ($LASTEXITCODE -ne 0) { throw 'La compilacion fallo.' }
}

function Find-Makensis {
    $candidates = @()
    $cmd = Get-Command makensis.exe -ErrorAction SilentlyContinue
    if ($cmd) { $candidates += $cmd.Source }
    $candidates += (Join-Path $root 'tools\nsis\makensis.exe')
    $candidates += (Join-Path $env:TEMP 'nsis-3.11\nsis-3.11\makensis.exe')
    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path $candidate)) { return $candidate }
    }
    $existing = Get-ChildItem -Path (Join-Path $root 'tools') -Filter makensis.exe -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($existing) { return $existing.FullName }
    return $null
}

$makensis = Find-Makensis
if (-not $makensis) {
    $toolsDir = Join-Path $root 'tools'
    $nsisDir = Join-Path $toolsDir 'nsis'
    New-Item -ItemType Directory -Force -Path $nsisDir | Out-Null
    $zip = Join-Path $toolsDir 'nsis-3.11.zip'
    if (-not (Test-Path $zip)) {
        Write-Host 'Descargando NSIS 3.11...'
        $url = 'https://master.dl.sourceforge.net/project/nsis/NSIS%203/3.11/nsis-3.11.zip?viasf=1'
        if (Get-Command curl.exe -ErrorAction SilentlyContinue) {
            & curl.exe -sL --max-time 180 -o $zip $url
            if ($LASTEXITCODE -ne 0) { throw 'No se pudo descargar NSIS.' }
        } else {
            Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing -TimeoutSec 180
        }
    }
    Write-Host 'Extrayendo NSIS...'
    Expand-Archive -Path $zip -DestinationPath $nsisDir -Force
    $makensis = (Get-ChildItem -Path $nsisDir -Filter makensis.exe -Recurse | Select-Object -First 1).FullName
}
if (-not $makensis) { throw 'No se encontro makensis.exe.' }

Write-Host "Usando $makensis"
New-Item -ItemType Directory -Force -Path (Join-Path $root 'dist') | Out-Null
Push-Location (Join-Path $root 'installer')
try {
    & $makensis /V2 "/DAPPVERSION=$Version" nebula.nsi
    if ($LASTEXITCODE -ne 0) { throw 'NSIS fallo al crear el instalador.' }
} finally {
    Pop-Location
}

$setup = Join-Path $root "dist\NebulaMedia-Setup-$Version.exe"
if (-not (Test-Path $setup)) { throw 'No se genero el instalador.' }

# ZIP portable, util para probar sin instalar.
$portable = Join-Path $root "dist\NebulaMedia-Portable-$Version.zip"
if (Test-Path $portable) { Remove-Item $portable -Force }
Compress-Archive -Path @(
    (Join-Path $root 'dist\NebulaMedia.exe'),
    (Join-Path $root 'LEEME.txt'),
    (Join-Path $root 'LICENSE.txt')
) -DestinationPath $portable -CompressionLevel Optimal

$installerHash = (Get-FileHash $setup -Algorithm SHA256).Hash
$portableHash = (Get-FileHash $portable -Algorithm SHA256).Hash
$hashes = @(
    "$installerHash  NebulaMedia-Setup-$Version.exe",
    "$portableHash  NebulaMedia-Portable-$Version.zip"
) -join "`r`n"
Set-Content -Path (Join-Path $root 'dist\SHA256SUMS.txt') -Value $hashes -Encoding ASCII

Write-Host ''
Write-Host "Instalador: $setup"
Write-Host "Portable:   $portable"
Write-Host "SHA256:     dist\SHA256SUMS.txt"
