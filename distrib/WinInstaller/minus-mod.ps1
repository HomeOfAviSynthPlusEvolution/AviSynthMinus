# Windows x86+x64 core-only mod. Run the helper in a native x64 process.
# Dot-source for isolated filesystem tests; normal execution uses fixed system paths.
[CmdletBinding()]
param(
    [ValidateSet('Check', 'Install', 'CheckRestore', 'Restore')][string]$Mode,
    [string]$Payload,
    [string]$PayloadX86,
    [string]$Report,
    [switch]$AcceptManual
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-PlainPath([string]$Path) {
    $cursor = [IO.Path]::GetFullPath($Path)
    while ($cursor) {
        if (Test-Path -LiteralPath $cursor) {
            if ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Refusing a reparse point: $cursor"
            }
        }
        $cursor = [IO.Path]::GetDirectoryName($cursor)
    }
}

function Get-Digest([string]$Path) {
    Assert-PlainPath $Path
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-CoreIdentity([string]$Path, [ValidateSet('x64', 'x86')][string]$Architecture = 'x64') {
    Assert-PlainPath $Path
    $stream = [IO.File]::OpenRead($Path)
    $reader = [IO.BinaryReader]::new($stream)
    try {
        if ($reader.ReadUInt16() -ne 0x5a4d) { throw 'Not a PE file.' }
        $stream.Position = 0x3c
        $offset = $reader.ReadInt32()
        if ($offset -lt 64 -or $offset -gt $stream.Length - 6) { throw 'Invalid PE header.' }
        $stream.Position = $offset
        $machine = 0x8664
        if ($Architecture -eq 'x86') { $machine = 0x14c }
        if ($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne $machine) {
            throw "The core must match $Architecture (native AMD64 for x64, I386 for x86), not another architecture."
        }
    } finally { $reader.Dispose() }
    $info = [Diagnostics.FileVersionInfo]::GetVersionInfo($Path)
    if ($info.OriginalFilename -ine 'avisynth.dll') { throw 'Unknown core identity.' }
    $kind = 'unknown'
    if ($info.ProductName -match '^AviSynth\+ 3\.7\.5(?:\s|$)' -and
        $info.FileMajorPart -eq 3 -and $info.FileMinorPart -eq 7 -and $info.FileBuildPart -eq 5) {
        $kind = 'official'
    } elseif ($info.ProductName -match '^AviSynth- .*\[Like AviSynth\+ 3\.7\.5\]' -or
        ($info.ProductName -match '^AviSynth- .*\[3\.7\.5\]' -and
         $info.FileMajorPart -eq 3 -and $info.FileMinorPart -eq 7 -and $info.FileBuildPart -eq 5)) {
        $kind = 'minus'
    }
    if ($kind -eq 'unknown') { throw "Unsupported core: $($info.ProductName). Supported baseline: AviSynth+ 3.7.5." }
    [pscustomobject]@{ Kind = $kind; Version = $info.FileVersion; Hash = Get-Digest $Path }
}

function Get-ModContext([ValidateSet('x64', 'x86')][string]$Architecture = 'x64') {
    if (-not [Environment]::Is64BitProcess -or $env:PROCESSOR_ARCHITECTURE -ne 'AMD64') {
        throw 'Use native x64 Windows PowerShell on x64 Windows.'
    }
    $app = Join-Path ([Environment]::GetFolderPath('ProgramFiles')) 'AviSynthMinus Mod'
    $system = [Environment]::SystemDirectory
    if ($Architecture -eq 'x86') {
        $app = Join-Path $app 'x86'
        $system = Join-Path ([IO.Directory]::GetParent($system).FullName) 'SysWOW64'
    }
    [pscustomobject]@{
        Architecture = $Architecture
        App = $app
        Target = Join-Path $system 'AviSynth.dll'
        State = Join-Path $app 'state.json'
        Journal = Join-Path $app 'pending.json'
    }
}

function Get-OfficialBase([ValidateSet('x64', 'x86')][string]$Architecture = 'x64') {
    $guid = '{AC78780F-BACA-4805-8D4F-AE1B52B7E7D3}_is1'
    foreach ($view in @([Microsoft.Win32.RegistryView]::Registry32, [Microsoft.Win32.RegistryView]::Registry64)) {
        $root = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine, $view)
        try {
            $key = $root.OpenSubKey("SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\$guid")
            if ($null -eq $key) { continue }
            try {
                $location = [string]$key.GetValue('InstallLocation', '')
                $name = [string]$key.GetValue('DisplayName', '')
                $command = [string]$key.GetValue('UninstallString', '')
                if ($name -notmatch '^AviSynth\+' -or -not $location -or
                    -not (Test-Path -LiteralPath $location -PathType Container)) { continue }
                $uninstaller = ''
                if ($command -match '^"([^"]+\.exe)"') { $uninstaller = $Matches[1] }
                elseif ($command -match '^(.+?\.exe)(?:\s|$)') { $uninstaller = $Matches[1] }
                if (-not $uninstaller -or -not (Test-Path -LiteralPath $uninstaller -PathType Leaf)) { continue }
                Assert-PlainPath $location
                $prefix = [IO.Path]::GetFullPath($location).TrimEnd('\') + '\'
                if (-not [IO.Path]::GetFullPath($uninstaller).StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { continue }
                $coreView = [Microsoft.Win32.RegistryView]::Registry64
                if ($Architecture -eq 'x86') { $coreView = [Microsoft.Win32.RegistryView]::Registry32 }
                $native = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine, $coreView)
                try {
                    $avs = $native.OpenSubKey('SOFTWARE\AviSynth')
                    if ($null -eq $avs) { continue }
                    try { $coreLocation = [string]$avs.GetValue('', '') } finally { $avs.Dispose() }
                    if ($coreLocation.TrimEnd('\') -ine $location.TrimEnd('\')) { continue }
                } finally { $native.Dispose() }
                return $prefix
            } finally { $key.Dispose() }
        } finally { $root.Dispose() }
    }
    throw "Official AviSynth+ $Architecture installation not found. Install AviSynth+ 3.7.5 with its $Architecture component first: https://github.com/AviSynth/AviSynthPlus/releases/tag/v3.7.5"
}

function Get-ContextArchitecture($Context) {
    # Old isolated fixtures and the original x64 manifest need no migration.
    if ($Context.PSObject.Properties['Architecture']) { return $Context.Architecture }
    return 'x64'
}

function Read-ModState($Context) {
    Assert-PlainPath $Context.App
    if (Test-Path -LiteralPath $Context.Journal) {
        throw "An interrupted operation was detected. Keep all backups and inspect $($Context.Journal) before retrying."
    }
    if (-not (Test-Path -LiteralPath $Context.State)) { return $null }
    Assert-PlainPath $Context.State
    $state = Get-Content -LiteralPath $Context.State -Raw | ConvertFrom-Json
    if ($state.Schema -ne 1 -or $state.Target -ine $Context.Target -or
        $state.BackupFile -notmatch '^original-[0-9a-f]{32}\.dll$' -or
        $state.BackupKind -notin @('official', 'minus') -or
        $state.Status -notin @('active', 'restored', 'detached') -or
        $state.BackupHash -notmatch '^[0-9a-f]{64}$' -or $state.InstalledHash -notmatch '^[0-9a-f]{64}$') {
        throw 'Invalid backup manifest. Refusing to modify the core.'
    }
    return $state
}

function Write-JsonAtomic([string]$Path, $Value) {
    Assert-PlainPath $Path
    $temp = $Path + '.' + [guid]::NewGuid().ToString('N') + '.tmp'
    [IO.File]::WriteAllText($temp, ($Value | ConvertTo-Json -Depth 6), [Text.UTF8Encoding]::new($false))
    if (Test-Path -LiteralPath $Path) { [IO.File]::Replace($temp, $Path, [NullString]::Value) }
    else { [IO.File]::Move($temp, $Path) }
}

function Get-InstallPlan($Context, [string]$PayloadPath) {
    Assert-PlainPath $Context.Target
    $architecture = Get-ContextArchitecture $Context
    $base = Get-OfficialBase $architecture
    $incoming = Get-CoreIdentity $PayloadPath $architecture
    if ($incoming.Kind -ne 'minus') { throw 'The installer payload is not an AviSynth- core.' }
    $current = Get-CoreIdentity $Context.Target $architecture
    $state = Read-ModState $Context
    if ($null -ne $state -and $state.Status -eq 'active') {
        if ($base -ine $state.OfficialBase -or $current.Hash -ne $state.InstalledHash) {
            throw 'The official installation or core changed outside this mod. Uninstall the mod before establishing a new baseline.'
        }
        $backup = Join-Path $Context.App $state.BackupFile
        if ((Get-Digest $backup) -ne $state.BackupHash) { throw 'Original backup is missing or damaged.' }
    }
    [pscustomobject]@{ Base = $base; Incoming = $incoming; Current = $current; State = $state }
}

function Install-CoreMod($Context, [string]$PayloadPath, [bool]$AllowManual) {
    $plan = Get-InstallPlan $Context $PayloadPath
    $state = $plan.State
    if ($null -eq $state -or $state.Status -ne 'active') {
        if ($plan.Current.Kind -eq 'minus' -and -not $AllowManual) {
            throw 'Manual Minus installation: confirmation is required. Uninstall will restore this Minus DLL, not the official core.'
        }
        [IO.Directory]::CreateDirectory($Context.App) | Out-Null
        $backupName = 'original-' + [guid]::NewGuid().ToString('N') + '.dll'
        $backup = Join-Path $Context.App $backupName
        [IO.File]::Copy($Context.Target, $backup, $false)
        if ((Get-Digest $backup) -ne $plan.Current.Hash) { throw 'Backup verification failed.' }
        $state = [pscustomobject]@{
            Schema = 1; Target = $Context.Target; OfficialBase = $plan.Base
            BackupFile = $backupName; BackupHash = $plan.Current.Hash
            BackupKind = $plan.Current.Kind; BackupVersion = $plan.Current.Version
            InstalledHash = $plan.Incoming.Hash; InstalledVersion = $plan.Incoming.Version; Status = 'active'
        }
    } else {
        $state.InstalledHash = $plan.Incoming.Hash
        $state.InstalledVersion = $plan.Incoming.Version
    }
    Invoke-CoreTransaction $Context $PayloadPath $plan.Current.Hash $state
    return "Installed $($state.InstalledVersion). Uninstall restores $($state.BackupKind) $($state.BackupVersion). Backup: $($state.BackupFile)"
}

function Invoke-CoreTransaction($Context, [string]$Source, [string]$ExpectedHash, $NextState) {
    # Preserve the previous DLL until the manifest is committed. An interrupted
    # transaction leaves a journal and refuses further automatic operations.
    $id = [guid]::NewGuid().ToString('N')
    $stage = $Context.Target + '.minus-' + $id + '.tmp'
    $previous = Join-Path $Context.App ('previous-' + $id + '.dll')
    Assert-PlainPath $Context.Target
    $replaced = $false
    $committed = $false
    try {
        [IO.File]::Copy($Source, $stage, $false)
        $sourceHash = Get-Digest $Source
        $desiredHash = $NextState.InstalledHash
        if ($NextState.Status -eq 'restored') { $desiredHash = $NextState.BackupHash }
        if ($sourceHash -ne $desiredHash -or (Get-Digest $stage) -ne $sourceHash -or
            (Get-Digest $Context.Target) -ne $ExpectedHash) {
            throw 'Core changed during preparation. No replacement was performed.'
        }
        Write-JsonAtomic $Context.Journal ([ordered]@{ Target = $Context.Target; Previous = $previous; Before = $ExpectedHash; After = $sourceHash })
        [IO.File]::Replace($stage, $Context.Target, $previous)
        $replaced = $true
        if ((Get-Digest $previous) -ne $ExpectedHash) { throw 'Core changed concurrently with replacement.' }
        if ((Get-Digest $Context.Target) -ne $sourceHash) { throw 'Replacement verification failed.' }
        Write-JsonAtomic $Context.State $NextState
        $committed = $true
        [IO.File]::Delete($Context.Journal)
    } catch {
        $failure = $_
        # Once the new manifest is committed, keep the matching core and journal.
        # Rolling back only the DLL here would leave an inconsistent manifest.
        if ($committed) { throw $failure }
        if ($replaced) {
            # Roll back only our own replacement, never a third-party update.
            if ((Get-Digest $Context.Target) -eq $sourceHash) {
                [IO.File]::Replace($previous, $Context.Target, [NullString]::Value)
            }
        }
        if ((Get-Digest $Context.Target) -eq $ExpectedHash) { [IO.File]::Delete($Context.Journal) }
        throw $failure
    } finally {
        if (Test-Path -LiteralPath $stage) { [IO.File]::Delete($stage) }
    }
}

function Get-RestorePlan($Context) {
    $state = Read-ModState $Context
    if ($null -eq $state -or $state.Status -ne 'active') { return [pscustomobject]@{ Action = 'skip'; State = $state; Reason = 'No active mod state; system files will be left untouched.' } }
    $base = $null
    try { $base = Get-OfficialBase (Get-ContextArchitecture $Context) } catch { }
    if ($base -ine $state.OfficialBase -or -not (Test-Path -LiteralPath $Context.Target) -or
        (Get-Digest $Context.Target) -ne $state.InstalledHash) {
        return [pscustomobject]@{ Action = 'skip'; State = $state; Reason = 'Official installation or core was removed/changed. No DLL will be restored; backups will be retained.' }
    }
    $backup = Join-Path $Context.App $state.BackupFile
    if ((Get-Digest $backup) -ne $state.BackupHash) { throw 'Cannot restore: the original backup is missing or damaged. Uninstall cancelled.' }
    [pscustomobject]@{ Action = 'restore'; State = $state; Reason = "Restore $($state.BackupKind) $($state.BackupVersion); keep backup files." }
}

function Restore-CoreMod($Context) {
    $plan = Get-RestorePlan $Context
    if ($plan.Action -eq 'restore') {
        $plan.State.Status = 'restored'
        Invoke-CoreTransaction $Context (Join-Path $Context.App $plan.State.BackupFile) $plan.State.InstalledHash $plan.State
    } elseif ($null -ne $plan.State) {
        $plan.State.Status = 'detached'
        Write-JsonAtomic $Context.State $plan.State
    }
    return $plan.Reason
}

function Get-BundleInstallPlan($Contexts, $Payloads) {
    $x64 = Get-CoreIdentity $Payloads.x64 'x64'
    $x86 = Get-CoreIdentity $Payloads.x86 'x86'
    if ($x64.Kind -ne 'minus' -or $x86.Kind -ne 'minus' -or $x64.Version -ne $x86.Version) {
        throw 'Both payloads must be Minus DLLs of the same version.'
    }
    $items = @()
    foreach ($context in $Contexts) {
        $arch = Get-ContextArchitecture $context
        $state = Read-ModState $context
        $managed = $null -ne $state -and $state.Status -eq 'active'
        if (-not $managed) {
            # Missing upstream components are not installed by this overlay.
            if (-not (Test-Path -LiteralPath $context.Target)) { continue }
            try { $null = Get-OfficialBase $arch } catch { continue }
        }
        $plan = Get-InstallPlan $context $Payloads[$arch]
        $manual = ($null -eq $plan.State -or $plan.State.Status -ne 'active') -and $plan.Current.Kind -eq 'minus'
        $items += [pscustomobject]@{ Context = $context; Payload = $Payloads[$arch]; Plan = $plan; Manual = $manual }
    }
    if ($items.Count -eq 0) {
        throw 'No supported official AviSynth+ component found. Install AviSynth+ 3.7.5 with x86 and/or x64 first: https://github.com/AviSynth/AviSynthPlus/releases/tag/v3.7.5'
    }
    return $items
}

function Install-CoreBundle($Contexts, $Payloads, [bool]$AllowManual) {
    # Preflight every selected component before any replacement. Each component
    # has its own atomic DLL/state transaction; retries preserve completed work.
    $items = @(Get-BundleInstallPlan $Contexts $Payloads)
    if (@($items | Where-Object Manual).Count -gt 0 -and -not $AllowManual) {
        throw 'Manual Minus installation: confirmation is required before either component is updated.'
    }
    $completed = @()
    foreach ($item in $items) {
        $arch = Get-ContextArchitecture $item.Context
        try {
            $message = Install-CoreMod $item.Context $item.Payload $AllowManual
            $completed += "${arch}: $message"
        } catch {
            throw ("${arch} update failed: " + $_.Exception.Message + "`nAlready completed (retained for retry):`n" + ($completed -join "`n"))
        }
    }
    return ($completed -join "`n")
}

function Restore-CoreBundle($Contexts) {
    # Check every required backup before starting uninstall. If a later write
    # fails, Inno aborts removal; retry safely skips already restored components.
    foreach ($context in $Contexts) { $null = Get-RestorePlan $context }
    $completed = @()
    foreach ($context in $Contexts) {
        $arch = Get-ContextArchitecture $context
        try { $completed += "${arch}: $(Restore-CoreMod $context)" }
        catch { throw ("${arch} restore failed: " + $_.Exception.Message + "`nAlready completed:`n" + ($completed -join "`n")) }
    }
    return ($completed -join "`n")
}

if ($MyInvocation.InvocationName -ne '.') {
    $mutex = [Threading.Mutex]::new($false, 'Global\AviSynthMinusCoreModHelper')
    $held = $false
    try {
        try { $held = $mutex.WaitOne(0) }
        catch [Threading.AbandonedMutexException] { $held = $true }
        if (-not $held) { throw 'Another mod operation is in progress. Retry after it finishes.' }
        $contexts = @((Get-ModContext 'x64'), (Get-ModContext 'x86'))
        $payloads = @{ x64 = $Payload; x86 = $PayloadX86 }
        switch ($Mode) {
            'Check' {
                $items = @(Get-BundleInstallPlan $contexts $payloads)
                $selected = @($items | ForEach-Object { Get-ContextArchitecture $_.Context }) -join ', '
                $message = "Update installed components: $selected. Other components, plugins and settings remain untouched."
                $manual = @($items | Where-Object Manual | ForEach-Object { Get-ContextArchitecture $_.Context }) -join ', '
                if ($manual) {
                    $message += "`nManual Minus detected: $manual. These components will be backed up as Minus, NOT official AviSynth+. Uninstall restores those Minus DLLs. Cancel and repair official AviSynth+ first if you need official rollback."
                    if ($Report) { [IO.File]::WriteAllText($Report, $message) }
                    Write-Output $message
                    exit 10
                }
            }
            'Install' { $message = Install-CoreBundle $contexts $payloads $AcceptManual.IsPresent }
            'CheckRestore' { $message = (@($contexts | ForEach-Object { "$(Get-ContextArchitecture $_): $((Get-RestorePlan $_).Reason)" }) -join "`n") }
            'Restore' { $message = Restore-CoreBundle $contexts }
            default { throw 'A mode is required.' }
        }
        if ($Report) { [IO.File]::WriteAllText($Report, $message) }
        Write-Output $message
        exit 0
    } catch {
        $message = $_.Exception.Message
        if ($Report) { [IO.File]::WriteAllText($Report, $message) }
        Write-Error $message -ErrorAction Continue
        exit 1
    } finally {
        if ($held) { $mutex.ReleaseMutex() }
        $mutex.Dispose()
    }
}
