# Isolated filesystem tests: never call Get-ModContext or modify registry/system DLLs.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$OfficialDll,
    [Parameter(Mandatory)][string]$MinusDll,
    [string]$MinusDllX86
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'minus-mod.ps1')
$officialPath = (Resolve-Path -LiteralPath $OfficialDll).Path
$minusPath = (Resolve-Path -LiteralPath $MinusDll).Path
if ((Get-CoreIdentity $officialPath).Kind -ne 'official') { throw 'Official fixture required.' }
if ((Get-CoreIdentity $minusPath).Kind -ne 'minus') { throw 'Minus fixture required.' }
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('minus-mod-test-' + [guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($testRoot) | Out-Null
$script:BasePresent = $true
function Get-OfficialBase {
    if (-not $script:BasePresent) { throw 'Fixture: official installation removed.' }
    return (Join-Path $testRoot 'official-base')
}
function New-Fixture([string]$Name, [string]$InitialDll) {
    $dir = Join-Path $testRoot $Name
    [IO.Directory]::CreateDirectory($dir) | Out-Null
    $app = Join-Path $dir 'mod'
    $target = Join-Path $dir 'AviSynth.dll'
    [IO.File]::Copy($InitialDll, $target)
    return [pscustomobject]@{ App = $app; Target = $target; State = Join-Path $app 'state.json'; Journal = Join-Path $app 'pending.json' }
}
function Assert-Equal($Actual, $Expected, [string]$Label) {
    if ($Actual -ne $Expected) { throw "FAIL $Label : expected $Expected, got $Actual" }
    Write-Host "PASS $Label"
}
function Assert-Throws([scriptblock]$Action, [string]$Pattern, [string]$Label) {
    $caught = $null
    try { & $Action | Out-Null } catch { $caught = $_.Exception.Message }
    if ($null -eq $caught -or $caught -notmatch $Pattern) { throw "FAIL $Label : unexpected result '$caught'" }
    Write-Host "PASS $Label"
}
$originalHash = Get-Digest $officialPath
$minusHash = Get-Digest $minusPath

$wrongArch = Join-Path $testRoot 'wrong-arch.dll'
[IO.File]::Copy($minusPath, $wrongArch)
$bytes = [IO.File]::ReadAllBytes($wrongArch)
$pe = [BitConverter]::ToInt32($bytes, 0x3c)
$bytes[$pe + 4] = 0x4c
$bytes[$pe + 5] = 0x01
[IO.File]::WriteAllBytes($wrongArch, $bytes)
Assert-Throws { Get-CoreIdentity $wrongArch } 'native AMD64' 'x86 payload rejected'
$ctx = New-Fixture 'missing-core' $officialPath
[IO.File]::Delete($ctx.Target)
Assert-Throws { Get-InstallPlan $ctx $minusPath } 'Could not find' 'missing x64 core blocks install'
$ctx = New-Fixture 'wrong-payload' $officialPath
Assert-Throws { Get-InstallPlan $ctx $officialPath } 'not an AviSynth-' 'official DLL rejected as payload'

$ctx = New-Fixture 'official-install' $officialPath
Install-CoreMod $ctx $minusPath $false | Out-Null
$first = Read-ModState $ctx
Assert-Equal (Get-Digest $ctx.Target) $minusHash 'install replaces core'
Assert-Equal $first.BackupKind 'official' 'official backup classified correctly'
Assert-Equal (Get-Digest (Join-Path $ctx.App $first.BackupFile)) $originalHash 'official backup exact'
# Simulate a different build without loading it: append a byte outside the PE image.
$upgrade = Join-Path $testRoot 'upgrade.dll'
[IO.File]::Copy($minusPath, $upgrade)
$stream = [IO.File]::Open($upgrade, [IO.FileMode]::Append)
try { $stream.WriteByte(42) } finally { $stream.Dispose() }
Install-CoreMod $ctx $upgrade $false | Out-Null
Assert-Equal (Read-ModState $ctx).BackupFile $first.BackupFile 'upgrade preserves earliest backup'
Assert-Equal (Get-Digest $ctx.Target) (Get-Digest $upgrade) 'upgrade installs different build'
Restore-CoreMod $ctx | Out-Null
Assert-Equal (Get-Digest $ctx.Target) $originalHash 'uninstall restores official bytes'
Assert-Equal (Read-ModState $ctx).Status 'restored' 'restore commits state'
Install-CoreMod $ctx $minusPath $false | Out-Null
Assert-Equal ((Read-ModState $ctx).BackupFile -ne $first.BackupFile) $true 'reinstall takes a fresh baseline'

$ctx = New-Fixture 'manual-takeover' $minusPath
Assert-Throws { Install-CoreMod $ctx $upgrade $false } 'confirmation' 'manual takeover requires consent'
Install-CoreMod $ctx $upgrade $true | Out-Null
Assert-Equal (Read-ModState $ctx).BackupKind 'minus' 'manual Minus never labelled official'
Restore-CoreMod $ctx | Out-Null
Assert-Equal (Get-Digest $ctx.Target) $minusHash 'manual takeover restores preexisting Minus'

$ctx = New-Fixture 'external-change' $officialPath
Install-CoreMod $ctx $minusPath $false | Out-Null
[IO.File]::Copy($upgrade, $ctx.Target, $true)
Assert-Throws { Install-CoreMod $ctx $minusPath $false } 'changed outside' 'external change blocks upgrade'
Assert-Equal (Get-RestorePlan $ctx).Action 'skip' 'external change blocks restore'
Restore-CoreMod $ctx | Out-Null
Assert-Equal (Get-Digest $ctx.Target) (Get-Digest $upgrade) 'uninstall leaves external replacement intact'

$ctx = New-Fixture 'base-removed' $officialPath
Install-CoreMod $ctx $minusPath $false | Out-Null
$script:BasePresent = $false
Assert-Throws { Get-InstallPlan $ctx $minusPath } 'removed' 'missing official base blocks install'
Assert-Equal (Get-RestorePlan $ctx).Action 'skip' 'removed official base blocks restore'
$script:BasePresent = $true

$ctx = New-Fixture 'corrupt-backup' $officialPath
Install-CoreMod $ctx $minusPath $false | Out-Null
[IO.File]::WriteAllText((Join-Path $ctx.App (Read-ModState $ctx).BackupFile), 'corrupt')
Assert-Throws { Get-RestorePlan $ctx } 'missing or damaged' 'damaged backup cancels uninstall'
Assert-Throws { Get-InstallPlan $ctx $minusPath } 'missing or damaged' 'damaged backup blocks upgrade'

$ctx = New-Fixture 'interrupted' $officialPath
[IO.Directory]::CreateDirectory($ctx.App) | Out-Null
[IO.File]::WriteAllText($ctx.Journal, '{}')
Assert-Throws { Get-InstallPlan $ctx $minusPath } 'interrupted' 'pending journal blocks install'
Assert-Throws { Get-RestorePlan $ctx } 'interrupted' 'pending journal blocks uninstall'

$ctx = New-Fixture 'locked-core' $officialPath
$lock = [IO.File]::Open($ctx.Target, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::None)
try { Assert-Throws { Install-CoreMod $ctx $minusPath $false } 'used by another process' 'locked core fails safely' }
finally { $lock.Dispose() }
Assert-Equal (Get-Digest $ctx.Target) $originalHash 'locked core remains intact'

# Inject a manifest commit failure after File.Replace; transaction must roll back.
$script:RealWriter = ${function:Write-JsonAtomic}
$script:FailStateWrite = $true
function Write-JsonAtomic([string]$Path, $Value) {
    if ($script:FailStateWrite -and $Path.EndsWith('state.json')) { throw 'Injected manifest failure' }
    & $script:RealWriter $Path $Value
}
$ctx = New-Fixture 'failed-commit' $officialPath
Assert-Throws { Install-CoreMod $ctx $minusPath $false } 'Injected' 'manifest failure reported'
Assert-Equal (Get-Digest $ctx.Target) $originalHash 'manifest failure rolls back core'
Assert-Equal (Test-Path -LiteralPath $ctx.Journal) $false 'successful rollback clears journal'
$script:FailStateWrite = $false
Install-CoreMod $ctx $minusPath $false | Out-Null
Assert-Equal (Get-Digest $ctx.Target) $minusHash 'retry after rollback succeeds'

Write-Host "All isolated tests passed. Fixtures retained at $testRoot"

if ($MinusDllX86) {
    $x86Path = (Resolve-Path -LiteralPath $MinusDllX86).Path
    $x86Hash = Get-Digest $x86Path
    Assert-Equal (Get-CoreIdentity $x86Path 'x86').Kind 'minus' 'real x86 CI payload accepted'
    $payloads = @{ x64 = $minusPath; x86 = $x86Path }
    function New-Pair([string]$Name) {
        $a = New-Fixture ($Name + '-x64') $officialPath
        $b = New-Fixture ($Name + '-x86') $x86Path
        $a | Add-Member Architecture 'x64'
        $b | Add-Member Architecture 'x86'
        return @($a, $b)
    }
    $pair = New-Pair 'dual'
    Assert-Equal @(Get-BundleInstallPlan $pair $payloads).Count 2 'detect both components'
    Assert-Throws { Install-CoreBundle $pair $payloads $false } 'confirmation' 'mixed manual consent preflight'
    Assert-Equal (Get-Digest $pair[0].Target) $originalHash 'consent failure changes neither core'
    Install-CoreBundle $pair $payloads $true | Out-Null
    Assert-Equal (Get-Digest $pair[0].Target) $minusHash 'bundle updates x64'
    Assert-Equal (Get-Digest $pair[1].Target) $x86Hash 'bundle updates x86'
    Assert-Equal (Read-ModState $pair[0]).BackupKind 'official' 'x64 baseline remains official'
    Assert-Equal (Read-ModState $pair[1]).BackupKind 'minus' 'x86 baseline remains manual Minus'
    $baseline = (Read-ModState $pair[0]).BackupFile
    Install-CoreBundle $pair $payloads $false | Out-Null
    Assert-Equal (Read-ModState $pair[0]).BackupFile $baseline 'bundle upgrade keeps original backup'
    Restore-CoreBundle $pair | Out-Null
    Assert-Equal (Get-Digest $pair[0].Target) $originalHash 'bundle restores official x64'
    Assert-Equal (Get-Digest $pair[1].Target) $x86Hash 'bundle restores manual x86'

    $pair = New-Pair 'x64-only'
    [IO.File]::Delete($pair[1].Target)
    Assert-Equal @(Get-BundleInstallPlan $pair $payloads).Count 1 'x64-only component detection'
    Install-CoreBundle $pair $payloads $false | Out-Null
    Assert-Equal (Test-Path -LiteralPath $pair[1].Target) $false 'missing x86 component not created'
    $pair = New-Pair 'x86-only'
    [IO.File]::Delete($pair[0].Target)
    Install-CoreBundle $pair $payloads $true | Out-Null
    Assert-Equal (Test-Path -LiteralPath $pair[0].Target) $false 'missing x64 component not created'

    $pair = New-Pair 'legacy-x64-upgrade'
    Install-CoreMod $pair[0] $minusPath $false | Out-Null
    $baseline = (Read-ModState $pair[0]).BackupFile
    Install-CoreBundle $pair $payloads $true | Out-Null
    Assert-Equal (Read-ModState $pair[0]).BackupFile $baseline 'old x64 installer state retained by dual upgrade'
    [IO.File]::Delete($pair[0].Target)
    Assert-Throws { Get-BundleInstallPlan $pair $payloads } 'Could not find' 'missing managed component blocks upgrade'

    $pair = New-Pair 'locked-second'
    $lock = [IO.File]::Open($pair[1].Target, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::None)
    try { Assert-Throws { Install-CoreBundle $pair $payloads $true } 'used by another process' 'both cores preflight before replacement' }
    finally { $lock.Dispose() }
    Assert-Equal (Get-Digest $pair[0].Target) $originalHash 'second-core preflight failure leaves first unchanged'

    # Failure after x64 completed: x86 rolls back, both manifests stay accurate,
    # installer reports failure, and retry preserves the original x64 baseline.
    $script:FailPath = $pair[1].State
    function Write-JsonAtomic([string]$Path, $Value) {
        if ($Path -eq $script:FailPath) { throw 'Injected second-component commit failure' }
        & $script:RealWriter $Path $Value
    }
    Assert-Throws { Install-CoreBundle $pair $payloads $true } 'Already completed' 'partial completion explicitly reported'
    Assert-Equal (Get-Digest $pair[0].Target) $minusHash 'completed x64 remains tracked'
    Assert-Equal (Read-ModState $pair[0]).Status 'active' 'completed x64 manifest accurate'
    Assert-Equal (Test-Path -LiteralPath $pair[1].State) $false 'failed x86 has no false active manifest'
    $script:FailPath = ''
    Install-CoreBundle $pair $payloads $true | Out-Null
    Restore-CoreBundle $pair | Out-Null
    Assert-Equal (Get-Digest $pair[0].Target) $originalHash 'retry then uninstall restores original x64'
    Assert-Equal (Get-Digest $pair[1].Target) $x86Hash 'retry then uninstall restores original x86'

    $pair = New-Pair 'restore-preflight'
    Install-CoreBundle $pair $payloads $true | Out-Null
    [IO.File]::WriteAllText((Join-Path $pair[1].App (Read-ModState $pair[1]).BackupFile), 'broken')
    Assert-Throws { Restore-CoreBundle $pair } 'missing or damaged' 'both restore backups checked before changes'
    Assert-Equal (Get-Digest $pair[0].Target) $minusHash 'second bad backup leaves first core installed'
    Write-Host "All dual-architecture tests passed. Fixtures retained at $testRoot"
}
