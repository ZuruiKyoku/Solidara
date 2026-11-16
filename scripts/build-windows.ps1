[CmdletBinding()]
param(
    [ValidateSet('Build', 'Rebuild', 'Clean')]
    [string] $Action = 'Build',
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Release',
    [string] $VisualStudioRoot = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Invoke-Checked {
    param([string] $FilePath, [string[]] $Arguments)
    & $FilePath @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "$FilePath failed with exit code $LASTEXITCODE. See the preceding build output."
    }
}

function Find-CMake {
    param([string] $StudioRoot)
    $candidates = @()
    if ($StudioRoot) {
        $candidates += Join-Path $StudioRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
    }
    $onPath = Get-Command cmake -CommandType Application -ErrorAction SilentlyContinue
    if ($onPath) { $candidates += $onPath.Source }
    foreach ($candidate in ($candidates | Select-Object -Unique)) {
        if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) { continue }
        $description = & $candidate --version
        if ($LASTEXITCODE -eq 0 -and (($description -join "`n") -match 'cmake version (\d+\.\d+\.\d+)')) {
            if ([version] $Matches[1] -ge [version] '4.2.0') { return $candidate }
        }
    }
    throw 'CMake 4.2 or newer is required for Visual Studio 2026. Install C++ CMake tools through Visual Studio Installer, or install CMake and restart Visual Studio.'
}

function Complete-VcpkgClone {
    param([string] $GitExecutable, [string] $Root)
    $promisor = & $GitExecutable -C $Root config --bool --get remote.origin.promisor
    $configExit = $LASTEXITCODE
    if ($configExit -ne 0 -and $configExit -ne 1) { throw 'Cannot inspect the dependency checkout configuration.' }
    if ($promisor -ne 'true') { return }

    Write-Host 'Completing the existing dependency checkout before vcpkg starts. This one-time download includes historical port files...'
    # vcpkg extracts versioned ports with an isolated Git environment on Windows.
    # All historical blobs must already be local; lazy fetching there can lose DNS/proxy settings.
    Invoke-Checked $GitExecutable @('-C', $Root, 'fetch', '--refetch', '--no-filter', 'origin')
    $missingObjects = @(& $GitExecutable -C $Root rev-list --objects --all --missing=print |
        Where-Object { $_.StartsWith('?') })
    if ($LASTEXITCODE -ne 0 -or $missingObjects.Count -ne 0) {
        throw 'Dependency history is still incomplete. Check Git connectivity and retry Build; the existing checkout has been preserved.'
    }
    & $GitExecutable -C $Root config --local --unset-all remote.origin.partialclonefilter | Out-Host
    if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne 5) { throw 'Cannot remove the dependency checkout download filter.' }
    Invoke-Checked $GitExecutable @('-C', $Root, 'config', '--local', 'remote.origin.promisor', 'false')
}

function Initialize-Vcpkg {
    param([string] $ToolsDirectory)
    $revision = '8f363bdc11359ede4bc51e5eb32a2617538049c8'
    $root = Join-Path $ToolsDirectory "vcpkg-$revision"
    $git = Get-Command git -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $git) { throw 'Git for Windows is required for dependency setup. Install Git, then restart Visual Studio.' }
    if (-not (Test-Path -LiteralPath $root)) {
        Write-Host 'Preparing the pinned Solidara dependency tools (first build requires internet access)...'
        # Versioned ports need historical file contents locally during extraction.
        Invoke-Checked $git.Source @('clone', 'https://github.com/microsoft/vcpkg.git', $root)
    }
    if (-not (Test-Path -LiteralPath (Join-Path $root '.git'))) {
        throw "The managed tool directory is not a Git checkout: $root. Move it aside before retrying."
    }
    $status = & $git.Source -C $root status --porcelain
    if ($LASTEXITCODE -ne 0) { throw "Cannot inspect the managed vcpkg checkout: $root" }
    if ($status) { throw "The managed vcpkg checkout has local changes: $root. Preserve them before retrying; setup will not overwrite them." }
    Complete-VcpkgClone $git.Source $root
    Invoke-Checked $git.Source @('-C', $root, 'checkout', '--detach', $revision)
    $toolchain = Join-Path $root 'scripts/buildsystems/vcpkg.cmake'
    if (-not (Test-Path -LiteralPath $toolchain -PathType Leaf)) { throw "Missing vcpkg toolchain: $toolchain" }
    if (-not (Test-Path -LiteralPath (Join-Path $root 'vcpkg.exe') -PathType Leaf)) {
        Invoke-Checked (Join-Path $root 'bootstrap-vcpkg.bat') @('-disableMetrics')
    }
    return $root
}

function Invoke-SolidaraBuild {
    param([string] $RequestedAction, [string] $RequestedConfiguration, [string] $StudioRoot)
    if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
        throw 'This build entry point requires Windows and Visual Studio 2026. Use the linux-debug CMake preset on Linux.'
    }
    $repository = Split-Path -Parent $PSScriptRoot
    $buildDirectory = Join-Path $repository 'build/visual-studio'
    if ($RequestedAction -eq 'Clean' -and -not (Test-Path -LiteralPath (Join-Path $buildDirectory 'CMakeCache.txt'))) {
        Write-Host 'There is no configured build to clean.'
        return
    }
    $cmake = Find-CMake $StudioRoot
    New-Item -ItemType Directory -Force -Path $buildDirectory | Out-Null
    $lock = $null
    $toolsLock = $null
    try {
        # Prevent two IDE instances from changing the same CMake build tree concurrently.
        $lock = [IO.File]::Open((Join-Path $buildDirectory '.solidara-build.lock'), 'OpenOrCreate', 'ReadWrite', 'None')
        if ($RequestedAction -eq 'Clean') {
            Invoke-Checked $cmake @('--build', $buildDirectory, '--config', $RequestedConfiguration, '--target', 'clean')
            return
        }
        if (-not $env:LOCALAPPDATA) { throw 'LOCALAPPDATA is unavailable; cannot select a local dependency tools directory.' }
        $toolsDirectory = Join-Path $env:LOCALAPPDATA 'Solidara/Tools'
        New-Item -ItemType Directory -Force -Path $toolsDirectory | Out-Null
        # Other checkouts share the tool clone. Hold its lock through vcpkg configuration.
        $toolsLock = [IO.File]::Open((Join-Path $toolsDirectory '.setup.lock'), 'OpenOrCreate', 'ReadWrite', 'None')
        $env:VCPKG_ROOT = Initialize-Vcpkg $toolsDirectory
        $env:VCPKG_DISABLE_METRICS = '1'
        $configureArguments = @('-S', $repository, '-B', $buildDirectory,
            '-G', 'Visual Studio 18 2026', '-A', 'x64',
            "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake",
            '-DVCPKG_TARGET_TRIPLET=x64-windows', '-DBUILD_TESTING=ON')
        if ($StudioRoot) {
            $instance = [IO.Path]::GetFullPath($StudioRoot).TrimEnd([IO.Path]::DirectorySeparatorChar)
            $configureArguments += "-DCMAKE_GENERATOR_INSTANCE=$instance"
        }
        Invoke-Checked $cmake $configureArguments
        $toolsLock.Dispose()
        $toolsLock = $null
        $buildArguments = @('--build', $buildDirectory, '--config', $RequestedConfiguration, '--target', 'Solidara', '--parallel', '4')
        if ($RequestedAction -eq 'Rebuild') { $buildArguments += '--clean-first' }
        Invoke-Checked $cmake $buildArguments
    } catch [IO.IOException] {
        throw "Cannot access the build or dependency files. Check permissions and close other Solidara builds, then retry. $($_.Exception.Message)"
    } finally {
        if ($toolsLock) { $toolsLock.Dispose() }
        if ($lock) { $lock.Dispose() }
    }
}

if ($MyInvocation.InvocationName -ne '.') {
    try {
        Invoke-SolidaraBuild $Action $Configuration $VisualStudioRoot
        exit 0
    } catch {
        Write-Host "Solidara build failed: $($_.Exception.Message)" -ForegroundColor Red
        exit 1
    }
}
