$ErrorActionPreference = "Stop"

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmake --build build --config Release --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

ctest --test-dir build -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmake --install build --config Release --prefix dist
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "LookAway is ready: $PSScriptRoot\dist\LookAway.exe"
