[CmdletBinding()]
param(
    [string]$NdkRoot,
    [string]$OutputRoot,
    [ValidateRange(1, 8)][int]$Jobs = 4
)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path -LiteralPath $PSScriptRoot).Path
$sourceRoot = Join-Path $projectRoot 'core'
if (-not $NdkRoot) {
    $NdkRoot = Join-Path $env:LOCALAPPDATA 'Android/Sdk/ndk/29.0.14206865'
}
if (-not $OutputRoot) {
    $OutputRoot = Join-Path $projectRoot 'out'
}
$ndk = (Resolve-Path -LiteralPath $NdkRoot).Path
$output = [IO.Path]::GetFullPath($OutputRoot)
if (-not (Test-Path -LiteralPath (Join-Path $sourceRoot 'jni/Android.mk'))) {
    throw 'The pinned PCSX ReARMed source is missing.'
}
if ($sourceRoot -match '\s' -or $output -match '\s') {
    throw 'The NDK recipe requires source and output paths without spaces.'
}
if ($output -eq [IO.Path]::GetPathRoot($output) -or
    $output -eq $sourceRoot -or
    $sourceRoot.StartsWith($output.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Choose a dedicated output directory, not a source or drive root.'
}
$ndkProperties = Get-Content -LiteralPath (Join-Path $ndk 'source.properties') -Raw
if ($ndkProperties -notmatch 'Pkg.Revision\s*=\s*29\.0\.14206865') {
    throw 'This recipe was tested with Android NDK 29.0.14206865.'
}
New-Item -ItemType Directory -Path $output -Force | Out-Null
[IO.File]::WriteAllText((Join-Path $sourceRoot 'include/revision.h'), '#define REV "ff81ed17a15241d2f3730cdd7585b38e172532ca"' + "`n")
$objects = Join-Path $output 'obj'
$libraries = Join-Path $output 'libs'
$gitTools = Join-Path $env:ProgramFiles 'Git/usr/bin'
$previousPath = $env:PATH
$previousEpoch = $env:SOURCE_DATE_EPOCH
try {
    # Upstream make uses sed/diff/cp when generating its revision header.
    $env:PATH = "$gitTools;$previousPath"
    $env:SOURCE_DATE_EPOCH = '1790204415'
    & (Join-Path $ndk 'ndk-build.cmd') -C (Join-Path $sourceRoot 'jni') `
        NDK_PROJECT_PATH=null "APP_BUILD_SCRIPT=$sourceRoot/jni/Android.mk" `
        "NDK_APPLICATION_MK=$sourceRoot/jni/Application.mk" `
        "NDK_OUT=$objects" "NDK_LIBS_OUT=$libraries" `
        APP_ABI=arm64-v8a APP_PLATFORM=android-26 APP_STL=c++_static `
        EMUORBIT_SOURCE_REVISION=ff81ed1 EMUORBIT_SOURCE_DESCRIBE=r26l-112-gff81ed1 `
        USE_ASYNC_CDROM=0 USE_ASYNC_GPU=0 USE_ASYNC_SPU=0 `
        NDRC_THREAD=0 USE_LIBRETRO_VFS=1 -B "-j$Jobs"
    if ($LASTEXITCODE -ne 0) { throw "PCSX ReARMed build failed: $LASTEXITCODE" }
} finally {
    $env:PATH = $previousPath
    $env:SOURCE_DATE_EPOCH = $previousEpoch
}
$library = Join-Path $libraries 'arm64-v8a/libretro.so'
if (-not (Test-Path -LiteralPath $library)) { throw 'Core library was not produced.' }
$readelf = Join-Path $ndk 'toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-readelf.exe'
$segments = @(& $readelf --program-headers --wide $library | Where-Object { $_ -match '^\s*LOAD\s' })
if ($LASTEXITCODE -ne 0 -or -not $segments.Count -or
    @($segments | Where-Object { $_ -notmatch '0x4000\s*$' }).Count) {
    throw 'The ARM64 library does not have 16 KiB LOAD segment alignment.'
}
[ordered]@{
    source_revision = 'ff81ed17a15241d2f3730cdd7585b38e172532ca'
    ndk = '29.0.14206865'
    abi = 'arm64-v8a'
    api = 26
    sha256 = (Get-FileHash -LiteralPath $library -Algorithm SHA256).Hash
    bytes = (Get-Item -LiteralPath $library).Length
    output = $library
} | ConvertTo-Json
