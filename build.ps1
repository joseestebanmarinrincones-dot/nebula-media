param(
    [string]$Version = ''
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

$versionFile = Join-Path $root 'VERSION'
if (-not $Version) {
    $Version = (Get-Content $versionFile -Raw).Trim()
}
if ($Version -notmatch '^\d+\.\d+\.\d+$') {
    throw "Version invalida: '$Version'. Usa el formato 1.2.3."
}
Set-Content -Path $versionFile -Value $Version -Encoding ASCII

$parts = $Version.Split('.')
$major = [int]$parts[0]
$minor = [int]$parts[1]
$patch = [int]$parts[2]

$gpp = (Get-Command g++ -ErrorAction Stop).Source
$windres = (Get-Command windres -ErrorAction Stop).Source

New-Item -ItemType Directory -Force -Path build, dist, 'build\generated' | Out-Null

Write-Host "Version: $Version"
$versionHeader = @"
#pragma once
#define NEBULA_VERSION_MAJOR $major
#define NEBULA_VERSION_MINOR $minor
#define NEBULA_VERSION_PATCH $patch
#define NEBULA_VERSION_W L"$Version"
#define NEBULA_VERSION_A "$Version"
"@
Set-Content -Path 'build\generated\version.h' -Value $versionHeader -Encoding UTF8

$versionRc = @"
#define NEBULA_RC_FILEVERSION $major,$minor,$patch,0
#define NEBULA_RC_VERSION_STR "$Version.0"
#define NEBULA_RC_VERSION_SHORT "$Version"
"@
Set-Content -Path 'build\generated\version.rcinc' -Value $versionRc -Encoding ASCII

Write-Host "Generando recursos..."
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'tools\make_assets.ps1')
if ($LASTEXITCODE -ne 0) { throw 'No se pudieron generar los recursos.' }

$sources = @('main', 'app', 'paint', 'util', 'audio', 'thumbs', 'dnd', 'update')
$common = @(
    '-std=c++20',
    '-O2',
    '-DUNICODE',
    '-D_UNICODE',
    '-Ibuild\generated',
    '-Isrc',
    '-finput-charset=UTF-8',
    '-fexec-charset=UTF-8'
)


foreach ($name in $sources) {
    Write-Host "Compilando $name.cpp..."
    & $gpp @common -c "src\$name.cpp" -o "build\$name.o"
    if ($LASTEXITCODE -ne 0) { throw "Error compilando $name.cpp" }
}

Write-Host "Compilando recursos..."
& $windres -I build\generated -I res res\app.rc -O coff -o build\res.o
if ($LASTEXITCODE -ne 0) { throw 'Error compilando los recursos.' }

$objects = $sources | ForEach-Object { "build\$_.o" }
$objects += 'build\res.o'
$libraries = @(
    '-lole32', '-loleaut32', '-luuid', '-lshell32', '-lgdiplus', '-lshlwapi',
    '-lcomctl32', '-ladvapi32', '-lwinmm', '-lwinhttp', '-lbcrypt', '-luser32', '-lgdi32',
    '-lkernel32'
)

Write-Host "Enlazando..."
& $gpp @common -mwindows -static -static-libgcc -static-libstdc++ -s `
    @objects -o dist\NebulaMedia.exe @libraries
if ($LASTEXITCODE -ne 0) { throw 'Error enlazando la aplicacion.' }

$exe = Get-Item 'dist\NebulaMedia.exe'
Write-Host ("Listo: {0} ({1:N2} MB)" -f $exe.FullName, ($exe.Length / 1MB))
