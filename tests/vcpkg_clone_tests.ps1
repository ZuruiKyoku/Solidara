[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'scripts/build-windows.ps1')

$gitExecutable = (Get-Command git -CommandType Application | Select-Object -First 1).Source
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('solidara-clone-tests-' + [Guid]::NewGuid().ToString('N'))
$script:passed = 0
$failed = $false
New-Item -ItemType Directory -Path $fixture | Out-Null

function Assert-True {
    param([bool] $Condition, [string] $Message)
    if (-not $Condition) { throw $Message }
}

function Pass-Case {
    param([string] $Name)
    $script:passed++
    Write-Host "PASS $Name"
}

function Invoke-GitCapture {
    param([string[]] $Arguments, [int] $ExpectedExit = 0)
    # Windows PowerShell 5.1 wraps redirected native stderr as ErrorRecord.
    # Keep it as diagnostic output and check the actual native exit status.
    $previousPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        # Keep fixture bytes independent of the developer's Windows line-ending policy.
        $output = @(& $gitExecutable -c core.autocrlf=false @Arguments 2>&1)
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousPreference
    }
    if ($ExpectedExit -ge 0 -and $exitCode -ne $ExpectedExit) {
        throw "Git $($Arguments -join ' ') exited ${exitCode}: $($output -join "`n")"
    }
    return [pscustomobject] @{ ExitCode = $exitCode; Text = ($output -join "`n"); Lines = $output }
}

function Get-CheckoutState {
    param([string] $Root)
    $status = (Invoke-GitCapture @('-C', $Root, 'status', '--porcelain')).Text
    return [pscustomobject] @{
        Head = (Invoke-GitCapture @('-C', $Root, 'rev-parse', 'HEAD')).Text
        Status = $status
        Index = (Get-FileHash -LiteralPath (Join-Path $Root '.git/index') -Algorithm SHA256).Hash
        Contents = [IO.File]::ReadAllText((Join-Path $Root 'ports/widget/portfile.cmake'))
        UserFile = [IO.File]::ReadAllText((Join-Path $Root 'user-note.txt'))
    }
}

function Assert-CheckoutPreserved {
    param([string] $Root, $Before)
    Assert-True ((Get-FileHash -LiteralPath (Join-Path $Root '.git/index') -Algorithm SHA256).Hash -eq $Before.Index) 'The normal Git index changed.'
    Assert-True ((Invoke-GitCapture @('-C', $Root, 'rev-parse', 'HEAD')).Text -eq $Before.Head) 'The normal checkout HEAD changed.'
    Assert-True ((Invoke-GitCapture @('-C', $Root, 'status', '--porcelain')).Text -eq $Before.Status) 'The working tree or staged changes changed.'
    Assert-True ([IO.File]::ReadAllText((Join-Path $Root 'ports/widget/portfile.cmake')) -eq $Before.Contents) 'Tracked local edits changed.'
    Assert-True ([IO.File]::ReadAllText((Join-Path $Root 'user-note.txt')) -eq $Before.UserFile) 'Local user data changed.'
}

function Invoke-PortExtraction {
    param([string] $Root, [string] $Tree, [string] $Label)
    $outputDirectory = Join-Path $fixture "extracted-$Label"
    $index = Join-Path $fixture "index-$Label"
    New-Item -ItemType Directory -Path $outputDirectory | Out-Null
    $previousIndex = $env:GIT_INDEX_FILE
    try {
        $env:GIT_INDEX_FILE = $index
        $null = Invoke-GitCapture @('--git-dir', (Join-Path $Root '.git'), '--work-tree', $outputDirectory, 'read-tree', '--', $Tree)
        $prefix = $outputDirectory.Replace('\', '/') + '/'
        $result = Invoke-GitCapture @('--git-dir', (Join-Path $Root '.git'), '--work-tree', $outputDirectory, 'checkout-index', '-af', "--prefix=$prefix") -ExpectedExit -1
        return [pscustomobject] @{ ExitCode = $result.ExitCode; Diagnostics = $result.Text; Directory = $outputDirectory }
    } finally {
        $env:GIT_INDEX_FILE = $previousIndex
    }
}

try {
    $seed = Join-Path $fixture 'seed'
    $server = Join-Path $fixture 'server.git'
    $partial = Join-Path $fixture 'partial'
    $null = Invoke-GitCapture @('init', '--quiet', '--initial-branch=main', $seed)
    $versions = New-Object System.Collections.ArrayList
    foreach ($version in 1..3) {
        foreach ($port in @('widget', 'transitive')) {
            $directory = Join-Path $seed "ports/$port"
            New-Item -ItemType Directory -Force -Path $directory | Out-Null
            [IO.File]::WriteAllText((Join-Path $directory 'portfile.cmake'), "# $port recipe version $version`n")
        }
        $null = Invoke-GitCapture @('-C', $seed, 'add', '--all')
        $null = Invoke-GitCapture @('-C', $seed, '-c', 'user.name=ZuruiKyoku', '-c', 'user.email=30306568+ZuruiKyoku@users.noreply.github.com', '-c', 'commit.gpgSign=false', 'commit', '--quiet', '-m', "Add fixture recipes version $version")
        foreach ($port in @('widget', 'transitive')) {
            $tree = (Invoke-GitCapture @('-C', $seed, 'rev-parse', "HEAD:ports/$port")).Text
            [void] $versions.Add([pscustomobject] @{ Version = $version; Port = $port; Tree = $tree; Contents = "# $port recipe version $version`n" })
        }
    }
    $null = Invoke-GitCapture @('clone', '--quiet', '--bare', $seed, $server)
    $null = Invoke-GitCapture @('--git-dir', $server, 'config', 'uploadpack.allowFilter', 'true')
    $null = Invoke-GitCapture @('--git-dir', $server, 'config', 'uploadpack.allowAnySHA1InWant', 'true')
    # file:// deliberately uses upload-pack; a plain local path clone ignores filtering.
    $serverUri = ([UriBuilder]::new('file', '', -1, [IO.Path]::GetFullPath($server))).Uri.AbsoluteUri
    $offlineUri = ([UriBuilder]::new('file', '', -1, [IO.Path]::GetFullPath((Join-Path $fixture 'unavailable.git')))).Uri.AbsoluteUri
    $null = Invoke-GitCapture @('-c', 'protocol.file.allow=always', 'clone', '--quiet', '--filter=blob:none', $serverUri, $partial)
    $missingBefore = @((Invoke-GitCapture @('-C', $partial, 'rev-list', '--objects', '--all', '--missing=print')).Lines | Where-Object { "$_".StartsWith('?') })
    Assert-True ($missingBefore.Count -ge 4) 'Fixture is not genuinely partial; historical blobs should be absent.'
    Assert-True ((Invoke-GitCapture @('-C', $partial, 'config', '--bool', '--get', 'remote.origin.promisor')).Text -eq 'true') 'The clone is not configured as a promisor clone.'
    Pass-Case 'Real filtered transport omits historical blobs for both direct and transitive ports'

    [IO.File]::WriteAllText((Join-Path $partial 'user-note.txt'), 'preserve unpublished work')
    [IO.File]::AppendAllText((Join-Path $partial 'ports/widget/portfile.cmake'), '# local edit')
    $null = Invoke-GitCapture @('-C', $partial, 'add', 'user-note.txt')
    $before = Get-CheckoutState $partial
    $null = Invoke-GitCapture @('-C', $partial, 'remote', 'set-url', 'origin', $offlineUri)
    $initial = Invoke-PortExtraction $partial $versions[0].Tree 'missing'
    Assert-True ($initial.ExitCode -ne 0) 'Historical extraction unexpectedly succeeded without the missing blob or a remote.'
    Assert-True ($initial.Diagnostics -match 'promisor|fetch|remote') 'The expected failure did not identify the unavailable lazy fetch.'
    Assert-CheckoutPreserved $partial $before
    Pass-Case 'Historical read-tree succeeds but checkout-index fails when lazy fetching is unavailable'

    $migrationFailed = $false
    try { Complete-VcpkgClone $gitExecutable $partial } catch { $migrationFailed = $true }
    Assert-True $migrationFailed 'Migration unexpectedly succeeded with an unavailable origin.'
    Assert-True ((Invoke-GitCapture @('-C', $partial, 'config', '--bool', '--get', 'remote.origin.promisor')).Text -eq 'true') 'Failed migration disabled promisor handling.'
    Assert-True ((Invoke-GitCapture @('-C', $partial, 'config', '--get', 'remote.origin.partialclonefilter')).Text -eq 'blob:none') 'Failed migration removed the download filter.'
    Assert-CheckoutPreserved $partial $before
    Pass-Case 'Failed hydration preserves promisor configuration, HEAD, index and local work'

    $null = Invoke-GitCapture @('-C', $partial, 'remote', 'set-url', 'origin', $serverUri)
    $migrationOutput = @(Complete-VcpkgClone $gitExecutable $partial)
    Assert-True ($migrationOutput.Count -eq 0) 'Migration leaked native output into its success return stream.'
    $missingAfter = @((Invoke-GitCapture @('-C', $partial, 'rev-list', '--objects', '--all', '--missing=print')).Lines | Where-Object { "$_".StartsWith('?') })
    Assert-True ($missingAfter.Count -eq 0) 'Migration left reachable historical objects missing.'
    Assert-True ((Invoke-GitCapture @('-C', $partial, 'config', '--bool', '--get', 'remote.origin.promisor')).Text -eq 'false') 'Successful migration did not disable promisor fetching.'
    $null = Invoke-GitCapture @('-C', $partial, 'config', '--get', 'remote.origin.partialclonefilter') -ExpectedExit 1
    Assert-CheckoutPreserved $partial $before
    Pass-Case 'Real refetch without filtering completes history before clearing partial-clone configuration'

    $null = Invoke-GitCapture @('-C', $partial, 'remote', 'set-url', 'origin', $offlineUri)
    foreach ($entry in $versions) {
        $extraction = Invoke-PortExtraction $partial $entry.Tree "$($entry.Port)-$($entry.Version)"
        Assert-True ($extraction.ExitCode -eq 0) "Offline historical extraction failed: $($extraction.Diagnostics)"
        Assert-True ([IO.File]::ReadAllText((Join-Path $extraction.Directory 'portfile.cmake')) -eq $entry.Contents) 'The extracted historical recipe does not match its original version.'
    }
    Assert-CheckoutPreserved $partial $before
    Pass-Case 'All six historical port trees extract correctly after origin becomes unavailable'

    Complete-VcpkgClone $gitExecutable $partial
    Assert-CheckoutPreserved $partial $before
    Pass-Case 'Completed migration is idempotent and needs no origin connectivity'

    $full = Join-Path $fixture 'full'
    $null = Invoke-GitCapture @('-c', 'protocol.file.allow=always', 'clone', '--quiet', $serverUri, $full)
    $null = Invoke-GitCapture @('-C', $full, 'remote', 'set-url', 'origin', $offlineUri)
    Complete-VcpkgClone $gitExecutable $full
    foreach ($entry in $versions) {
        $extraction = Invoke-PortExtraction $full $entry.Tree "full-$($entry.Port)-$($entry.Version)"
        Assert-True ($extraction.ExitCode -eq 0) 'A fresh complete clone required origin for historical extraction.'
        Assert-True ([IO.File]::ReadAllText((Join-Path $extraction.Directory 'portfile.cmake')) -eq $entry.Contents) 'Full-clone historical contents differ.'
    }
    Pass-Case 'New unfiltered clones supply every historical recipe without deferred downloads'
} catch {
    $failed = $true
    Write-Host "FAIL $($_.Exception.Message)" -ForegroundColor Red
} finally {
    Remove-Item -LiteralPath $fixture -Recurse -Force
}

$failureCount = [int] $failed
Write-Host "vcpkg clone regression tests: $script:passed passed; $failureCount failed."
Write-Host 'Tests use local Git transport and real extraction commands; no Windows compilation is performed.'
if ($failed) { exit 1 }
exit 0
