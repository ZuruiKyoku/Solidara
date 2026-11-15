[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$buildEntry = Join-Path (Split-Path -Parent $PSScriptRoot) 'scripts/build-windows.ps1'
$powerShellExecutable = (Get-Process -Id $PID).Path
$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) ('solidara-build-tests-' + [Guid]::NewGuid().ToString('N'))
$pinnedVcpkg = '8f363bdc11359ede4bc51e5eb32a2617538049c8'
$script:passed = 0
$script:failed = 0
$script:skipped = 0
New-Item -ItemType Directory -Path $temporaryRoot | Out-Null

function Assert-True {
    param([bool] $Condition, [string] $Message)
    if (-not $Condition) { throw $Message }
}

function Assert-Throws {
    param([scriptblock] $Operation, [string] $MessagePattern)
    try { & $Operation } catch {
        if ($_.Exception.Message -notmatch $MessagePattern) {
            throw "Expected error matching '$MessagePattern'; received: $($_.Exception.Message)"
        }
        return
    }
    throw "Expected an error matching '$MessagePattern', but the operation succeeded."
}

function Test-Case {
    param([string] $Name, [scriptblock] $Body)
    try {
        & $Body
        $script:passed++
        Write-Host "PASS $Name"
    } catch {
        $script:failed++
        Write-Host "FAIL ${Name}: $($_.Exception.Message)" -ForegroundColor Red
    }
}

try {
    Test-Case 'Build script parses and can be dot-sourced without starting a build' {
        $tokens = $null
        $errors = $null
        $null = [System.Management.Automation.Language.Parser]::ParseFile($buildEntry, [ref] $tokens, [ref] $errors)
        Assert-True ($errors.Count -eq 0) "PowerShell parser errors: $errors"
    }
    . $buildEntry

    Test-Case 'Native successful output is streamed without polluting return values' {
        $result = @(Invoke-Checked $powerShellExecutable @('-NoLogo', '-NoProfile', '-NonInteractive', '-Command', "Write-Output 'native command output'; exit 0"))
        Assert-True ($result.Count -eq 0) 'Native stdout must not become a function return value.'
    }

    Test-Case 'Native nonzero exit stops the build and identifies the failed command' {
        Assert-Throws {
            Invoke-Checked $powerShellExecutable @('-NoLogo', '-NoProfile', '-NonInteractive', '-Command', "Write-Output 'expected native failure output'; exit 23")
        } 'failed with exit code 23'
    }

    Test-Case 'CMake 4.2 minimum accepts a compatible executable on PATH' {
        function Get-Command { [pscustomobject] @{ Source = 'Test-PathCMake' } }
        function Test-Path { $true }
        function Test-PathCMake { $global:LASTEXITCODE = 0; 'cmake version 4.2.0'; 'CMake suite maintained by Kitware' }
        Assert-True ((Find-CMake '') -eq 'Test-PathCMake') 'Compatible PATH CMake was not selected.'
    }

    Test-Case 'Older Visual Studio CMake falls back to a compatible PATH executable' {
        function Get-Command { [pscustomobject] @{ Source = 'Test-PathCMake' } }
        function Join-Path { 'Test-StudioCMake' }
        function Test-Path { $true }
        function Test-StudioCMake { $global:LASTEXITCODE = 0; 'cmake version 3.31.6' }
        function Test-PathCMake { $global:LASTEXITCODE = 0; 'cmake version 4.2.3' }
        Assert-True ((Find-CMake 'Studio fixture') -eq 'Test-PathCMake') 'Incompatible bundled CMake prevented PATH fallback.'
    }

    Test-Case 'Compatible bundled CMake is preferred without executing the PATH candidate' {
        function Get-Command { [pscustomobject] @{ Source = 'Test-PathCMake' } }
        function Join-Path { 'Test-StudioCMake' }
        function Test-Path { $true }
        function Test-StudioCMake { $global:LASTEXITCODE = 0; 'cmake version 4.3.1' }
        function Test-PathCMake { throw 'The lower-priority PATH executable should not run.' }
        Assert-True ((Find-CMake 'Studio fixture') -eq 'Test-StudioCMake') 'Bundled compatible CMake was not preferred.'
    }

    Test-Case 'CMake below the minimum is rejected with installation guidance' {
        function Get-Command { [pscustomobject] @{ Source = 'Test-PathCMake' } }
        function Test-Path { $true }
        function Test-PathCMake { $global:LASTEXITCODE = 0; 'cmake version 4.1.9' }
        Assert-Throws { Find-CMake '' } 'CMake 4\.2 or newer is required'
    }

    Test-Case 'CMake version text from a failed command is not trusted' {
        function Get-Command { [pscustomobject] @{ Source = 'Test-PathCMake' } }
        function Test-Path { $true }
        function Test-PathCMake { $global:LASTEXITCODE = 9; 'cmake version 4.3.1' }
        Assert-Throws { Find-CMake '' } 'CMake 4\.2 or newer is required'
    }

    Test-Case 'Missing Git reports the prerequisite before creating a managed checkout' {
        function Get-Command { $null }
        Assert-Throws { Initialize-Vcpkg (Join-Path $temporaryRoot 'no-git') } 'Git for Windows is required'
        Assert-True (-not (Test-Path (Join-Path $temporaryRoot 'no-git'))) 'Failed prerequisite detection created files.'
    }

    Test-Case 'An existing unmanaged directory is preserved and rejected' {
        $tools = Join-Path $temporaryRoot 'unmanaged'
        $root = Join-Path $tools "vcpkg-$pinnedVcpkg"
        New-Item -ItemType Directory -Path $root | Out-Null
        Set-Content -LiteralPath (Join-Path $root 'keep.txt') -Value 'user data'
        Assert-Throws { Initialize-Vcpkg $tools } 'not a Git checkout'
        Assert-True ((Get-Content -LiteralPath (Join-Path $root 'keep.txt') -Raw).Trim() -eq 'user data') 'Unmanaged data changed.'
    }

    Test-Case 'A dirty real Git checkout is rejected without reset or bootstrap' {
        $tools = Join-Path $temporaryRoot 'dirty'
        $root = Join-Path $tools "vcpkg-$pinnedVcpkg"
        New-Item -ItemType Directory -Path $root | Out-Null
        $git = (Get-Command git -CommandType Application | Select-Object -First 1).Source
        Invoke-Checked $git @('init', '--quiet', $root)
        Set-Content -LiteralPath (Join-Path $root 'keep.txt') -Value 'local unpublished work'
        $headBefore = Get-Content -LiteralPath (Join-Path $root '.git/HEAD') -Raw
        function Invoke-Checked { throw 'No modifying command may run against a dirty checkout.' }
        Assert-Throws { Initialize-Vcpkg $tools } 'has local changes'
        Assert-True ((Get-Content -LiteralPath (Join-Path $root 'keep.txt') -Raw).Trim() -eq 'local unpublished work') 'Local data changed.'
        Assert-True ((Get-Content -LiteralPath (Join-Path $root '.git/HEAD') -Raw) -eq $headBefore) 'The Git branch reference changed.'
    }

    Test-Case 'Multiple Git executables use the first PATH match' {
        $tools = Join-Path $temporaryRoot 'multiple-git'
        $root = Join-Path $tools "vcpkg-$pinnedVcpkg"
        New-Item -ItemType Directory -Path (Join-Path $root '.git') | Out-Null
        New-Item -ItemType Directory -Path (Join-Path $root 'scripts/buildsystems') | Out-Null
        Set-Content -LiteralPath (Join-Path $root 'scripts/buildsystems/vcpkg.cmake') -Value '# fixture'
        Set-Content -LiteralPath (Join-Path $root 'vcpkg.exe') -Value 'fixture executable'
        function Get-Command {
            [pscustomobject] @{ Source = 'Test-PreferredGit' }
            [pscustomobject] @{ Source = 'Test-ShadowedGit' }
        }
        function Test-PreferredGit { $global:LASTEXITCODE = 0 }
        function Test-ShadowedGit { throw 'The shadowed Git executable must not run.' }
        function Invoke-Checked {
            param([string] $FilePath, [string[]] $Arguments)
            Assert-True ($FilePath -eq 'Test-PreferredGit') 'Multiple executable paths were merged into an invalid command.'
        }
        Assert-True ((Initialize-Vcpkg $tools) -eq $root) 'Managed checkout setup did not return its root.'
    }

    Test-Case 'A clean managed checkout returns only its root and bootstraps once' {
        $tools = Join-Path $temporaryRoot 'clean'
        $root = Join-Path $tools "vcpkg-$pinnedVcpkg"
        New-Item -ItemType Directory -Path (Join-Path $root '.git') | Out-Null
        New-Item -ItemType Directory -Path (Join-Path $root 'scripts/buildsystems') | Out-Null
        Set-Content -LiteralPath (Join-Path $root 'scripts/buildsystems/vcpkg.cmake') -Value '# fixture'
        $script:managedCommands = New-Object System.Collections.ArrayList
        function Get-Command { [pscustomobject] @{ Source = 'Test-ManagedGit' } }
        function Test-ManagedGit { $global:LASTEXITCODE = 0 }
        function Invoke-Checked {
            param([string] $FilePath, [string[]] $Arguments)
            [void] $script:managedCommands.Add([pscustomobject] @{ FilePath = $FilePath; Arguments = $Arguments })
            Write-Host 'managed tool output'
            if ($FilePath -like '*bootstrap-vcpkg.bat') {
                Set-Content -LiteralPath (Join-Path (Split-Path -Parent $FilePath) 'vcpkg.exe') -Value 'fixture executable'
            }
        }
        $result = @(Initialize-Vcpkg $tools)
        Assert-True ($result.Count -eq 1 -and $result[0] -eq $root) 'Managed setup returned console output instead of a single root path.'
        Assert-True ($script:managedCommands.Count -eq 2) 'Expected pinned checkout followed by bootstrap.'
        Assert-True (($script:managedCommands[0].Arguments -join ' ') -match "checkout --detach $pinnedVcpkg") 'Checkout did not select the pinned revision.'
        Assert-True ($script:managedCommands[1].FilePath -like '*bootstrap-vcpkg.bat') 'Bootstrap was not invoked.'
        Assert-True ($script:managedCommands[1].Arguments -contains '-disableMetrics') 'Bootstrap metrics were not disabled.'
        $null = Initialize-Vcpkg $tools
        Assert-True ($script:managedCommands.Count -eq 3) 'An existing vcpkg executable was bootstrapped again.'
    }

    Test-Case 'A failed native Git status is rejected before checkout' {
        $tools = Join-Path $temporaryRoot 'broken-status'
        $root = Join-Path $tools "vcpkg-$pinnedVcpkg"
        New-Item -ItemType Directory -Path (Join-Path $root '.git') | Out-Null
        function Get-Command { [pscustomobject] @{ Source = 'Test-BrokenGit' } }
        function Test-BrokenGit { $global:LASTEXITCODE = 128 }
        function Invoke-Checked { throw 'Checkout must not run after status fails.' }
        Assert-Throws { Initialize-Vcpkg $tools } 'Cannot inspect the managed vcpkg checkout'
    }

    if ([Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT) {
        Test-Case 'Fresh Clean does not discover or download dependencies' {
            $fixtureRepository = Join-Path $temporaryRoot 'fresh-clean'
            $fixtureScripts = Join-Path $fixtureRepository 'scripts'
            New-Item -ItemType Directory -Path $fixtureScripts | Out-Null
            $fixtureEntry = Join-Path $fixtureScripts 'build-windows.ps1'
            Copy-Item -LiteralPath $buildEntry -Destination $fixtureEntry
            . $fixtureEntry
            function Find-CMake { throw 'Fresh Clean must not require CMake.' }
            function Initialize-Vcpkg { throw 'Fresh Clean must not set up dependencies.' }
            function Invoke-Checked { throw 'Fresh Clean must not execute a native command.' }
            Invoke-SolidaraBuild 'Clean' 'Release' ''
            Assert-True (-not (Test-Path (Join-Path $fixtureRepository 'build'))) 'Fresh Clean created a build tree.'
        }
    } else {
        $script:skipped++
        Write-Host 'SKIP Fresh Clean orchestration (requires Windows; OS guard remains active)'
    }

    if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
        Test-Case 'Direct non-Windows invocation fails helpfully with exit code 1' {
            $output = & $powerShellExecutable -NoLogo -NoProfile -NonInteractive -File $buildEntry
            Assert-True ($LASTEXITCODE -eq 1) 'Non-Windows entry point did not exit with code 1.'
            Assert-True (($output -join "`n") -match 'requires Windows and Visual Studio 2026') 'OS guard did not explain the supported platform.'
        }
    } else {
        $script:skipped++
        Write-Host 'SKIP Non-Windows guard process check (running on Windows)'
    }
} finally {
    Remove-Item -LiteralPath $temporaryRoot -Recurse -Force
}

Write-Host "Windows build helper tests: $script:passed passed; $script:failed failed; $script:skipped skipped."
Write-Host 'These tests validate PowerShell orchestration; they do not compile or run the Windows application.'
if ($script:failed -ne 0) { exit 1 }
exit 0
