# Builds the Release exe and packs the zip for GitHub Releases.
#   powershell -ExecutionPolicy Bypass -File release\make-release.ps1
# Output: dist\suguhiku-v<version>-windows-x64.zip
#   suguhiku.exe, the user guide (release\*.txt in Japanese), LICENSE.txt, licenses\ (third-party licenses)
# (This file is ASCII only on purpose: Windows PowerShell 5.1 misreads UTF-8 without BOM.)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# CMake installed by winget may not be on PATH yet
$env:Path = [System.Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [System.Environment]::GetEnvironmentVariable('Path', 'User')

# Version from CMakeLists.txt: project(GuitarPractice VERSION x.y.z ...)
$cmakeText = [System.IO.File]::ReadAllText((Join-Path $root 'CMakeLists.txt'))
$version = [regex]::Match($cmakeText, 'project\(GuitarPractice VERSION ([0-9.]+)').Groups[1].Value
if (-not $version) { throw 'Could not read the version from CMakeLists.txt' }

cmake -S . -B build -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE -ne 0) { throw 'cmake configure failed' }
cmake --build build --config Release --target GuitarPractice -- /m /v:minimal
if ($LASTEXITCODE -ne 0) { throw 'Release build failed' }

$exe = Join-Path $root 'build\GuitarPractice_artefacts\Release\suguhiku.exe'
if (-not (Test-Path $exe)) { throw "Not found: $exe" }

$name = "suguhiku-v$version-windows-x64"
$dist = Join-Path $root 'dist'
$stage = Join-Path $dist $name
$zip = Join-Path $dist "$name.zip"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage -Confirm:$false }
if (Test-Path $zip) { Remove-Item -Force $zip -Confirm:$false }
New-Item -ItemType Directory -Force (Join-Path $stage 'licenses') | Out-Null

Copy-Item $exe $stage
Copy-Item (Join-Path $root 'LICENSE') (Join-Path $stage 'LICENSE.txt')

# The user guide: every release\*.txt except the files that go into licenses\
$licenseFiles = @('licenses-README.txt', 'nlohmann-json-LICENSE.txt')
Get-ChildItem (Join-Path $root 'release') -Filter '*.txt' |
    Where-Object { $licenseFiles -notcontains $_.Name } |
    ForEach-Object { Copy-Item $_.FullName $stage }

# Third-party licenses (from the repo and from the dependencies fetched into build\)
$lic = Join-Path $stage 'licenses'
$copies = @(
    @('release\licenses-README.txt',                                           'README.txt'),
    @('release\nlohmann-json-LICENSE.txt',                                     'nlohmann-json-LICENSE.txt'),
    @('build\_deps\juce-src\LICENSE.md',                                       'JUCE-LICENSE.md'),
    @('Resources\Web\alphatab\alphaTab-LICENSE.txt',                           'alphaTab-LICENSE.txt'),
    @('Resources\Web\alphatab\Bravura-OFL.txt',                                'Bravura-OFL.txt'),
    @('Resources\Web\alphatab\sonivox-LICENSE.txt',                            'sonivox-LICENSE.txt'),
    @('Resources\Fonts\OFL.txt',                                               'DelaGothicOne-OFL.txt'),
    @('build\_deps\neuralampmodelercore-src\LICENSE',                          'NeuralAmpModelerCore-LICENSE.txt'),
    @('build\_deps\neuralampmodelercore-src\Dependencies\eigen\COPYING.MPL2',  'Eigen-COPYING.MPL2'),
    @('build\asiosdk\ASIOSDK\LICENSE.txt',                                     'ASIO-SDK-LICENSE.txt')
)
foreach ($c in $copies) { Copy-Item (Join-Path $root $c[0]) (Join-Path $lic $c[1]) }
$webview2 = Get-ChildItem (Join-Path $root 'build\webview2\pkg') -Directory | Select-Object -First 1
Copy-Item (Join-Path $webview2.FullName 'LICENSE.txt') (Join-Path $lic 'WebView2-LICENSE.txt')

# Zip with UTF-8 file names (the guide has a Japanese name)
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory($stage, $zip, [System.IO.Compression.CompressionLevel]::Optimal, $true, [System.Text.Encoding]::UTF8)

Write-Host "Done: $zip"
