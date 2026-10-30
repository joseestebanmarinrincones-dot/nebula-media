param(
    [string]$Version = ''
)

$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root
if (-not (Test-Path 'VERSION')) { throw 'No se encontró VERSION.' }

$gh = (Get-Command gh.exe -ErrorAction SilentlyContinue).Source
if (-not $gh) { throw 'Falta GitHub CLI (gh). Instálalo con: winget install GitHub.cli' }
& $gh auth status *> $null
if ($LASTEXITCODE -ne 0) { throw 'GitHub CLI no tiene sesión. Ejecuta: gh auth login' }

function Find-Git {
    $cmd = Get-Command git.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $local = Join-Path $root 'tools\git\cmd\git.exe'
    if (Test-Path $local) { return $local }
    $temp = Join-Path $env:TEMP 'MinGit\cmd\git.exe'
    if (Test-Path $temp) { return $temp }

    Write-Host 'Descargando MinGit...'
    $release = & $gh api repos/git-for-windows/git/releases/latest | ConvertFrom-Json
    $asset = $release.assets | Where-Object { $_.name -match '^MinGit-.*-64-bit\.zip$' } | Select-Object -First 1
    if (-not $asset) { throw 'No se encontró MinGit en GitHub.' }
    $zip = Join-Path $root 'tools\MinGit.zip'
    $dest = Join-Path $root 'tools\git'
    if (Get-Command curl.exe -ErrorAction SilentlyContinue) {
        & curl.exe -sL --max-time 300 -o $zip $asset.browser_download_url
        if ($LASTEXITCODE -ne 0) { throw 'No se pudo descargar MinGit.' }
    } else {
        Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $zip -TimeoutSec 300
    }
    if (Test-Path $dest) { Remove-Item $dest -Recurse -Force }
    Expand-Archive -Path $zip -DestinationPath $dest -Force
    $found = Get-ChildItem -Path $dest -Filter git.exe -Recurse | Select-Object -First 1
    if (-not $found) { throw 'No se encontró git.exe.' }
    return $found.FullName
}

$git = Find-Git
$gitDir = Split-Path $git
$env:PATH = "$gitDir;$env:PATH"

if (-not (Test-Path '.git')) { throw 'La carpeta no es un repositorio Git.' }
$repo = (& $gh repo view --json nameWithOwner --jq .nameWithOwner).Trim()
if (-not $repo) { throw 'No se pudo identificar el repositorio de GitHub.' }
if ($repo -ne 'joseestebanmarinrincones-dot/nebula-media') {
    Write-Warning "El repositorio actual es $repo. Recuerda actualizar src/update_config.h."
}

$current = (Get-Content VERSION -Raw).Trim()
if (-not $Version) {
    $parts = $current.Split('.')
    $Version = "$($parts[0]).$($parts[1]).$([int]$parts[2] + 1)"
}
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw "Versión inválida: $Version" }
if ($Version -eq $current) { throw 'La versión nueva debe ser distinta de la actual.' }

$tag = "v$Version"
& $gh release view $tag 2>$null | Out-Null
if ($LASTEXITCODE -eq 0) { throw "El release $tag ya existe. Usa otro número de versión." }

Write-Host "Preparando Nebula Media $Version..."
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'build.ps1') -Version $Version
if ($LASTEXITCODE -ne 0) { throw 'Falló la compilación.' }
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'tools\make_installer.ps1') -Version $Version
if ($LASTEXITCODE -ne 0) { throw 'Falló la creación del instalador.' }

$exe = Join-Path $root 'dist\NebulaMedia.exe'
$setup = Join-Path $root "dist\NebulaMedia-Setup-$Version.exe"
$portable = Join-Path $root "dist\NebulaMedia-Portable-$Version.zip"
foreach ($file in @($exe, $setup, $portable)) {
    if (-not (Test-Path $file)) { throw "Falta el archivo: $file" }
}

$hash = (Get-FileHash $exe -Algorithm SHA256).Hash.ToLower()
$baseUrl = "https://github.com/$repo/releases/download/$tag"
@(
    "version=$Version",
    "url=$baseUrl/NebulaMedia.exe",
    "sha256=$hash"
) | Set-Content -Path 'dist\latest.txt' -Encoding ASCII

function Invoke-GitQuiet {
    param([string[]]$GitArgs)
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'SilentlyContinue'
    & $git @GitArgs 2>$null | Out-Null
    $code = $LASTEXITCODE
    $ErrorActionPreference = $previous
    return $code
}

Write-Host 'Guardando y subiendo el código...'
[void](Invoke-GitQuiet @('config', 'user.name', 'joseestebanmarinrincones-dot'))
[void](Invoke-GitQuiet @('config', 'user.email', '349629224+joseestebanmarinrincones-dot@users.noreply.github.com'))
[void](Invoke-GitQuiet @('config', 'core.autocrlf', 'false'))
& $gh auth setup-git *> $null
if ((Invoke-GitQuiet @('add', '-A')) -ne 0) { throw 'No se pudo preparar el commit.' }
$changes = & $git status --porcelain 2>$null
if ($changes) {
    if ((Invoke-GitQuiet @('commit', '-m', "Release v$Version")) -ne 0) {
        throw 'No se pudo crear el commit.'
    }
}
if ((Invoke-GitQuiet @('push', 'origin', 'main')) -ne 0) {
    throw 'No se pudo subir el código a GitHub.'
}
Write-Host 'Código subido.'

Write-Host "Publicando release $tag..."
$assets = @($setup, $portable, $exe, (Join-Path $root 'dist\latest.txt'),
            (Join-Path $root 'dist\SHA256SUMS.txt'))
& $gh release create $tag @assets --title "Nebula Media $Version" --latest `
    --notes "Actualización automática de Nebula Media. Versión $Version. Los usuarios reciben la actualización solos al abrir y cerrar la app."
if ($LASTEXITCODE -ne 0) { throw 'No se pudo publicar el release.' }

$desktop = [Environment]::GetFolderPath('Desktop')
$desktopSetup = Join-Path $desktop "NebulaMedia-Setup-$Version.exe"
Copy-Item -LiteralPath $setup -Destination $desktopSetup -Force
Get-ChildItem -Path $desktop -Filter 'NebulaMedia-Setup-*.exe' -File |
    Where-Object { $_.FullName -ne $desktopSetup } |
    Remove-Item -Force

Write-Host ''
Write-Host "Publicado: https://github.com/$repo/releases/tag/$tag"
Write-Host "Instalador para compartir: $desktopSetup"
Write-Host 'Tus amigos no necesitan hacer nada para las próximas versiones: la app se actualiza al cerrar.'
