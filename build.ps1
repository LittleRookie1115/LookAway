$ErrorActionPreference = "Stop"

& "$PSScriptRoot\tools\generate-icon.ps1"

$buildDirectory = Join-Path $PSScriptRoot "build"
$distDirectory = Join-Path $PSScriptRoot "dist"

if ((Get-Command mingw32-make -ErrorAction SilentlyContinue) -and
    (Get-Command g++ -ErrorAction SilentlyContinue)) {
    $generator = "MinGW Makefiles"
} elseif (Get-Command ninja -ErrorAction SilentlyContinue) {
    $generator = "Ninja"
} elseif ((Get-Command nmake -ErrorAction SilentlyContinue) -and
          (Get-Command cl -ErrorAction SilentlyContinue)) {
    $generator = "NMake Makefiles"
} else {
    throw "No supported C++ build tool was found. Install MinGW, Ninja, or Visual Studio Build Tools."
}

$cachePath = Join-Path $buildDirectory "CMakeCache.txt"
if (Test-Path -LiteralPath $cachePath) {
    $generatorLine = Select-String -LiteralPath $cachePath -Pattern '^CMAKE_GENERATOR:INTERNAL=(.+)$' |
        Select-Object -First 1
    $cachedGenerator = if ($generatorLine) { $generatorLine.Matches[0].Groups[1].Value } else { $null }
    if ($cachedGenerator -ne $generator) {
        $expectedBuildDirectory = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "build"))
        $resolvedBuildDirectory = [System.IO.Path]::GetFullPath($buildDirectory)
        if ($resolvedBuildDirectory -ne $expectedBuildDirectory) {
            throw "Refusing to remove unexpected build directory: $resolvedBuildDirectory"
        }
        Remove-Item -LiteralPath $resolvedBuildDirectory -Recurse -Force
    }
}

Write-Host "Using CMake generator: $generator"
cmake -S "$PSScriptRoot" -B "$buildDirectory" -G "$generator" -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmake --build "$buildDirectory" --config Release --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

ctest --test-dir "$buildDirectory" -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmake --install "$buildDirectory" --config Release --prefix "$distDirectory"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "LookAway is ready: $PSScriptRoot\dist\LookAway.exe"
