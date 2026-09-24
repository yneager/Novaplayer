# Builds lambda-stream-server.exe: the open-source Stremio-compatible streaming
# server (stremio-native/stream-server, libtorrent backend) with LAMBDA's
# loopback-only host (lambda_stream_host.rs). Used by CI and local builds.
#
# Needs: git, Rust (cargo, x86_64-pc-windows-msvc), a vcpkg checkout, and an
# MSVC developer environment (cl.exe on PATH) for the C++ parts.
param(
  [Parameter(Mandatory = $true)][string]$WorkDir,
  [Parameter(Mandatory = $true)][string]$VcpkgRoot,
  [Parameter(Mandatory = $true)][string]$OutDir
)
$ErrorActionPreference = "Stop"

$pins = Get-Content -Raw (Join-Path $PSScriptRoot "versions.json") | ConvertFrom-Json
$src = Join-Path $WorkDir "stream-server"
$installed = Join-Path $WorkDir "vcpkg_installed"
$triplet = "x64-windows-lambda"

function Invoke-Checked([string]$exe, [string[]]$arguments) {
  # Native tools report progress on stderr; only the exit code means failure.
  $ErrorActionPreference = "Continue"
  & $exe @arguments 2>&1 | ForEach-Object { "$_" }
  if ($LASTEXITCODE -ne 0) { throw "$exe failed with exit code $LASTEXITCODE" }
}

New-Item -ItemType Directory -Force $WorkDir, $OutDir | Out-Null

if (-not (Test-Path (Join-Path $src ".git"))) {
  Invoke-Checked git @("init", "-q", $src)
  Invoke-Checked git @("-C", $src, "remote", "add", "origin", $pins.streamServer.repository)
  # Upstream line endings, so LAMBDA's patches apply on every machine.
  Invoke-Checked git @("-C", $src, "config", "core.autocrlf", "false")
}
$ErrorActionPreference = "Continue"
$head = (& git -C $src rev-parse --verify -q HEAD) 2>$null
$ErrorActionPreference = "Stop"
if ($head -ne $pins.streamServer.commit) {
  Invoke-Checked git @("-C", $src, "fetch", "-q", "--depth", "1", "origin", $pins.streamServer.commit)
}
Invoke-Checked git @("-C", $src, "checkout", "-q", "--force", $pins.streamServer.commit)
Invoke-Checked git @("-C", $src, "reset", "-q", "--hard")
# Fixes LAMBDA carries on top of the pinned upstream commit.
foreach ($patch in Get-ChildItem (Join-Path $PSScriptRoot "patches") -Filter *.patch | Sort-Object Name) {
  Invoke-Checked git @("-C", $src, "apply", "--whitespace=nowarn", $patch.FullName)
}

# libtorrent (upstream's pinned overlay port) + OpenSSL.
Invoke-Checked (Join-Path $VcpkgRoot "vcpkg.exe") @(
  "install", "--disable-metrics",
  "--x-manifest-root=$src",
  "--x-install-root=$installed",
  "--triplet=$triplet",
  "--overlay-triplets=$(Join-Path $PSScriptRoot 'triplets')",
  "--overlay-ports=$(Join-Path $src 'vcpkg-overlays')"
)

Copy-Item (Join-Path $PSScriptRoot "lambda_stream_host.rs") (Join-Path $src "server\examples\lambda_stream_host.rs") -Force

$env:VCPKG_ROOT = $VcpkgRoot
$env:VCPKG_INSTALLED_DIR = $installed
$env:VCPKGRS_TRIPLET = $triplet
# bindgen only parses the MSVC headers; newer STLs reject older libclang
# versions (STL1000) although parsing works.
if (-not $env:BINDGEN_EXTRA_CLANG_ARGS) { $env:BINDGEN_EXTRA_CLANG_ARGS = "-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH" }
# Upstream builds for x86-64-v3 (AVX2); the portable package targets any x64 CPU.
$env:RUSTFLAGS = "-C target-cpu=x86-64"
Push-Location $src
try {
  Invoke-Checked cargo @("build", "--release", "--locked", "-p", "server",
    "--example", "lambda_stream_host", "--no-default-features", "--features", "libtorrent")
} finally {
  Pop-Location
}

Copy-Item (Join-Path $src "target\release\examples\lambda_stream_host.exe") (Join-Path $OutDir "lambda-stream-server.exe") -Force
Copy-Item (Join-Path $src "LICENSE") (Join-Path $OutDir "stream-server-LICENSE.txt") -Force
Write-Host "Built $(Join-Path $OutDir 'lambda-stream-server.exe') from $($pins.streamServer.repository)@$($pins.streamServer.commit)"
