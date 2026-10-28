param(
    [ValidateSet('Win32','x64')][string]$Platform = 'x64',
    [ValidateSet('Release','Debug')][string]$Configuration = 'Release'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$existingCmake = Get-Command cmake -ErrorAction SilentlyContinue
$cmake = if ($existingCmake) { $existingCmake.Source } else { Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' }
if (!(Test-Path -LiteralPath $cmake)) { throw 'The existing Visual Studio CMake component is required.' }
$source = Join-Path $root 'src\thirdparty\libass-deps'
if (!(Test-Path -LiteralPath "$source\sources\libass\libass\ass.h")) {
    throw 'Run tools/fetch_libass_sources.py using an existing Python 3 installation first.'
}
$build = Join-Path $root "bin\libass\$Platform"
# Configure once per source/build configuration; CMake tracks subsequent source edits.
if (!(Test-Path -LiteralPath "$build\CMakeCache.txt")) {
    $version = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion
    $major = [int]($version.Split('.')[0])
    $generators = @{16='Visual Studio 16 2019';17='Visual Studio 17 2022';18='Visual Studio 18 2026'}
    if (!$generators.ContainsKey($major)) { throw "Unsupported Visual Studio version: $version" }
    & $cmake -S $source -B $build -G $generators[$major] -A $Platform
    if ($LASTEXITCODE) { throw "libass configure failed ($LASTEXITCODE)" }
}
& $cmake --build $build --config $Configuration --target ass --parallel 1 -- /nr:false
if ($LASTEXITCODE) { throw "libass build failed ($LASTEXITCODE)" }
